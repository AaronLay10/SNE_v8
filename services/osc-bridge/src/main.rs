use std::time::Duration;

use tracing::info;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
    let scs_host = std::env::var("SCS_HOST").unwrap_or_else(|_| "127.0.0.1".to_string());
    let scs_port = std::env::var("SCS_OSC_PORT").unwrap_or_else(|_| "53000".to_string());

    info!(
        room_id = %room_id,
        scs_host = %scs_host,
        scs_port = %scs_port,
        "osc-bridge starting (placeholder)"
    );

    // Placeholder: subscribe to internal command stream, emit OSC with acks/retries.
    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => break,
            _ = tokio::time::sleep(Duration::from_secs(3600)) => {}
        }
    }

    Ok(())
}

