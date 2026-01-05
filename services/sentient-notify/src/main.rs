use std::time::Duration;

use sentient_protocol::{CoreFault, CoreStatus, SCHEMA_VERSION};
use tokio::sync::mpsc;
use tracing::{info, warn};

#[derive(Debug)]
struct Config {
    room_id: String,
    mqtt_host: String,
    mqtt_port: u16,
    mqtt_client_id: String,
    mqtt_username: Option<String>,
    mqtt_password: Option<String>,
    webhook_url: Option<String>,
    core_status_timeout_ms: u64,
    startup_grace_ms: u64,
}

impl Config {
    fn from_env() -> anyhow::Result<Self> {
        let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
        let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "mqtt".to_string());
        let mqtt_port: u16 = std::env::var("MQTT_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(1883);
        let mqtt_client_id = std::env::var("MQTT_CLIENT_ID")
            .unwrap_or_else(|_| format!("sentient-notify-{}", room_id));
        let mqtt_username = std::env::var("MQTT_USERNAME")
            .ok()
            .filter(|v| !v.is_empty());
        let mqtt_password = std::env::var("MQTT_PASSWORD")
            .ok()
            .filter(|v| !v.is_empty());
        let webhook_url = std::env::var("NOTIFY_WEBHOOK_URL")
            .ok()
            .filter(|v| !v.is_empty());

        let core_status_timeout_ms: u64 = std::env::var("NOTIFY_CORE_STATUS_TIMEOUT_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(5000);
        let startup_grace_ms: u64 = std::env::var("NOTIFY_STARTUP_GRACE_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(15000);

        Ok(Self {
            room_id,
            mqtt_host,
            mqtt_port,
            mqtt_client_id,
            mqtt_username,
            mqtt_password,
            webhook_url,
            core_status_timeout_ms,
            startup_grace_ms,
        })
    }
}

#[derive(Debug)]
enum MqttEvent {
    Connected,
    Disconnected(String),
    Publish(rumqttc::Publish),
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let config = Config::from_env()?;
    info!(?config, "sentient-notify starting");

    let http = reqwest::Client::builder()
        .timeout(Duration::from_secs(3))
        .build()
        .expect("reqwest client");

    let (client, mut events) = connect_mqtt(&config).await?;
    client
        .subscribe(
            format!("room/{}/core/fault", config.room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/core/status", config.room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/audio/fault", config.room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/core/device/+/fault", config.room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;

    let mut broker_outage_active = false;
    let mut core_outage_active = false;
    let mut last_core_status_seen_unix_ms: Option<u64> = None;
    let mut first_connected_unix_ms: Option<u64> = None;
    let mut ticker = tokio::time::interval(Duration::from_secs(1));
    ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
            _ = ticker.tick() => {
                if broker_outage_active {
                    continue;
                }
                let now = unix_ms_now();
                let Some(connected_at) = first_connected_unix_ms else {
                    continue;
                };
                if now.saturating_sub(connected_at) < config.startup_grace_ms {
                    continue;
                }

                let stale = last_core_status_seen_unix_ms
                    .map(|t| now.saturating_sub(t) > config.core_status_timeout_ms)
                    .unwrap_or(true);

                if stale && !core_outage_active {
                    core_outage_active = true;
                    warn!(
                        timeout_ms = config.core_status_timeout_ms,
                        "core status not updating; room should be paused and checked"
                    );
                    emit_webhook(
                        &config,
                        &http,
                        CoreFault{
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "CORE_UNHEALTHY".to_string(),
                            severity: "CRITICAL".to_string(),
                            message: "sentient-core status not updating; room should be paused and handled manually".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({
                                "core_status_timeout_ms": config.core_status_timeout_ms,
                                "last_core_status_seen_unix_ms": last_core_status_seen_unix_ms,
                            }),
                        }
                    ).await;
                } else if !stale && core_outage_active {
                    core_outage_active = false;
                    info!("core status updates restored");
                    emit_webhook(
                        &config,
                        &http,
                        CoreFault{
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "CORE_RESTORED".to_string(),
                            severity: "INFO".to_string(),
                            message: "sentient-core status updates restored".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({}),
                        }
                    ).await;
                }
            }
            maybe_ev = events.recv() => {
                let Some(ev) = maybe_ev else { break; };
                match ev {
                    MqttEvent::Publish(p) => {
                        if p.topic == format!("room/{}/core/status", config.room_id) {
                            match serde_json::from_slice::<CoreStatus>(&p.payload) {
                                Ok(_) => {
                                    last_core_status_seen_unix_ms = Some(unix_ms_now());
                                }
                                Err(err) => {
                                    warn!(error=%err, "invalid core status payload");
                                }
                            }
                        } else if p.topic == format!("room/{}/audio/fault", config.room_id) {
                            handle_core_fault(&config, &http, &p.payload).await;
                        } else if p.topic == format!("room/{}/core/fault", config.room_id)
                            || p.topic.starts_with(&format!("room/{}/core/device/", config.room_id))
                                && p.topic.ends_with("/fault")
                        {
                            handle_core_fault(&config, &http, &p.payload).await;
                        }
                    }
                    MqttEvent::Disconnected(err) => {
                        if broker_outage_active {
                            continue;
                        }
                        broker_outage_active = true;
                        core_outage_active = false;
                        warn!(error=%err, "mqtt broker disconnected (notify)");
                        emit_webhook(
                            &config,
                            &http,
                            CoreFault{
                                schema: SCHEMA_VERSION.to_string(),
                                room_id: config.room_id.clone(),
                                kind: "BROKER_UNREACHABLE".to_string(),
                                severity: "CRITICAL".to_string(),
                                message: "Room MQTT broker unreachable; room should be paused and handled manually".to_string(),
                                observed_at_unix_ms: unix_ms_now(),
                                details: serde_json::json!({"error": err}),
                            }
                        ).await;
                    }
                    MqttEvent::Connected => {
                        if first_connected_unix_ms.is_none() {
                            first_connected_unix_ms = Some(unix_ms_now());
                        }
                        if !broker_outage_active {
                            continue;
                        }
                        broker_outage_active = false;
                        info!("mqtt broker reconnected (notify)");
                        emit_webhook(
                            &config,
                            &http,
                            CoreFault{
                                schema: SCHEMA_VERSION.to_string(),
                                room_id: config.room_id.clone(),
                                kind: "BROKER_RESTORED".to_string(),
                                severity: "INFO".to_string(),
                                message: "Room MQTT broker connection restored".to_string(),
                                observed_at_unix_ms: unix_ms_now(),
                                details: serde_json::json!({}),
                            }
                        ).await;
                    }
                }
            }
        }
    }

    Ok(())
}

async fn handle_core_fault(config: &Config, http: &reqwest::Client, payload: &[u8]) {
    let fault: CoreFault = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error=%err, "invalid core fault payload");
            return;
        }
    };
    info!(kind=%fault.kind, severity=%fault.severity, message=%fault.message, "core fault");
    emit_webhook(config, http, fault).await;
}

