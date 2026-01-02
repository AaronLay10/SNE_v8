use std::time::Duration;

use sentient_protocol::{AckStatus, CommandAck, CommandEnvelope, Heartbeat, SafetyState, SafetyStateKind, SCHEMA_VERSION};
use tokio::time::MissedTickBehavior;
use tracing::{info, warn};
use uuid::Uuid;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
    let device_id = std::env::var("DEVICE_ID").unwrap_or_else(|_| "sim1".to_string());

    let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "mqtt".to_string());
    let mqtt_port: u16 = std::env::var("MQTT_PORT").ok().and_then(|v| v.parse().ok()).unwrap_or(1883);
    let mqtt_username = std::env::var("MQTT_USERNAME").ok().filter(|v| !v.is_empty());
    let mqtt_password = std::env::var("MQTT_PASSWORD").ok().filter(|v| !v.is_empty());

    let client_id = std::env::var("MQTT_CLIENT_ID")
        .unwrap_or_else(|_| format!("controller-sim-{}-{}-{}", room_id, device_id, Uuid::new_v4()));

    info!(
        room_id = %room_id,
        device_id = %device_id,
        mqtt_host = %mqtt_host,
        mqtt_port,
        "controller-sim starting"
    );

    let (client, mut eventloop) = {
        let mut options = rumqttc::MqttOptions::new(client_id, mqtt_host, mqtt_port);
        options.set_keep_alive(Duration::from_secs(5));
        if let (Some(user), Some(pass)) = (mqtt_username.as_deref(), mqtt_password.as_deref()) {
            options.set_credentials(user, pass);
        }
        rumqttc::AsyncClient::new(options, 200)
    };

    let cmd_topic = format!("room/{}/device/{}/cmd", room_id, device_id);
    client.subscribe(cmd_topic.clone(), rumqttc::QoS::AtLeastOnce).await?;

    let hb_topic = format!("room/{}/device/{}/heartbeat", room_id, device_id);
    let ack_topic = format!("room/{}/device/{}/ack", room_id, device_id);

    info!(%cmd_topic, %hb_topic, %ack_topic, "subscribed and ready");

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
                let msg = Heartbeat {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: room_id.clone(),
                    device_id: device_id.clone(),
                    uptime_ms: start.elapsed().as_millis() as u64,
                    firmware_version: "sim-0.1.0".to_string(),
                    safety_state: SafetyState { kind: SafetyStateKind::Safe, reason_code: None, latched: false },
                    last_error: None,
                    observed_at_unix_ms: unix_ms_now(),
                };
                if let Ok(payload) = serde_json::to_vec(&msg) {
                    if let Err(err) = client.publish(&hb_topic, rumqttc::QoS::AtLeastOnce, false, payload).await {
                        warn!(error = %err, "failed to publish heartbeat");
                    }
                }
            }
            ev = eventloop.poll() => {
                match ev {
                    Ok(rumqttc::Event::Incoming(rumqttc::Packet::Publish(p))) => {
                        if p.topic == cmd_topic {
                            handle_command(&client, &ack_topic, &room_id, &device_id, &p.payload).await;
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
    room_id: &str,
    device_id: &str,
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

    let accepted = CommandAck {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        device_id: device_id.to_string(),
        command_id: cmd.command_id,
        correlation_id: cmd.correlation_id,
        status: AckStatus::Accepted,
        reason_code: None,
        safety_state: SafetyState { kind: SafetyStateKind::Safe, reason_code: None, latched: false },
        observed_at_unix_ms: unix_ms_now(),
    };

    if let Ok(bytes) = serde_json::to_vec(&accepted) {
        if let Err(err) = client.publish(ack_topic, rumqttc::QoS::AtLeastOnce, false, bytes).await {
            warn!(error = %err, "failed to publish ACCEPTED ack");
        }
    }

    // Simulate execution duration.
    tokio::time::sleep(Duration::from_millis(50)).await;

    let completed = CommandAck {
        status: AckStatus::Completed,
        observed_at_unix_ms: unix_ms_now(),
        ..accepted
    };

    if let Ok(bytes) = serde_json::to_vec(&completed) {
        if let Err(err) = client.publish(ack_topic, rumqttc::QoS::AtLeastOnce, false, bytes).await {
            warn!(error = %err, "failed to publish COMPLETED ack");
        }
    }
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

