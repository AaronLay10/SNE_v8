use std::time::{Duration, Instant};

use anyhow::Context;
use serde::Deserialize;
use tokio::time::MissedTickBehavior;
use tracing::{info, warn};

#[derive(Debug, Deserialize)]
struct Config {
    room_id: String,
    mqtt_host: String,
    mqtt_port: u16,
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
            _ = tokio::signal::ctrl_c() => {
                warn!("shutdown requested (ctrl-c)");
                break;
            }
        }
    }

    info!("sentient-core stopped");
    Ok(())
}