async fn emit_webhook(config: &Config, http: &reqwest::Client, fault: CoreFault) {
    let Some(url) = config.webhook_url.as_deref() else {
        return;
    };
    if let Err(err) = http.post(url).json(&fault).send().await {
        warn!(error=%err, "failed to POST notify webhook");
    }
}

async fn connect_mqtt(
    config: &Config,
) -> anyhow::Result<(rumqttc::AsyncClient, mpsc::Receiver<MqttEvent>)> {
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
    let (tx, rx) = mpsc::channel::<MqttEvent>(1024);

    tokio::spawn(async move {
        let mut is_connected = false;
        loop {
            match eventloop.poll().await {
                Ok(rumqttc::Event::Incoming(rumqttc::Packet::Publish(p))) => {
                    if tx.send(MqttEvent::Publish(p)).await.is_err() {
                        break;
                    }
                }
                Ok(rumqttc::Event::Incoming(rumqttc::Packet::ConnAck(_))) => {
                    if !is_connected {
                        is_connected = true;
                        if tx.send(MqttEvent::Connected).await.is_err() {
                            break;
                        }
                    }
                }
                Ok(rumqttc::Event::Incoming(rumqttc::Packet::Disconnect)) => {
                    if is_connected {
                        is_connected = false;
                        let _ = tx
                            .send(MqttEvent::Disconnected("DISCONNECT".to_string()))
                            .await;
                    }
                }
                Ok(_) => {}
                Err(err) => {
                    if is_connected {
                        is_connected = false;
                        let _ = tx.send(MqttEvent::Disconnected(err.to_string())).await;
                    }
                    tokio::time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    Ok((client, rx))
}
