use std::{net::SocketAddr, time::Duration};

use rosc::decoder;
use tokio::net::UdpSocket;
use tracing::{info, warn};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let bind_ip = std::env::var("SCS_SIM_BIND_IP").unwrap_or_else(|_| "0.0.0.0".to_string());
    let port: u16 = std::env::var("SCS_SIM_OSC_PORT")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(53000);

    let addr: SocketAddr = format!("{}:{}", bind_ip, port).parse()?;
    let socket = UdpSocket::bind(addr).await?;
    info!(bind=%addr, "scs-sim listening for OSC packets");

    let mut buf = vec![0u8; 65535];

    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
            res = socket.recv_from(&mut buf) => {
                let (n, peer) = match res {
                    Ok(v) => v,
                    Err(err) => {
                        warn!(error=%err, "udp recv error");
                        tokio::time::sleep(Duration::from_millis(50)).await;
                        continue;
                    }
                };

                match decoder::decode_udp(&buf[..n]) {
                    Ok((_remain, packet)) => {
                        info!(from=%peer, bytes=n, packet=?packet, "received OSC");
                    }
                    Err(err) => {
                        warn!(from=%peer, bytes=n, error=%err, "failed to decode OSC");
                    }
                }
            }
        }
    }

    Ok(())
}
