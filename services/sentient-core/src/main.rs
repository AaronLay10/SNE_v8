use std::time::{Duration, Instant};

use anyhow::Context;
use serde::Deserialize;
use sentient_protocol::{CommandAck, Heartbeat, Presence, PresenceStatus, SafetyState, SafetyStateKind, SCHEMA_VERSION};
use tokio::{sync::mpsc, time::MissedTickBehavior};
use tracing::{info, warn};
use uuid::Uuid;

#[derive(Debug, Deserialize)]
struct Config {
    room_id: String,
    mqtt_host: String,
    mqtt_port: u16,
    mqtt_client_id: String,
    mqtt_username: Option<String>,
    mqtt_password: Option<String>,
    database_url: String,
    dry_run: bool,
    tick_ms: u64,
    device_offline_ms: u64,
}

impl Config {
    fn from_env() -> anyhow::Result<Self> {
        let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
        let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "localhost".to_string());
        let mqtt_port = std::env::var("MQTT_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(1883);
        let mqtt_client_id =
            std::env::var("MQTT_CLIENT_ID").unwrap_or_else(|_| format!("sentient-core-{}-{}", room_id, Uuid::new_v4()));
        let mqtt_username = std::env::var("MQTT_USERNAME").ok().filter(|v| !v.is_empty());
        let mqtt_password = std::env::var("MQTT_PASSWORD").ok().filter(|v| !v.is_empty());
        let database_url =
            std::env::var("DATABASE_URL").unwrap_or_else(|_| "postgres://sentient@localhost/sentient".to_string());
        let dry_run = std::env::var("DRY_RUN")
            .ok()
            .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
            .unwrap_or(false);
        let tick_ms = std::env::var("TICK_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(1);
        let device_offline_ms = std::env::var("DEVICE_OFFLINE_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(3000);

        Ok(Self {
            room_id,
            mqtt_host,
            mqtt_port,
            mqtt_client_id,
            mqtt_username,
            mqtt_password,
            database_url,
            dry_run,
            tick_ms,
            device_offline_ms,
        })
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let config = Config::from_env().context("load config from env")?;
    info!(?config, "sentient-core starting");

    if config.tick_ms == 0 {
        anyhow::bail!("TICK_MS must be >= 1");
    }

    let mut mqtt = connect_mqtt(&config).await?;
    subscribe_default_topics(&mqtt.client, &config.room_id).await?;

    let start = Instant::now();
    let mut tick = tokio::time::interval(Duration::from_millis(config.tick_ms));
    tick.set_missed_tick_behavior(MissedTickBehavior::Skip);

    let mut ticks: u64 = 0;
    let mut last_report = Instant::now();
    let mut devices: std::collections::HashMap<String, DeviceStatus> = std::collections::HashMap::new();
    let mut last_device_sweep = Instant::now();

    loop {
        tokio::select! {
            _ = tick.tick() => {
                ticks = ticks.wrapping_add(1);

                // Placeholder for: graph evaluation + safety gating + MQTT command dispatch + telemetry.
                if last_device_sweep.elapsed() >= Duration::from_millis(500) {
                    sweep_device_offline(&config, &mut devices);
                    last_device_sweep = Instant::now();
                }

                if last_report.elapsed() >= Duration::from_secs(5) {
                    let uptime = start.elapsed();
                    publish_core_heartbeat(&mqtt, &config.room_id, uptime).await;
                    info!(
                        room_id = %config.room_id,
                        ticks,
                        uptime_ms = uptime.as_millis() as u64,
                        dry_run = config.dry_run,
                        device_count = devices.len(),
                        "scheduler heartbeat"
                    );
                    last_report = Instant::now();
                }
            }
            maybe_msg = mqtt.incoming.recv() => {
                if let Some(msg) = maybe_msg {
                    handle_incoming_mqtt(&config.room_id, msg, &mut devices).await;
                }
            }
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
        }
    }

    info!("sentient-core stopped");
    Ok(())
}

struct MqttHandle {
    client: rumqttc::AsyncClient,
    incoming: mpsc::Receiver<rumqttc::Publish>,
}

async fn connect_mqtt(config: &Config) -> anyhow::Result<MqttHandle> {
    let mut mqtt_config = rumqttc::MqttOptions::new(
        config.mqtt_client_id.clone(),
        config.mqtt_host.clone(),
        config.mqtt_port,
    );
    mqtt_config.set_keep_alive(Duration::from_secs(5));
    if let (Some(user), Some(pass)) = (&config.mqtt_username, &config.mqtt_password) {
        mqtt_config.set_credentials(user, pass);
    }

    let (client, mut eventloop) = rumqttc::AsyncClient::new(mqtt_config, 200);
    let (tx, rx) = mpsc::channel::<rumqttc::Publish>(1024);

    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(rumqttc::Event::Incoming(rumqttc::Packet::Publish(p))) => {
                    if tx.send(p).await.is_err() {
                        break;
                    }
                }
                Ok(_) => {}
                Err(err) => {
                    warn!(error = %err, "mqtt eventloop error");
                    tokio::time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    Ok(MqttHandle {
        client,
        incoming: rx,
    })
}

async fn publish_core_heartbeat(mqtt: &MqttHandle, room_id: &str, uptime: Duration) {
    let topic = format!("room/{}/core/heartbeat", room_id);
    let msg = serde_json::json!({
        "schema": SCHEMA_VERSION,
        "room_id": room_id,
        "uptime_ms": uptime.as_millis() as u64,
        "observed_at_unix_ms": unix_ms_now(),
        "safety_state": SafetyState{
            kind: SafetyStateKind::Safe,
            reason_code: None,
            latched: false,
        },
    });

    match serde_json::to_vec(&msg) {
        Ok(payload) => {
            if let Err(err) = mqtt.client.publish(topic, rumqttc::QoS::AtMostOnce, false, payload).await {
                warn!(error = %err, "failed to publish core heartbeat");
            }
        }
        Err(err) => warn!(error = %err, "failed to serialize core heartbeat"),
    }
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

async fn subscribe_default_topics(client: &rumqttc::AsyncClient, room_id: &str) -> anyhow::Result<()> {
    let topics = [
        format!("room/{}/device/+/heartbeat", room_id),
        format!("room/{}/device/+/ack", room_id),
        format!("room/{}/device/+/state", room_id),
        format!("room/{}/device/+/telemetry", room_id),
        format!("room/{}/device/+/presence", room_id),
    ];

    for t in topics {
        client.subscribe(t, rumqttc::QoS::AtLeastOnce).await?;
    }
    Ok(())
}

#[derive(Debug, Clone)]
struct DeviceStatus {
    last_heartbeat_at_unix_ms: Option<u64>,
    last_ack_at_unix_ms: Option<u64>,
    is_offline: bool,
}

#[derive(Debug)]
enum DeviceTopicKind {
    Heartbeat,
    Ack,
    State,
    Telemetry,
    Presence,
}

fn parse_device_topic(room_id: &str, topic: &str) -> Option<(String, DeviceTopicKind)> {
    // room/{room_id}/device/{device_id}/{kind}
    let prefix = format!("room/{}/device/", room_id);
    let rest = topic.strip_prefix(&prefix)?;
    let mut parts = rest.split('/');
    let device_id = parts.next()?.to_string();
    let kind = parts.next()?;
    if parts.next().is_some() {
        return None;
    }
    let kind = match kind {
        "heartbeat" => DeviceTopicKind::Heartbeat,
        "ack" => DeviceTopicKind::Ack,
        "state" => DeviceTopicKind::State,
        "telemetry" => DeviceTopicKind::Telemetry,
        "presence" => DeviceTopicKind::Presence,
        _ => return None,
    };
    Some((device_id, kind))
}

async fn handle_incoming_mqtt(
    room_id: &str,
    msg: rumqttc::Publish,
    devices: &mut std::collections::HashMap<String, DeviceStatus>,
) {
    let Some((device_id, kind)) = parse_device_topic(room_id, &msg.topic) else {
        return;
    };

    let status = devices.entry(device_id.clone()).or_insert(DeviceStatus {
        last_heartbeat_at_unix_ms: None,
        last_ack_at_unix_ms: None,
        is_offline: true,
    });

    match kind {
        DeviceTopicKind::Heartbeat => {
            match serde_json::from_slice::<Heartbeat>(&msg.payload) {
                Ok(hb) => {
                    status.last_heartbeat_at_unix_ms = Some(hb.observed_at_unix_ms);
                    if status.is_offline {
                        status.is_offline = false;
                        info!(device_id = %device_id, "device online");
                    }
                    info!(
                        device_id = %device_id,
                        uptime_ms = hb.uptime_ms,
                        fw = %hb.firmware_version,
                        safety = ?hb.safety_state.kind,
                        "device heartbeat"
                    );
                }
                Err(err) => warn!(device_id = %device_id, error = %err, "invalid heartbeat payload"),
            }
        }
        DeviceTopicKind::Ack => {
            match serde_json::from_slice::<CommandAck>(&msg.payload) {
                Ok(ack) => {
                    status.last_ack_at_unix_ms = Some(ack.observed_at_unix_ms);
                    info!(
                        device_id = %device_id,
                        status = ?ack.status,
                        command_id = %ack.command_id,
                        "device ack"
                    );
                }
                Err(err) => warn!(device_id = %device_id, error = %err, "invalid ack payload"),
            }
        }
        DeviceTopicKind::Presence => {
            match serde_json::from_slice::<Presence>(&msg.payload) {
                Ok(p) => {
                    match p.status {
                        PresenceStatus::Online => {
                            if status.is_offline {
                                status.is_offline = false;
                                info!(device_id = %device_id, "device online (presence)");
                            }
                        }
                        PresenceStatus::Offline => {
                            if !status.is_offline {
                                status.is_offline = true;
                                warn!(device_id = %device_id, "device offline (presence)");
                            }
                        }
                    }
                }
                Err(err) => warn!(device_id = %device_id, error = %err, "invalid presence payload"),
            }
        }
        DeviceTopicKind::State => {
            info!(device_id = %device_id, bytes = msg.payload.len(), "device state (raw)");
        }
        DeviceTopicKind::Telemetry => {
            info!(device_id = %device_id, bytes = msg.payload.len(), "device telemetry (raw)");
        }
    }
}

fn sweep_device_offline(config: &Config, devices: &mut std::collections::HashMap<String, DeviceStatus>) {
    let now = unix_ms_now();
    for (device_id, status) in devices.iter_mut() {
        let Some(last) = status.last_heartbeat_at_unix_ms else {
            continue;
        };
        let is_offline = now.saturating_sub(last) > config.device_offline_ms;
        if is_offline && !status.is_offline {
            status.is_offline = true;
            warn!(
                device_id = %device_id,
                last_heartbeat_at_unix_ms = last,
                device_offline_ms = config.device_offline_ms,
                "device offline"
            );
        }
    }
}
