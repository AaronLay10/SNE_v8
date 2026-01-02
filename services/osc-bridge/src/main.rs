use std::{net::SocketAddr, time::Duration};

use rosc::{encoder, OscMessage, OscPacket, OscType};
use sentient_protocol::{OscArg, OscCue};
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
    let mqtt_client_id = std::env::var("MQTT_CLIENT_ID")
        .unwrap_or_else(|_| format!("osc-bridge-{}-{}", room_id, uuid::Uuid::new_v4()));

    info!(
        room_id = %room_id,
        scs_host = %scs_host,
        scs_port,
        mqtt_host = %mqtt_host,
        mqtt_port,
        "osc-bridge starting"
    );

    let target: SocketAddr = format!("{}:{}", scs_host, scs_port).parse()?;
    let socket = UdpSocket::bind("0.0.0.0:0").await?;

    let (mqtt, mut mqtt_eventloop) = connect_mqtt(mqtt_client_id, mqtt_host, mqtt_port).await?;
    let cue_topic = format!("room/{}/audio/cue", room_id);
    mqtt.subscribe(cue_topic.clone(), rumqttc::QoS::AtLeastOnce).await?;

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
                            handle_cue_message(&socket, target, &p.payload).await;
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
) -> anyhow::Result<(rumqttc::AsyncClient, rumqttc::EventLoop)> {
    let mut options = rumqttc::MqttOptions::new(client_id, host, port);
    options.set_keep_alive(Duration::from_secs(5));
    Ok(rumqttc::AsyncClient::new(options, 200))
}

async fn handle_cue_message(socket: &UdpSocket, target: SocketAddr, payload: &[u8]) {
    let cue: OscCue = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error = %err, "invalid OSC cue payload (json)");
            return;
        }
    };

    let args: Vec<OscType> = cue
        .args
        .into_iter()
        .map(|a| match a {
            OscArg::Int(v) => OscType::Int(v),
            OscArg::Float(v) => OscType::Float(v),
            OscArg::String(v) => OscType::String(v),
            OscArg::Bool(v) => OscType::Bool(v),
        })
        .collect();

    let msg = OscMessage {
        addr: cue.address,
        args: if args.is_empty() { None } else { Some(args) },
    };
    let packet = OscPacket::Message(msg);
    let buf = match encoder::encode(&packet) {
        Ok(v) => v,
        Err(err) => {
            warn!(error = %err, "failed to encode OSC packet");
            return;
        }
    };

    if let Err(err) = socket.send_to(&buf, target).await {
        warn!(error = %err, "failed to send OSC packet");
    }
}
