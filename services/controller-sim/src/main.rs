use std::time::Duration;

use sentient_protocol::{
    AckStatus, CommandAck, CommandEnvelope, DeviceState, Heartbeat, Presence, PresenceStatus,
    SafetyState, SafetyStateKind, SCHEMA_VERSION,
};
use tokio::time::MissedTickBehavior;
use tracing::{info, warn};
use uuid::Uuid;

#[derive(Debug, Clone)]
struct AuthConfig {
    enforce: bool,
    hmac_key: Option<Vec<u8>>,
}

#[derive(Debug, Clone)]
struct SimBehavior {
    drop_first_accepted_ack: bool,
}

#[derive(Debug, Clone)]
struct SimSafetyConfig {
    kind: SafetyStateKind,
    latched: bool,
    reason_code: Option<String>,
    trigger_fault_after_ms: Option<u64>,
}

impl SimSafetyConfig {
    fn from_env() -> Self {
        let kind = std::env::var("SIM_SAFETY_KIND")
            .ok()
            .and_then(|v| match v.as_str() {
                "SAFE" => Some(SafetyStateKind::Safe),
                "BLOCKED" => Some(SafetyStateKind::Blocked),
                "FAULT" => Some(SafetyStateKind::Fault),
                "E_STOP" => Some(SafetyStateKind::EStop),
                "MAINTENANCE" => Some(SafetyStateKind::Maintenance),
                _ => None,
            })
            .unwrap_or(SafetyStateKind::Safe);
        let latched = parse_bool_env("SIM_SAFETY_LATCHED").unwrap_or(false);
        let reason_code = std::env::var("SIM_SAFETY_REASON_CODE")
            .ok()
            .filter(|v| !v.is_empty());
        let trigger_fault_after_ms = std::env::var("SIM_TRIGGER_FAULT_AFTER_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .filter(|v| *v > 0);
        Self {
            kind,
            latched,
            reason_code,
            trigger_fault_after_ms,
        }
    }

    fn initial_state(&self) -> SafetyState {
        // Start SAFE unless explicitly configured otherwise; optional timed trigger can escalate later.
        if self.trigger_fault_after_ms.is_some() {
            return SafetyState {
                kind: SafetyStateKind::Safe,
                reason_code: None,
                latched: false,
            };
        }
        SafetyState {
            kind: self.kind,
            reason_code: self.reason_code.clone(),
            latched: self.latched,
        }
    }

    fn triggered_state(&self) -> SafetyState {
        SafetyState {
            kind: self.kind,
            reason_code: self.reason_code.clone(),
            latched: self.latched,
        }
    }
}

#[derive(Debug, Default, Clone)]
struct CommandRecord {
    accepted_sent: bool,
    completed_sent: bool,
    completed: bool,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
    let device_id = std::env::var("DEVICE_ID").unwrap_or_else(|_| "sim1".to_string());

    let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "mqtt".to_string());
    let mqtt_port: u16 = std::env::var("MQTT_PORT")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(1883);
    let mqtt_username = std::env::var("MQTT_USERNAME")
        .ok()
        .filter(|v| !v.is_empty());
    let mqtt_password = std::env::var("MQTT_PASSWORD")
        .ok()
        .filter(|v| !v.is_empty());

    let client_id = std::env::var("MQTT_CLIENT_ID").unwrap_or_else(|_| {
        format!(
            "controller-sim-{}-{}-{}",
            room_id,
            device_id,
            Uuid::new_v4()
        )
    });

    let enforce_cmd_auth = parse_bool_env("ENFORCE_CMD_AUTH").unwrap_or(false);
    let hmac_key = std::env::var("DEVICE_HMAC_KEY_HEX")
        .ok()
        .filter(|v| !v.trim().is_empty())
        .map(|hex| hex::decode(hex.trim()))
        .transpose()?;
    if enforce_cmd_auth && hmac_key.is_none() {
        anyhow::bail!("ENFORCE_CMD_AUTH is enabled but DEVICE_HMAC_KEY_HEX is not set");
    }
    let auth = AuthConfig {
        enforce: enforce_cmd_auth,
        hmac_key,
    };

