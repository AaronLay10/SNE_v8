use std::time::{Duration, Instant};

use anyhow::Context;
use serde::Deserialize;
use sentient_protocol::{SafetyState, SafetyStateKind, SCHEMA_VERSION};
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

    loop {
        tokio::select! {
            _ = tick.tick() => {
                ticks = ticks.wrapping_add(1);

                // Placeholder for: graph evaluation + safety gating + MQTT command dispatch + telemetry.

                if last_report.elapsed() >= Duration::from_secs(5) {
                    let uptime = start.elapsed();
                    publish_core_heartbeat(&mqtt, &config.room_id, uptime).await;
                    info!(
                        room_id = %config.room_id,
                        ticks,
                        uptime_ms = uptime.as_millis() as u64,
                        dry_run = config.dry_run,
                        "scheduler heartbeat"
                    );
                    last_report = Instant::now();
                }
            }
            maybe_msg = mqtt.incoming.recv() => {
                if let Some(msg) = maybe_msg {
                    handle_incoming_mqtt(&config.room_id, msg).await;
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
    ];

    for t in topics {
        client.subscribe(t, rumqttc::QoS::AtLeastOnce).await?;
    }
    Ok(())
}

async fn handle_incoming_mqtt(room_id: &str, msg: rumqttc::Publish) {
    // Placeholder for: schema validation + state update + graph trigger evaluation.
    if msg.topic.starts_with(&format!("room/{}/device/", room_id)) {
        info!(topic = %msg.topic, bytes = msg.payload.len(), "mqtt device message");
    }
}
