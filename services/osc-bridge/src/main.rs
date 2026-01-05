use std::{net::SocketAddr, time::Duration};

use rosc::{encoder, OscMessage, OscPacket, OscType};
use sentient_protocol::{CoreFault, OscAckStatus, OscCue, SCHEMA_VERSION};
use tokio::net::UdpSocket;
use tracing::{info, warn};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
    let scs_host = std::env::var("SCS_HOST").unwrap_or_else(|_| "127.0.0.1".to_string());
    let scs_port: u16 = std::env::var("SCS_OSC_PORT")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(53000);

    let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "localhost".to_string());
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
    let mqtt_client_id = std::env::var("MQTT_CLIENT_ID")
        .unwrap_or_else(|_| format!("osc-bridge-{}-{}", room_id, uuid::Uuid::new_v4()));

    let osc_retries: u32 = std::env::var("OSC_RETRIES")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(5);
    let osc_retry_base_ms: u64 = std::env::var("OSC_RETRY_BASE_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(200);

    info!(
        room_id = %room_id,
        scs_host = %scs_host,
        scs_port,
        mqtt_host = %mqtt_host,
        mqtt_port,
        osc_retries,
        osc_retry_base_ms,
        "osc-bridge starting"
    );

    let target: SocketAddr = format!("{}:{}", scs_host, scs_port).parse()?;
    let socket = UdpSocket::bind("0.0.0.0:0").await?;

    let (mqtt, mut mqtt_eventloop) = connect_mqtt(
        mqtt_client_id,
        mqtt_host,
        mqtt_port,
        mqtt_username,
        mqtt_password,
    )
    .await?;
    let cue_topic = format!("room/{}/audio/cue", room_id);
    let ack_topic = format!("room/{}/audio/ack", room_id);
    let fault_topic = format!("room/{}/audio/fault", room_id);
    mqtt.subscribe(cue_topic.clone(), rumqttc::QoS::AtLeastOnce)
        .await?;

    info!(topic = %cue_topic, "subscribed for audio cues");

    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
            ev = mqtt_eventloop.poll() => {
                match ev {
                    Ok(rumqttc::Event::Incoming(rumqttc::Packet::Publish(p))) => {
                        if p.topic == cue_topic {
                            handle_cue_message(
                                &mqtt,
                                &ack_topic,
                                &fault_topic,
                                &socket,
                                target,
                                &p.payload,
                                osc_retries,
                                osc_retry_base_ms,
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

async fn connect_mqtt(
    client_id: String,
    host: String,
    port: u16,
    username: Option<String>,
    password: Option<String>,
) -> anyhow::Result<(rumqttc::AsyncClient, rumqttc::EventLoop)> {
    let mut options = rumqttc::MqttOptions::new(client_id, host, port);
    options.set_keep_alive(Duration::from_secs(5));
    if let (Some(user), Some(pass)) = (username.as_deref(), password.as_deref()) {
        options.set_credentials(user, pass);
    }
    Ok(rumqttc::AsyncClient::new(options, 200))
}

async fn handle_cue_message(
    mqtt: &rumqttc::AsyncClient,
    ack_topic: &str,
    fault_topic: &str,
    socket: &UdpSocket,
    target: SocketAddr,
    payload: &[u8],
    retries: u32,
    retry_base_ms: u64,
) {
    let cue: OscCue = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error = %err, "invalid OSC cue payload (json)");
            return;
        }
    };

    info!(
        room_id = %cue.room_id,
        cue_id = %cue.cue_id,
        correlation_id = %cue.correlation_id,
        address = %cue.address,
        args = cue.args.len(),
        "sending OSC cue"
    );

    let args: Vec<OscType> = cue
        .args
        .into_iter()
        .map(|a| match a {
            sentient_protocol::OscArg::Int(v) => OscType::Int(v),
            sentient_protocol::OscArg::Float(v) => OscType::Float(v),
            sentient_protocol::OscArg::String(v) => OscType::String(v),
            sentient_protocol::OscArg::Bool(v) => OscType::Bool(v),
        })
        .collect();

    let msg = OscMessage {
        addr: cue.address,
        args,
    };
    let packet = OscPacket::Message(msg);
    let buf = match encoder::encode(&packet) {
        Ok(v) => v,
        Err(err) => {
            warn!(error = %err, "failed to encode OSC packet");
            return;
        }
    };

    let mut attempt: u32 = 0;
    let mut last_err: Option<String>;
    loop {
        attempt = attempt.saturating_add(1);
        match socket.send_to(&buf, target).await {
            Ok(_) => {
                let _ = publish_ack(
                    mqtt,
                    ack_topic,
                    &cue.room_id,
                    &cue.cue_id,
                    cue.correlation_id,
                    OscAckStatus::Sent,
                    None,
                    attempt,
                )
                .await;
                break;
            }
            Err(err) => {
                last_err = Some(err.to_string());
                warn!(error=%err, attempt, "failed to send OSC packet");
                if attempt > retries {
                    let _ = publish_ack(
                        mqtt,
                        ack_topic,
                        &cue.room_id,
                        &cue.cue_id,
                        cue.correlation_id,
                        OscAckStatus::Failed,
                        last_err.clone(),
                        attempt,
                    )
                    .await;
                    let _ = publish_fault(
                        mqtt,
                        fault_topic,
                        &cue.room_id,
                        &cue.cue_id,
                        cue.correlation_id,
                        last_err.unwrap_or_else(|| "send failed".to_string()),
                        attempt,
                    )
                    .await;
                    break;
                }

                let delay = retry_base_ms.saturating_mul(1_u64 << (attempt.saturating_sub(1)));
                tokio::time::sleep(Duration::from_millis(delay.min(5000))).await;
            }
        }
    }
}

async fn publish_ack(
    mqtt: &rumqttc::AsyncClient,
    topic: &str,
    room_id: &str,
    cue_id: &str,
    correlation_id: uuid::Uuid,
    status: OscAckStatus,
    error: Option<String>,
    attempts: u32,
) -> anyhow::Result<()> {
    let payload = serde_json::json!({
        "schema": SCHEMA_VERSION,
        "room_id": room_id,
        "cue_id": cue_id,
        "correlation_id": correlation_id,
        "status": status,
        "attempts": attempts,
        "error": error,
        "observed_at_unix_ms": unix_ms_now(),
    });
    mqtt.publish(
        topic,
        rumqttc::QoS::AtLeastOnce,
        false,
        serde_json::to_vec(&payload)?,
    )
    .await?;
    Ok(())
}

async fn publish_fault(
    mqtt: &rumqttc::AsyncClient,
    topic: &str,
    room_id: &str,
    cue_id: &str,
    correlation_id: uuid::Uuid,
    error: String,
    attempts: u32,
) -> anyhow::Result<()> {
    let fault = CoreFault {
        schema: SCHEMA_VERSION.to_string(),
        room_id: room_id.to_string(),
        kind: "OSC_SEND_FAILED".to_string(),
        severity: "CRITICAL".to_string(),
        message: "osc-bridge failed to deliver OSC cue to SCS".to_string(),
        observed_at_unix_ms: unix_ms_now(),
        details: serde_json::json!({
            "cue_id": cue_id,
            "correlation_id": correlation_id,
            "attempts": attempts,
            "error": error,
        }),
    };
    mqtt.publish(
        topic,
        rumqttc::QoS::AtLeastOnce,
        true,
        serde_json::to_vec(&fault)?,
    )
    .await?;
    Ok(())
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}