    let behavior = SimBehavior {
        drop_first_accepted_ack: parse_bool_env("SIM_DROP_FIRST_ACCEPTED_ACK").unwrap_or(false),
    };
    let mut dropped_first_accepted_ack = false;
    let mut commands: std::collections::HashMap<Uuid, CommandRecord> =
        std::collections::HashMap::new();

    let safety_cfg = SimSafetyConfig::from_env();
    let mut current_safety = safety_cfg.initial_state();

    info!(
        room_id = %room_id,
        device_id = %device_id,
        mqtt_host = %mqtt_host,
        mqtt_port,
        enforce_cmd_auth = auth.enforce,
        drop_first_accepted_ack = behavior.drop_first_accepted_ack,
        sim_safety_kind = ?safety_cfg.kind,
        sim_safety_latched = safety_cfg.latched,
        sim_trigger_fault_after_ms = ?safety_cfg.trigger_fault_after_ms,
        "controller-sim starting"
    );

    let presence_topic = format!("room/{}/device/{}/presence", room_id, device_id);
    let last_will = Presence {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.clone(),
        device_id: device_id.clone(),
        status: PresenceStatus::Offline,
        observed_at_unix_ms: unix_ms_now(),
    };
    let last_will_payload = serde_json::to_vec(&last_will)?;

    let (client, mut eventloop) = {
        let mut options = rumqttc::MqttOptions::new(client_id, mqtt_host, mqtt_port);
        options.set_keep_alive(Duration::from_secs(5));
        options.set_last_will(rumqttc::LastWill::new(
            presence_topic.clone(),
            last_will_payload,
            rumqttc::QoS::AtLeastOnce,
            true, // retained
        ));
        if let (Some(user), Some(pass)) = (mqtt_username.as_deref(), mqtt_password.as_deref()) {
            options.set_credentials(user, pass);
        }
        rumqttc::AsyncClient::new(options, 200)
    };

    let cmd_topic = format!("room/{}/device/{}/cmd", room_id, device_id);
    client
        .subscribe(cmd_topic.clone(), rumqttc::QoS::AtLeastOnce)
        .await?;

    let hb_topic = format!("room/{}/device/{}/heartbeat", room_id, device_id);
    let ack_topic = format!("room/{}/device/{}/ack", room_id, device_id);
    let state_topic = format!("room/{}/device/{}/state", room_id, device_id);

    // Publish retained ONLINE presence on startup.
    let online = Presence {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.clone(),
        device_id: device_id.clone(),
        status: PresenceStatus::Online,
        observed_at_unix_ms: unix_ms_now(),
    };
    if let Ok(payload) = serde_json::to_vec(&online) {
        if let Err(err) = client
            .publish(&presence_topic, rumqttc::QoS::AtLeastOnce, true, payload)
            .await
        {
            warn!(error = %err, "failed to publish ONLINE presence");
        }
    }

    // Publish initial retained state snapshot on startup.
    publish_state(
        &client,
        &state_topic,
        &room_id,
        &device_id,
        &current_safety,
        serde_json::json!({"booted": true}),
    )
    .await;

    info!(%cmd_topic, %hb_topic, %ack_topic, %state_topic, "subscribed and ready");

    let start = tokio::time::Instant::now();
    let mut hb = tokio::time::interval(Duration::from_millis(1000));
    hb.set_missed_tick_behavior(MissedTickBehavior::Skip);

    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
            _ = hb.tick() => {
                if let Some(ms) = safety_cfg.trigger_fault_after_ms {
                    if start.elapsed().as_millis() as u64 >= ms
                        && current_safety.kind == SafetyStateKind::Safe
                    {
                        current_safety = safety_cfg.triggered_state();
                        warn!(
                            safety=?current_safety.kind,
                            latched=current_safety.latched,
                            "SIM_TRIGGER_FAULT_AFTER_MS: safety escalated"
                        );
                        publish_state(
                            &client,
                            &state_topic,
                            &room_id,
                            &device_id,
                            &current_safety,
                            serde_json::json!({"sim_triggered": true}),
                        )
                        .await;
                    }
                }
                let msg = Heartbeat {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: room_id.clone(),
                    device_id: device_id.clone(),
                    uptime_ms: start.elapsed().as_millis() as u64,
                    firmware_version: "sim-0.1.0".to_string(),
                    safety_state: current_safety.clone(),
                    last_error: None,
                    observed_at_unix_ms: unix_ms_now(),
                };
                if let Ok(payload) = serde_json::to_vec(&msg) {
                    if let Err(err) = client.publish(&hb_topic, rumqttc::QoS::AtMostOnce, false, payload).await {
                        warn!(error = %err, "failed to publish heartbeat");
                    }
                }
            }
            ev = eventloop.poll() => {
                match ev {
                    Ok(rumqttc::Event::Incoming(rumqttc::Packet::Publish(p))) => {
                        if p.topic == cmd_topic {
                            handle_command(
                                &client,
                                &ack_topic,
                                &state_topic,
                                &room_id,
                                &device_id,
                                &auth,
                                &behavior,
                                &mut dropped_first_accepted_ack,
                                &mut commands,
                                &mut current_safety,
                                &p.payload,
                            )
                            .await;
                        }
                    }
                    Ok(_) => {}
                    Err(err) => {
                        warn!(error = %err, "mqtt eventloop error");
                        tokio::time::sleep(Duration::from_secs(1)).await;
                    }
                }
            }
        }
    }

    Ok(())
}

async fn handle_command(
    client: &rumqttc::AsyncClient,
    ack_topic: &str,
    state_topic: &str,
    room_id: &str,
    device_id: &str,
    auth: &AuthConfig,
    behavior: &SimBehavior,
    dropped_first_accepted_ack: &mut bool,
    commands: &mut std::collections::HashMap<Uuid, CommandRecord>,
    current_safety: &mut SafetyState,
    payload: &[u8],
) {
    let cmd: CommandEnvelope = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error = %err, "invalid command payload (json)");
            return;
        }
    };

    info!(
        room_id = %room_id,
        device_id = %device_id,
        command_id = %cmd.command_id,
        correlation_id = %cmd.correlation_id,
        sequence = cmd.sequence,
        "received command"
    );

    if auth.enforce {
        let Some(key) = auth.hmac_key.as_deref() else {
            warn!("command auth enforced but no key present");
            return;
        };

        match sentient_protocol::verify_command_hmac_sha256(&cmd, key) {
            Ok(true) => {}
            Ok(false) => {
                publish_rejected_ack(
                    client,
                    ack_topic,
                    room_id,
                    device_id,
                    current_safety,
                    &cmd,
                    "AUTH_INVALID",
                )
                .await;
                return;
            }
            Err(err) => {
                warn!(error = %err, "failed to verify command auth");
                publish_rejected_ack(
                    client,
                    ack_topic,
                    room_id,
                    device_id,
                    current_safety,
                    &cmd,
                    "AUTH_ERROR",
                )
                .await;
                return;
            }
        }
    }

    let record = commands.entry(cmd.command_id).or_default();
    if record.completed {
        // Duplicate delivery (e.g., core retry). Re-ack without re-executing.
        maybe_publish_accepted_ack(
            client,
            ack_topic,
            room_id,
            device_id,
            current_safety,
            behavior,
            dropped_first_accepted_ack,
            record,
            &cmd,
        )
        .await;
        maybe_publish_completed_ack(
            client,
            ack_topic,
            room_id,
            device_id,
            current_safety,
            record,
            &cmd,
        )
        .await;
        return;
    }

    maybe_publish_accepted_ack(
        client,
        ack_topic,
        room_id,
        device_id,
        current_safety,
        behavior,
        dropped_first_accepted_ack,
        record,
        &cmd,
    )
    .await;

    // Simulate execution duration.
    tokio::time::sleep(Duration::from_millis(50)).await;

    record.completed = true;
    maybe_publish_completed_ack(
        client,
        ack_topic,
        room_id,
        device_id,
        current_safety,
        record,
        &cmd,
    )
    .await;

    // Update retained device state after completing the command.
    publish_state(
        client,
        state_topic,
        room_id,
        device_id,
        current_safety,
        serde_json::json!({
            "last_completed": {
                "command_id": cmd.command_id,
                "correlation_id": cmd.correlation_id,
                "sequence": cmd.sequence,
                "action": format!("{:?}", cmd.action),
            }
        }),
    )
    .await;
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

fn parse_bool_env(key: &str) -> Option<bool> {
    std::env::var(key)
        .ok()
        .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
}

async fn publish_rejected_ack(
    client: &rumqttc::AsyncClient,
    ack_topic: &str,
    room_id: &str,
    device_id: &str,
    safety: &SafetyState,
    cmd: &CommandEnvelope,
    reason_code: &str,
) {
    let rejected = CommandAck {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        device_id: device_id.to_string(),
        command_id: cmd.command_id,
        correlation_id: cmd.correlation_id,
        status: AckStatus::Rejected,
        reason_code: Some(reason_code.to_string()),
        safety_state: safety.clone(),
        observed_at_unix_ms: unix_ms_now(),
    };
    if let Ok(bytes) = serde_json::to_vec(&rejected) {
        if let Err(err) = client
            .publish(ack_topic, rumqttc::QoS::AtLeastOnce, false, bytes)
            .await
        {
            warn!(error = %err, "failed to publish REJECTED ack");
        }
    }
}

async fn maybe_publish_accepted_ack(
    client: &rumqttc::AsyncClient,
    ack_topic: &str,
    room_id: &str,
    device_id: &str,
    safety: &SafetyState,
    behavior: &SimBehavior,
    dropped_first_accepted_ack: &mut bool,
    record: &mut CommandRecord,
    cmd: &CommandEnvelope,
) {
    if record.accepted_sent {
        return;
    }

    if behavior.drop_first_accepted_ack && !*dropped_first_accepted_ack {
        *dropped_first_accepted_ack = true;
        warn!(command_id=%cmd.command_id, "SIM_DROP_FIRST_ACCEPTED_ACK: dropping ACCEPTED once");
        return;
    }

    let accepted = CommandAck {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        device_id: device_id.to_string(),
        command_id: cmd.command_id,
        correlation_id: cmd.correlation_id,
        status: AckStatus::Accepted,
        reason_code: None,
        safety_state: safety.clone(),
        observed_at_unix_ms: unix_ms_now(),
    };
    if let Ok(bytes) = serde_json::to_vec(&accepted) {
        if let Err(err) = client
            .publish(ack_topic, rumqttc::QoS::AtLeastOnce, false, bytes)
            .await
        {
            warn!(error = %err, "failed to publish ACCEPTED ack");
        } else {
            record.accepted_sent = true;
        }
    }
}

async fn maybe_publish_completed_ack(
    client: &rumqttc::AsyncClient,
    ack_topic: &str,
    room_id: &str,
    device_id: &str,
    safety: &SafetyState,
    record: &mut CommandRecord,
    cmd: &CommandEnvelope,
) {
    if record.completed_sent {
        return;
    }

    let completed = CommandAck {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        device_id: device_id.to_string(),
        command_id: cmd.command_id,
        correlation_id: cmd.correlation_id,
        status: AckStatus::Completed,
        reason_code: None,
        safety_state: safety.clone(),
        observed_at_unix_ms: unix_ms_now(),
    };
    if let Ok(bytes) = serde_json::to_vec(&completed) {
        if let Err(err) = client
            .publish(ack_topic, rumqttc::QoS::AtLeastOnce, false, bytes)
            .await
        {
            warn!(error = %err, "failed to publish COMPLETED ack");
        } else {
            record.completed_sent = true;
        }
    }
}

async fn publish_state(
    client: &rumqttc::AsyncClient,
    state_topic: &str,
    room_id: &str,
    device_id: &str,
    safety_state: &SafetyState,
    state: serde_json::Value,
) {
    let msg = DeviceState {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        device_id: device_id.to_string(),
        safety_state: safety_state.clone(),
        state,
        observed_at_unix_ms: unix_ms_now(),
    };
    match serde_json::to_vec(&msg) {
        Ok(bytes) => {
            if let Err(err) = client
                .publish(state_topic, rumqttc::QoS::AtLeastOnce, true, bytes)
                .await
            {
                warn!(error=%err, "failed to publish device state");
            }
        }
        Err(err) => warn!(error=%err, "failed to serialize device state"),
    }
}
