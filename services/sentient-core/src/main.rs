use std::time::{Duration, Instant};

use anyhow::Context;
use sentient_protocol::{
    sign_command_hmac_sha256, CommandAck, CommandAction, CommandEnvelope, CoreControlRequest,
    CoreDispatchRequest, CoreFault, CoreStatus, DeviceState, Heartbeat, Presence, PresenceStatus,
    SafetyClass, SafetyState, SafetyStateKind, CORE_CONTROL_OP_PAUSE_DISPATCH,
    CORE_CONTROL_OP_RELOAD_GRAPH, CORE_CONTROL_OP_RESET_SAFETY_LATCH,
    CORE_CONTROL_OP_RESUME_DISPATCH, CORE_CONTROL_OP_START_GRAPH, CORE_CONTROL_OP_STOP_GRAPH,
    SCHEMA_VERSION,
};
use serde::Deserialize;
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
    device_hmac_keys: std::collections::HashMap<String, Vec<u8>>,
    dev_test_command_device_id: Option<String>,
    dev_test_command_interval_ms: u64,
    dispatch_enabled: bool,
    critical_dispatch_armed: bool,
    dispatch_default_retries: u32,
    dispatch_ack_timeout_ms: u64,
    dispatch_complete_timeout_ms: u64,
    graph_path: Option<String>,
    graph_autostart: bool,
    db_enabled: bool,
    device_safety_class_json: Option<String>,
    core_control_token: Option<String>,
}

impl Config {
    fn from_env() -> anyhow::Result<Self> {
        let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
        let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "localhost".to_string());
        let mqtt_port = std::env::var("MQTT_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(1883);
        let mqtt_client_id = std::env::var("MQTT_CLIENT_ID")
            .unwrap_or_else(|_| format!("sentient-core-{}-{}", room_id, Uuid::new_v4()));
        let mqtt_username = std::env::var("MQTT_USERNAME")
            .ok()
            .filter(|v| !v.is_empty());
        let mqtt_password = std::env::var("MQTT_PASSWORD")
            .ok()
            .filter(|v| !v.is_empty());
        let database_url = std::env::var("DATABASE_URL")
            .unwrap_or_else(|_| "postgres://sentient@localhost/sentient".to_string());
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

        let device_hmac_keys =
            load_device_hmac_keys_from_env().context("load DEVICE_HMAC_KEYS_JSON")?;
        let dev_test_command_device_id = std::env::var("DEV_TEST_COMMAND_DEVICE_ID")
            .ok()
            .filter(|v| !v.is_empty());
        let dev_test_command_interval_ms = std::env::var("DEV_TEST_COMMAND_INTERVAL_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(10_000);

        let dispatch_enabled = std::env::var("CORE_DISPATCH_ENABLED")
            .ok()
            .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
            .unwrap_or(true);
        let critical_dispatch_armed = std::env::var("CORE_CRITICAL_ARMED")
            .ok()
            .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
            .unwrap_or(false);
        let dispatch_default_retries = std::env::var("CORE_DISPATCH_RETRIES")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(2);
        let dispatch_ack_timeout_ms = std::env::var("CORE_DISPATCH_ACK_TIMEOUT_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(2000);
        let dispatch_complete_timeout_ms = std::env::var("CORE_DISPATCH_COMPLETE_TIMEOUT_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(5000);

        let graph_path = std::env::var("CORE_GRAPH_PATH")
            .ok()
            .filter(|v| !v.trim().is_empty());
        let graph_autostart = std::env::var("CORE_GRAPH_AUTOSTART")
            .ok()
            .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
            .unwrap_or(false);

        let db_enabled = std::env::var("CORE_DB_ENABLED")
            .ok()
            .map(|v| matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"))
            .unwrap_or(true);

        let device_safety_class_json = std::env::var("DEVICE_SAFETY_CLASS_JSON")
            .ok()
            .filter(|v| !v.trim().is_empty());

        let core_control_token = std::env::var("CORE_CONTROL_TOKEN")
            .ok()
            .filter(|v| !v.trim().is_empty());

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
            device_hmac_keys,
            dev_test_command_device_id,
            dev_test_command_interval_ms,
            dispatch_enabled,
            critical_dispatch_armed,
            dispatch_default_retries,
            dispatch_ack_timeout_ms,
            dispatch_complete_timeout_ms,
            graph_path,
            graph_autostart,
            db_enabled,
            device_safety_class_json,
            core_control_token,
        })
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
enum NextRef {
    One(String),
    Many(Vec<String>),
}

impl NextRef {
    fn to_vec(&self) -> Vec<String> {
        match self {
            Self::One(v) => vec![v.clone()],
            Self::Many(v) => v.clone(),
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
enum StartRef {
    One(String),
    Many(Vec<String>),
}

impl StartRef {
    fn to_vec(&self) -> Vec<String> {
        match self {
            Self::One(v) => vec![v.clone()],
            Self::Many(v) => v.clone(),
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(tag = "kind", rename_all = "SCREAMING_SNAKE_CASE")]
enum GraphNode {
    Dispatch {
        device_id: String,
        action: CommandAction,
        #[serde(default)]
        parameters: serde_json::Value,
        #[serde(default = "default_safety_class_non_critical")]
        safety_class: SafetyClass,
        #[serde(default)]
        next: Option<NextRef>,
    },
    Delay {
        ms: u64,
        #[serde(default)]
        next: Option<NextRef>,
    },
    WaitStateEquals {
        device_id: String,
        /// JSON pointer into the last retained `DeviceState.state`, e.g. "/position" or "/sensor/open".
        pointer: String,
        equals: serde_json::Value,
        #[serde(default)]
        timeout_ms: Option<u64>,
        #[serde(default)]
        next: Option<NextRef>,
    },
    Noop {
        #[serde(default)]
        next: Option<NextRef>,
    },
}

#[derive(Debug, Clone, Deserialize)]
struct Graph {
    schema: String,
    room_id: String,
    start: StartRef,
    nodes: std::collections::HashMap<String, GraphNode>,
}

#[derive(Debug, Default)]
struct ActiveNodeState {
    node_id: String,
    entered_at: Option<Instant>,
    waiting_on_command_id: Option<Uuid>,
    next_after_wait: Option<NextRef>,
}

#[derive(Debug, Default)]
struct GraphRunner {
    graph: Option<Graph>,
    graph_version: Option<i64>,
    active_nodes: Vec<ActiveNodeState>,
}

fn default_safety_class_non_critical() -> SafetyClass {
    SafetyClass::NonCritical
}

impl GraphRunner {
    fn load_from_path(path: &str) -> anyhow::Result<Graph> {
        let raw = std::fs::read_to_string(path)
            .with_context(|| format!("read CORE_GRAPH_PATH={}", path))?;
        let g: Graph = serde_json::from_str(&raw).context("parse graph json")?;
        Ok(g)
    }

    fn maybe_autostart(&mut self, config: &Config) {
        if !config.graph_autostart {
            return;
        }
        if self.graph.is_none() || self.is_running() {
            return;
        }
        let starts = self.graph.as_ref().expect("graph").start.to_vec();
        self.active_nodes = starts
            .into_iter()
            .map(|node_id| ActiveNodeState {
                node_id,
                ..Default::default()
            })
            .collect();
    }

    fn is_running(&self) -> bool {
        !self.active_nodes.is_empty()
    }
}

async fn load_active_graph_from_db(
    database_url: &str,
    room_id: &str,
) -> anyhow::Result<Option<(Graph, i64)>> {
    let (client, connection) = tokio_postgres::connect(database_url, tokio_postgres::NoTls).await?;
    tokio::spawn(async move {
        if let Err(err) = connection.await {
            warn!(error=%err, "postgres connection error (graph loader)");
        }
    });

    let row = client
        .query_opt(
            "SELECT g.graph, g.version \
             FROM graph_active ga \
             JOIN graphs g ON g.room_id = ga.room_id AND g.version = ga.active_version \
             WHERE ga.room_id = $1",
            &[&room_id],
        )
        .await?;

    let Some(row) = row else {
        return Ok(None);
    };
    let graph_json: serde_json::Value = row.get(0);
    let version: i64 = row.get(1);
    let g: Graph = serde_json::from_value(graph_json).context("parse graph json from DB")?;
    Ok(Some((g, version)))
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

    let mut graph_runner = GraphRunner::default();
    if let Some(path) = config.graph_path.as_deref() {
        match GraphRunner::load_from_path(path) {
            Ok(g) => {
                if g.schema != SCHEMA_VERSION || g.room_id != config.room_id {
                    warn!(
                        graph_schema=%g.schema,
                        graph_room_id=%g.room_id,
                        "graph ignored: wrong schema/room_id"
                    );
                } else {
                    let starts = g.start.to_vec();
                    info!(path, nodes=g.nodes.len(), starts=?starts, "loaded graph");
                    graph_runner.graph = Some(g);
                }
            }
            Err(err) => warn!(error=%err, path, "failed to load graph"),
        }
    }

    if graph_runner.graph.is_none() && config.db_enabled {
        match load_active_graph_from_db(&config.database_url, &config.room_id).await {
            Ok(Some((g, version))) => {
                if g.schema != SCHEMA_VERSION || g.room_id != config.room_id {
                    warn!(
                        graph_schema=%g.schema,
                        graph_room_id=%g.room_id,
                        "graph ignored (db): wrong schema/room_id"
                    );
                } else {
                    info!(
                        nodes = g.nodes.len(),
                        version, "loaded active graph from DB"
                    );
                    graph_runner.graph = Some(g);
                    graph_runner.graph_version = Some(version);
                }
            }
            Ok(None) => {
                info!("no active graph in DB");
            }
            Err(err) => warn!(error=%err, "failed to load active graph from DB"),
        }
    }

    let mut mqtt = connect_mqtt(&config).await?;
    subscribe_default_topics(&mqtt.client, &config.room_id).await?;

    let db = if config.db_enabled {
        match DbWriter::connect(&config.database_url).await {
            Ok(w) => {
                info!("db writer connected");
                Some(w)
            }
            Err(err) => {
                warn!(error=%err, "db writer disabled (connect failed)");
                None
            }
        }
    } else {
        None
    };

    let mut runtime = RuntimeState::default();
    load_device_registry(&config, db.as_ref(), &mut runtime).await;
    runtime.room_safety = SafetyState {
        kind: SafetyStateKind::Safe,
        reason_code: None,
        latched: false,
    };

    let start = Instant::now();
    let mut tick = tokio::time::interval(Duration::from_millis(config.tick_ms));
    tick.set_missed_tick_behavior(MissedTickBehavior::Skip);

    let mut ticks: u64 = 0;
    let mut last_report = Instant::now();
    let mut last_status = Instant::now();
    let mut devices: std::collections::HashMap<String, DeviceStatus> =
        std::collections::HashMap::new();
    let mut last_device_sweep = Instant::now();
    let mut last_dev_test_cmd = Instant::now();
    let mut device_sequences: std::collections::HashMap<String, u64> =
        std::collections::HashMap::new();
    let mut pending: std::collections::HashMap<Uuid, PendingCommand> =
        std::collections::HashMap::new();
    let mut dispatch_tracker = DispatchTracker::default();

    loop {
        tokio::select! {
            _ = tick.tick() => {
                ticks = ticks.wrapping_add(1);

                graph_runner.maybe_autostart(&config);
                tick_graph_runner(
                    &config,
                    &mqtt.client,
                    &runtime,
                    db.as_ref(),
                    &mut graph_runner,
                    &devices,
                    &mut device_sequences,
                    &mut pending,
                    &mut dispatch_tracker,
                ).await;

                // Placeholder for: graph evaluation + safety gating + MQTT command dispatch + telemetry.
                maybe_publish_dev_test_command(
                    &config,
                    &mqtt.client,
                    &runtime,
                    &devices,
                    &mut device_sequences,
                    &mut last_dev_test_cmd,
                ).await;

                tick_pending_commands(&config, &mqtt.client, &runtime, db.as_ref(), &mut pending, &mut dispatch_tracker).await;

                if last_device_sweep.elapsed() >= Duration::from_millis(500) {
                    sweep_device_offline(&config, &mqtt.client, db.as_ref(), &mut devices).await;
                    last_device_sweep = Instant::now();
                }

                if last_report.elapsed() >= Duration::from_secs(5) {
                    let uptime = start.elapsed();
                    runtime.room_safety = compute_room_safety(
                        &devices,
                        runtime.safety_latched_since_unix_ms.is_some(),
                    );
                    publish_core_heartbeat(&mqtt, &config.room_id, uptime, &runtime.room_safety).await;
                    info!(
                        room_id = %config.room_id,
                        ticks,
                        uptime_ms = uptime.as_millis() as u64,
                        dry_run = config.dry_run,
                        device_count = devices.len(),
                        graph_running = graph_runner.is_running(),
                        "scheduler heartbeat"
                    );
                    last_report = Instant::now();
                }

                if last_status.elapsed() >= Duration::from_secs(1) {
                    let uptime = start.elapsed();
                    runtime.room_safety = compute_room_safety(
                        &devices,
                        runtime.safety_latched_since_unix_ms.is_some(),
                    );
                    publish_core_status(&mqtt.client, &config, &runtime, uptime, &graph_runner, &devices).await;
                    last_status = Instant::now();
                }
            }
            maybe_ev = mqtt.events.recv() => {
                if let Some(ev) = maybe_ev {
                    handle_mqtt_event(
                        &config,
                        &mqtt.client,
                        ev,
                        db.as_ref(),
                        &mut runtime,
                        &mut graph_runner,
                        &mut devices,
                        &mut device_sequences,
                        &mut pending,
                        &mut dispatch_tracker,
                    ).await;
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
    events: mpsc::Receiver<MqttEvent>,
}

#[derive(Debug)]
enum MqttEvent {
    Connected,
    Disconnected(String),
    Publish(rumqttc::Publish),
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
                        if tx
                            .send(MqttEvent::Disconnected("DISCONNECT".to_string()))
                            .await
                            .is_err()
                        {
                            break;
                        }
                    }
                }
                Ok(_) => {}
                Err(err) => {
                    if is_connected {
                        is_connected = false;
                        let _ = tx.send(MqttEvent::Disconnected(err.to_string())).await;
                    }
                    warn!(error = %err, "mqtt eventloop error");
                    tokio::time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });

    Ok(MqttHandle { client, events: rx })
}

#[derive(Debug)]
struct RuntimeState {
    dispatch_paused_reason: Option<String>,
    broker_outage_since_unix_ms: Option<u64>,
    device_registry: std::collections::HashMap<String, DeviceRegistryEntry>,
    room_safety: SafetyState,
    manual_pause: bool,
    safety_latched_since_unix_ms: Option<u64>,
}

impl Default for RuntimeState {
    fn default() -> Self {
        Self {
            dispatch_paused_reason: None,
            broker_outage_since_unix_ms: None,
            device_registry: std::collections::HashMap::new(),
            room_safety: SafetyState {
                kind: SafetyStateKind::Safe,
                reason_code: None,
                latched: false,
            },
            manual_pause: false,
            safety_latched_since_unix_ms: None,
        }
    }
}

impl RuntimeState {
    fn dispatch_is_paused(&self) -> bool {
        self.dispatch_paused_reason.is_some()
    }

    fn recompute_dispatch_pause_reason(&mut self) {
        self.dispatch_paused_reason = if self.broker_outage_since_unix_ms.is_some() {
            Some("BROKER_DOWN".to_string())
        } else if self.safety_latched_since_unix_ms.is_some() {
            Some("SAFETY_LATCHED".to_string())
        } else if self.manual_pause {
            Some("MANUAL_PAUSE".to_string())
        } else {
            None
        };
    }
}

async fn load_device_registry(config: &Config, db: Option<&DbWriter>, runtime: &mut RuntimeState) {
    let mut merged: std::collections::HashMap<String, DeviceRegistryEntry> =
        std::collections::HashMap::new();

    if let Some(raw) = config.device_safety_class_json.as_deref() {
        match serde_json::from_str::<std::collections::HashMap<String, String>>(raw) {
            Ok(map) => {
                for (device_id, cls) in map {
                    let cls = match cls.as_str() {
                        "CRITICAL" => SafetyClass::Critical,
                        "NON_CRITICAL" => SafetyClass::NonCritical,
                        other => {
                            warn!(device_id=%device_id, class=%other, "invalid DEVICE_SAFETY_CLASS_JSON value");
                            continue;
                        }
                    };
                    merged.insert(
                        device_id,
                        DeviceRegistryEntry {
                            safety_class: cls,
                            enabled: true,
                        },
                    );
                }
            }
            Err(err) => warn!(error=%err, "failed to parse DEVICE_SAFETY_CLASS_JSON"),
        }
    }

    if db.is_some() {
        match load_device_registry_from_db(&config.database_url).await {
            Ok(from_db) => {
                // DB overrides env, since it's the intended source of truth.
                merged.extend(from_db);
            }
            Err(err) => warn!(error=%err, "failed to load device registry from DB"),
        }
    }

    runtime.device_registry = merged;
    info!(
        device_count = runtime.device_registry.len(),
        "device registry loaded"
    );
}

async fn load_device_registry_from_db(
    database_url: &str,
) -> anyhow::Result<std::collections::HashMap<String, DeviceRegistryEntry>> {
    let (client, connection) = tokio_postgres::connect(database_url, tokio_postgres::NoTls)
        .await
        .context("connect postgres (registry)")?;
    tokio::spawn(async move {
        if let Err(err) = connection.await {
            warn!(error=%err, "postgres connection error (registry)");
        }
    });

    let mut out: std::collections::HashMap<String, DeviceRegistryEntry> =
        std::collections::HashMap::new();
    let rows = client
        .query("SELECT device_id, safety_class, enabled FROM devices", &[])
        .await
        .context("query devices")?;
    for row in rows {
        let device_id: String = row.get(0);
        let safety_class: String = row.get(1);
        let enabled: bool = row.get(2);
        let cls = match safety_class.as_str() {
            "CRITICAL" => SafetyClass::Critical,
            "NON_CRITICAL" => SafetyClass::NonCritical,
            other => {
                warn!(device_id=%device_id, safety_class=%other, "unknown safety_class in DB");
                continue;
            }
        };
        out.insert(
            device_id,
            DeviceRegistryEntry {
                safety_class: cls,
                enabled,
            },
        );
    }
    Ok(out)
}

#[derive(Debug, Clone)]
struct DeviceRegistryEntry {
    safety_class: SafetyClass,
    enabled: bool,
}

fn safety_class_rank(s: SafetyClass) -> u8 {
    match s {
        SafetyClass::NonCritical => 0,
        SafetyClass::Critical => 1,
    }
}

fn effective_safety_class(requested: SafetyClass, registry: SafetyClass) -> SafetyClass {
    if safety_class_rank(registry) > safety_class_rank(requested) {
        registry
    } else {
        requested
    }
}

async fn publish_core_heartbeat(
    mqtt: &MqttHandle,
    room_id: &str,
    uptime: Duration,
    safety: &SafetyState,
) {
    let topic = format!("room/{}/core/heartbeat", room_id);
    let msg = serde_json::json!({
        "schema": SCHEMA_VERSION,
        "room_id": room_id,
        "uptime_ms": uptime.as_millis() as u64,
        "observed_at_unix_ms": unix_ms_now(),
        "safety_state": safety,
    });

    match serde_json::to_vec(&msg) {
        Ok(payload) => {
            if let Err(err) = mqtt
                .client
                .publish(topic, rumqttc::QoS::AtMostOnce, false, payload)
                .await
            {
                warn!(error = %err, "failed to publish core heartbeat");
            }
        }
        Err(err) => warn!(error = %err, "failed to serialize core heartbeat"),
    }
}

fn compute_room_safety(
    devices: &std::collections::HashMap<String, DeviceStatus>,
    safety_latched: bool,
) -> SafetyState {
    let mut worst: SafetyStateKind = SafetyStateKind::Safe;
    for d in devices.values() {
        let Some(k) = d.last_reported_safety.as_ref().map(|s| s.kind) else {
            continue;
        };
        worst = match (worst, k) {
            (SafetyStateKind::EStop, _) => SafetyStateKind::EStop,
            (_, SafetyStateKind::EStop) => SafetyStateKind::EStop,
            (SafetyStateKind::Fault, _) => SafetyStateKind::Fault,
            (_, SafetyStateKind::Fault) => SafetyStateKind::Fault,
            (SafetyStateKind::Blocked, _) => SafetyStateKind::Blocked,
            (_, SafetyStateKind::Blocked) => SafetyStateKind::Blocked,
            (SafetyStateKind::Maintenance, _) => SafetyStateKind::Maintenance,
            (_, SafetyStateKind::Maintenance) => SafetyStateKind::Maintenance,
            (_, SafetyStateKind::Safe) => worst,
        };
    }

    let mut out = SafetyState {
        kind: worst,
        reason_code: None,
        latched: false,
    };

    if safety_latched {
        out.latched = true;
        out.kind = match out.kind {
            SafetyStateKind::EStop => SafetyStateKind::EStop,
            _ => SafetyStateKind::Fault,
        };
    }

    out
}

async fn maybe_latch_safety(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &mut RuntimeState,
    db: Option<&DbWriter>,
    device_id: &str,
    safety: &SafetyState,
    observed_at_unix_ms: u64,
) {
    if runtime.safety_latched_since_unix_ms.is_some() {
        return;
    }

    let should_latch =
        safety.latched || matches!(safety.kind, SafetyStateKind::Fault | SafetyStateKind::EStop);
    if !should_latch {
        return;
    }

    runtime.safety_latched_since_unix_ms = Some(observed_at_unix_ms);
    runtime.recompute_dispatch_pause_reason();

    let fault = CoreFault {
        schema: SCHEMA_VERSION.to_string(),
        room_id: config.room_id.clone(),
        kind: "SAFETY_LATCHED".to_string(),
        severity: "CRITICAL".to_string(),
        message: "Room safety latched; dispatch paused until manually reset".to_string(),
        observed_at_unix_ms,
        details: serde_json::json!({
            "device_id": device_id,
            "safety_kind": format!("{:?}", safety.kind),
            "reason_code": safety.reason_code,
            "latched": safety.latched,
        }),
    };
    publish_core_fault(client, &config.room_id, fault.clone()).await;
    if let Some(db) = db {
        if let Ok(v) = serde_json::to_value(&fault) {
            db.enqueue_json(
                &config.room_id,
                None,
                &format!("room/{}/core/fault", config.room_id),
                "CORE_FAULT",
                fault.observed_at_unix_ms,
                v,
            );
        }
    }
}

async fn publish_core_status(
    client: &rumqttc::AsyncClient,
    config: &Config,
    runtime: &RuntimeState,
    uptime: Duration,
    graph_runner: &GraphRunner,
    devices: &std::collections::HashMap<String, DeviceStatus>,
) {
    let topic = format!("room/{}/core/status", config.room_id);
    let offline_device_count = devices.values().filter(|d| d.is_offline).count() as u64;

    let graph_active_nodes: Vec<String> = graph_runner
        .active_nodes
        .iter()
        .map(|n| n.node_id.clone())
        .collect();
    let graph_active_node = graph_active_nodes.first().cloned();

    let msg = CoreStatus {
        schema: SCHEMA_VERSION.to_string(),
        room_id: config.room_id.clone(),
        uptime_ms: uptime.as_millis() as u64,
        tick_ms: config.tick_ms,
        dry_run: config.dry_run,
        dispatch_enabled: config.dispatch_enabled,
        dispatch_paused_reason: runtime.dispatch_paused_reason.clone(),
        broker_outage_since_unix_ms: runtime.broker_outage_since_unix_ms,
        safety_latched_since_unix_ms: runtime.safety_latched_since_unix_ms,
        room_safety: runtime.room_safety.clone(),
        device_count: devices.len() as u64,
        offline_device_count,
        graph_active_node,
        graph_active_nodes,
        graph_version: graph_runner.graph_version,
        observed_at_unix_ms: unix_ms_now(),
    };

    match serde_json::to_vec(&msg) {
        Ok(payload) => {
            if let Err(err) = client
                .publish(topic, rumqttc::QoS::AtLeastOnce, true, payload)
                .await
            {
                warn!(error=%err, "failed to publish core status");
            }
        }
        Err(err) => warn!(error=%err, "failed to serialize core status"),
    }
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

async fn subscribe_default_topics(
    client: &rumqttc::AsyncClient,
    room_id: &str,
) -> anyhow::Result<()> {
    // Align subscriptions with `docs/protocol/QOS_RETAIN.md`.
    client
        .subscribe(
            format!("room/{}/device/+/heartbeat", room_id),
            rumqttc::QoS::AtMostOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/device/+/telemetry", room_id),
            rumqttc::QoS::AtMostOnce,
        )
        .await?;

    client
        .subscribe(
            format!("room/{}/device/+/ack", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/device/+/presence", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/device/+/state", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;

    // MVP control plane: tools → core command dispatch.
    client
        .subscribe(
            format!("room/{}/core/dispatch", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;

    // Ops control plane: pause/resume dispatch (manual).
    client
        .subscribe(
            format!("room/{}/core/control", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;

    Ok(())
}

fn load_device_hmac_keys_from_env() -> anyhow::Result<std::collections::HashMap<String, Vec<u8>>> {
    let raw = std::env::var("DEVICE_HMAC_KEYS_JSON")
        .ok()
        .filter(|v| !v.trim().is_empty());
    let Some(raw) = raw else {
        return Ok(std::collections::HashMap::new());
    };

    let map: std::collections::HashMap<String, String> = serde_json::from_str(&raw)?;
    let mut out: std::collections::HashMap<String, Vec<u8>> = std::collections::HashMap::new();
    for (device_id, hex_key) in map {
        let key = hex::decode(hex_key.trim())
            .with_context(|| format!("invalid hex for device_id={}", device_id))?;
        out.insert(device_id, key);
    }
    Ok(out)
}

#[derive(Debug, Clone)]
struct DeviceStatus {
    last_heartbeat_at_unix_ms: Option<u64>,
    last_ack_at_unix_ms: Option<u64>,
    last_presence_at_unix_ms: Option<u64>,
    last_state_at_unix_ms: Option<u64>,
    last_state: Option<serde_json::Value>,
    presence: Option<PresenceStatus>,
    last_reported_safety: Option<SafetyState>,
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
    config: &Config,
    client: &rumqttc::AsyncClient,
    msg: rumqttc::Publish,
    db: Option<&DbWriter>,
    runtime: &mut RuntimeState,
    graph_runner: &mut GraphRunner,
    devices: &mut std::collections::HashMap<String, DeviceStatus>,
    device_sequences: &mut std::collections::HashMap<String, u64>,
    pending: &mut std::collections::HashMap<Uuid, PendingCommand>,
    dispatch_tracker: &mut DispatchTracker,
) {
    if msg.topic == format!("room/{}/core/dispatch", config.room_id) {
        handle_dispatch_request(
            config,
            client,
            runtime,
            db,
            &msg.payload,
            devices,
            device_sequences,
            pending,
            dispatch_tracker,
        )
        .await;
        return;
    }

    if msg.topic == format!("room/{}/core/control", config.room_id) {
        handle_core_control(
            config,
            client,
            runtime,
            graph_runner,
            db,
            devices,
            &msg.payload,
        )
        .await;
        return;
    }

    let Some((device_id, kind)) = parse_device_topic(&config.room_id, &msg.topic) else {
        return;
    };

    let status = devices.entry(device_id.clone()).or_insert(DeviceStatus {
        last_heartbeat_at_unix_ms: None,
        last_ack_at_unix_ms: None,
        last_presence_at_unix_ms: None,
        last_state_at_unix_ms: None,
        last_state: None,
        presence: None,
        last_reported_safety: None,
        is_offline: true,
    });

    match kind {
        DeviceTopicKind::Heartbeat => match serde_json::from_slice::<Heartbeat>(&msg.payload) {
            Ok(hb) => {
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&hb) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&device_id),
                            &msg.topic,
                            "HEARTBEAT",
                            hb.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                status.last_heartbeat_at_unix_ms = Some(hb.observed_at_unix_ms);
                status.last_reported_safety = Some(hb.safety_state.clone());
                maybe_latch_safety(
                    config,
                    client,
                    runtime,
                    db,
                    &device_id,
                    &hb.safety_state,
                    hb.observed_at_unix_ms,
                )
                .await;
                recompute_device_liveness(config, client, db, &device_id, status, "heartbeat")
                    .await;
                info!(
                    device_id = %device_id,
                    uptime_ms = hb.uptime_ms,
                    fw = %hb.firmware_version,
                    safety = ?hb.safety_state.kind,
                    "device heartbeat"
                );
            }
            Err(err) => warn!(device_id = %device_id, error = %err, "invalid heartbeat payload"),
        },
        DeviceTopicKind::Ack => {
            match serde_json::from_slice::<CommandAck>(&msg.payload) {
                Ok(ack) => {
                    if let Some(db) = db {
                        if let Ok(v) = serde_json::to_value(&ack) {
                            db.enqueue_json(
                                &config.room_id,
                                Some(&device_id),
                                &msg.topic,
                                "ACK",
                                ack.observed_at_unix_ms,
                                v,
                            );
                        }
                    }
                    status.last_ack_at_unix_ms = Some(ack.observed_at_unix_ms);
                    status.last_reported_safety = Some(ack.safety_state.clone());
                    maybe_latch_safety(
                        config,
                        client,
                        runtime,
                        db,
                        &device_id,
                        &ack.safety_state,
                        ack.observed_at_unix_ms,
                    )
                    .await;
                    if let Some(p) = pending.get_mut(&ack.command_id) {
                        p.last_update = Instant::now();
                        match ack.status {
                            sentient_protocol::AckStatus::Accepted => {
                                p.accepted = true;
                                p.accepted_at.get_or_insert(Instant::now());
                            }
                            sentient_protocol::AckStatus::Completed => {
                                p.completed = true;
                            }
                            sentient_protocol::AckStatus::Rejected => {
                                p.rejected = true;
                                p.reason_code = ack.reason_code.clone();
                            }
                        }

                        // If an ack arrives after we lost inflight tracking (e.g. core restart),
                        // backfill the correlation mapping so duplicate control-plane dispatches
                        // won't generate multiple physical actions.
                        dispatch_tracker.track_inflight(p.cmd.correlation_id, ack.command_id);
                    }
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
        DeviceTopicKind::Presence => match serde_json::from_slice::<Presence>(&msg.payload) {
            Ok(p) => {
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&p) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&device_id),
                            &msg.topic,
                            "PRESENCE",
                            p.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                status.last_presence_at_unix_ms = Some(p.observed_at_unix_ms);
                status.presence = Some(p.status);
                recompute_device_liveness(config, client, db, &device_id, status, "presence").await;
            }
            Err(err) => warn!(device_id = %device_id, error = %err, "invalid presence payload"),
        },
        DeviceTopicKind::State => match serde_json::from_slice::<DeviceState>(&msg.payload) {
            Ok(st) => {
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&st) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&device_id),
                            &msg.topic,
                            "STATE",
                            st.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                status.last_state_at_unix_ms = Some(st.observed_at_unix_ms);
                status.last_state = Some(st.state.clone());
                status.last_reported_safety = Some(st.safety_state.clone());
                maybe_latch_safety(
                    config,
                    client,
                    runtime,
                    db,
                    &device_id,
                    &st.safety_state,
                    st.observed_at_unix_ms,
                )
                .await;
                info!(
                    device_id = %device_id,
                    safety = ?st.safety_state.kind,
                    bytes = msg.payload.len(),
                    "device state"
                );

                if st.safety_state.kind != SafetyStateKind::Safe {
                    let fault = CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "DEVICE_SAFETY_STATE".to_string(),
                        severity: "WARN".to_string(),
                        message: "Device reported non-SAFE safety_state".to_string(),
                        observed_at_unix_ms: st.observed_at_unix_ms,
                        details: serde_json::json!({
                            "device_id": device_id,
                            "safety_state": format!("{:?}", st.safety_state.kind),
                            "reason_code": st.safety_state.reason_code,
                            "latched": st.safety_state.latched,
                        }),
                    };
                    publish_device_fault(client, &config.room_id, &device_id, &fault).await;
                    if let Some(db) = db {
                        if let Ok(v) = serde_json::to_value(&fault) {
                            db.enqueue_json(
                                &config.room_id,
                                Some(&device_id),
                                &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                                "DEVICE_FAULT",
                                fault.observed_at_unix_ms,
                                v,
                            );
                        }
                    }
                }

                publish_device_status(config, client, &device_id, status).await;
            }
            Err(err) => warn!(device_id = %device_id, error = %err, "invalid state payload"),
        },
        DeviceTopicKind::Telemetry => {
            if let Some(db) = db {
                if let Ok(v) = serde_json::from_slice::<serde_json::Value>(&msg.payload) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(&device_id),
                        &msg.topic,
                        "TELEMETRY",
                        unix_ms_now(),
                        v,
                    );
                }
            }
            info!(device_id = %device_id, bytes = msg.payload.len(), "device telemetry (raw)");
        }
    }
}

#[derive(Debug, Clone)]
struct PendingCommand {
    device_id: String,
    cmd: CommandEnvelope,
    published_at: Instant,
    last_update: Instant,
    retries_left: u32,
    ack_timeout_ms: u64,
    complete_timeout_ms: u64,
    accepted: bool,
    accepted_at: Option<Instant>,
    completed: bool,
    rejected: bool,
    reason_code: Option<String>,
}

#[derive(Debug, Default)]
struct DispatchTracker {
    // correlation_id -> command_id
    inflight: std::collections::HashMap<Uuid, Uuid>,
    // correlation_id -> completed_at
    recent: std::collections::HashMap<Uuid, Instant>,
}

impl DispatchTracker {
    fn track_inflight(&mut self, correlation_id: Uuid, command_id: Uuid) {
        self.inflight.insert(correlation_id, command_id);
    }

    fn inflight_command_id(&self, correlation_id: Uuid) -> Option<Uuid> {
        self.inflight.get(&correlation_id).copied()
    }

    fn mark_done(&mut self, correlation_id: Uuid) {
        self.inflight.remove(&correlation_id);
        self.recent.insert(correlation_id, Instant::now());
        // bound memory without requiring a separate sweep loop
        if self.recent.len() > 10_000 {
            self.recent.clear();
        }
    }

    fn is_recent(&self, correlation_id: Uuid, ttl: Duration) -> bool {
        self.recent
            .get(&correlation_id)
            .is_some_and(|t| t.elapsed() <= ttl)
    }

    fn sweep_recent(&mut self, ttl: Duration) {
        self.recent.retain(|_, t| t.elapsed() <= ttl);
    }
}

async fn handle_dispatch_request(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &RuntimeState,
    db: Option<&DbWriter>,
    payload: &[u8],
    devices: &std::collections::HashMap<String, DeviceStatus>,
    device_sequences: &mut std::collections::HashMap<String, u64>,
    pending: &mut std::collections::HashMap<Uuid, PendingCommand>,
    dispatch_tracker: &mut DispatchTracker,
) {
    let req: CoreDispatchRequest = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error=%err, "invalid core dispatch payload (json)");
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DISPATCH_REQUEST_INVALID".to_string(),
                severity: "WARN".to_string(),
                message: "Invalid core dispatch payload (JSON)".to_string(),
                observed_at_unix_ms: unix_ms_now(),
                details: serde_json::json!({
                    "error": err.to_string(),
                    "topic": format!("room/{}/core/dispatch", config.room_id),
                }),
            };
            publish_core_fault(client, &config.room_id, fault.clone()).await;
            if let Some(db) = db {
                db.enqueue_json(
                    &config.room_id,
                    None,
                    &format!("room/{}/core/fault", config.room_id),
                    "CORE_FAULT",
                    fault.observed_at_unix_ms,
                    serde_json::to_value(fault)
                        .unwrap_or_else(|_| serde_json::json!({"kind":"DISPATCH_REQUEST_INVALID"})),
                );
            }
            return;
        }
    };

    if req.schema != SCHEMA_VERSION || req.room_id != config.room_id {
        warn!(schema=%req.schema, room_id=%req.room_id, "ignoring dispatch request for wrong schema/room");
        return;
    }

    if runtime.dispatch_is_paused() {
        warn!(reason=?runtime.dispatch_paused_reason, device_id=%req.device_id, "ignoring core dispatch request: room dispatch is paused");
        let fault = CoreFault {
            schema: SCHEMA_VERSION.to_string(),
            room_id: config.room_id.clone(),
            kind: "DISPATCH_BLOCKED_PAUSED".to_string(),
            severity: "WARN".to_string(),
            message: "Dispatch blocked: room dispatch is paused".to_string(),
            observed_at_unix_ms: unix_ms_now(),
            details: serde_json::json!({
                "device_id": req.device_id,
                "reason": runtime.dispatch_paused_reason,
            }),
        };
        publish_core_fault(client, &config.room_id, fault.clone()).await;
        if let Some(db) = db {
            if let Ok(v) = serde_json::to_value(&fault) {
                db.enqueue_json(
                    &config.room_id,
                    None,
                    &format!("room/{}/core/fault", config.room_id),
                    "CORE_FAULT",
                    fault.observed_at_unix_ms,
                    v,
                );
            }
        }
        return;
    }
    if !config.dispatch_enabled {
        warn!(device_id=%req.device_id, "core dispatch is disabled (CORE_DISPATCH_ENABLED=false)");
        let fault = CoreFault {
            schema: SCHEMA_VERSION.to_string(),
            room_id: config.room_id.clone(),
            kind: "DISPATCH_BLOCKED_DISABLED".to_string(),
            severity: "WARN".to_string(),
            message: "Dispatch blocked: CORE_DISPATCH_ENABLED=false".to_string(),
            observed_at_unix_ms: unix_ms_now(),
            details: serde_json::json!({"device_id": req.device_id}),
        };
        publish_core_fault(client, &config.room_id, fault.clone()).await;
        if let Some(db) = db {
            if let Ok(v) = serde_json::to_value(&fault) {
                db.enqueue_json(
                    &config.room_id,
                    None,
                    &format!("room/{}/core/fault", config.room_id),
                    "CORE_FAULT",
                    fault.observed_at_unix_ms,
                    v,
                );
            }
        }
        return;
    }

    if config.dry_run {
        warn!(device_id=%req.device_id, "ignoring core dispatch request because DRY_RUN=true");
        let fault = CoreFault {
            schema: SCHEMA_VERSION.to_string(),
            room_id: config.room_id.clone(),
            kind: "DISPATCH_BLOCKED_DRY_RUN".to_string(),
            severity: "INFO".to_string(),
            message: "Dispatch ignored: DRY_RUN=true".to_string(),
            observed_at_unix_ms: unix_ms_now(),
            details: serde_json::json!({"device_id": req.device_id}),
        };
        publish_core_fault(client, &config.room_id, fault.clone()).await;
        if let Some(db) = db {
            if let Ok(v) = serde_json::to_value(&fault) {
                db.enqueue_json(
                    &config.room_id,
                    None,
                    &format!("room/{}/core/fault", config.room_id),
                    "CORE_FAULT",
                    fault.observed_at_unix_ms,
                    v,
                );
            }
        }
        return;
    }

    let device_id = req.device_id.clone();

    let reg = runtime.device_registry.get(&device_id).cloned();
    if let Some(reg) = reg.as_ref() {
        if !reg.enabled {
            warn!(device_id=%device_id, "dispatch blocked: device disabled in registry");
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DISPATCH_BLOCKED_DEVICE_DISABLED".to_string(),
                severity: "WARN".to_string(),
                message: "Dispatch blocked: device disabled".to_string(),
                observed_at_unix_ms: unix_ms_now(),
                details: serde_json::json!({"device_id": device_id}),
            };
            publish_device_fault(client, &config.room_id, &device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(&device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
            return;
        }
    }

    let req_safety_class = req.safety_class;
    let effective_req_safety_class = effective_safety_class(
        req_safety_class,
        reg.as_ref()
            .map(|r| r.safety_class)
            .unwrap_or(SafetyClass::NonCritical),
    );

    if let Some(status) = devices.get(&device_id) {
        if status.is_offline {
            warn!(device_id=%device_id, "ignoring dispatch request: device offline");
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DISPATCH_BLOCKED_DEVICE_OFFLINE".to_string(),
                severity: "WARN".to_string(),
                message: "Dispatch blocked: device offline".to_string(),
                observed_at_unix_ms: unix_ms_now(),
                details: serde_json::json!({"device_id": device_id}),
            };
            publish_device_fault(client, &config.room_id, &device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(&device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
            return;
        }
        if effective_req_safety_class == SafetyClass::Critical {
            if !config.critical_dispatch_armed {
                warn!(device_id=%device_id, "blocking CRITICAL dispatch (CORE_CRITICAL_ARMED=false)");
                let fault = CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "DISPATCH_BLOCKED_CRITICAL_NOT_ARMED".to_string(),
                    severity: "WARN".to_string(),
                    message: "Dispatch blocked: CRITICAL requires CORE_CRITICAL_ARMED=true"
                        .to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({"device_id": device_id}),
                };
                publish_device_fault(client, &config.room_id, &device_id, &fault).await;
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&fault) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&device_id),
                            &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                            "DEVICE_FAULT",
                            fault.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                return;
            }
            if status
                .last_reported_safety
                .as_ref()
                .is_none_or(|s| s.kind != SafetyStateKind::Safe || s.latched)
            {
                warn!(
                    device_id=%device_id,
                    reported_safety=?status.last_reported_safety.as_ref().map(|s| s.kind),
                    "blocking CRITICAL dispatch (device not SAFE)"
                );
                let fault = CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "DISPATCH_BLOCKED_DEVICE_NOT_SAFE".to_string(),
                    severity: "WARN".to_string(),
                    message: "Dispatch blocked: device not SAFE".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({
                        "device_id": device_id,
                        "reported_safety": status.last_reported_safety.as_ref().map(|s| format!("{:?}", s.kind)),
                        "reported_latched": status.last_reported_safety.as_ref().map(|s| s.latched).unwrap_or(false),
                    }),
                };
                publish_device_fault(client, &config.room_id, &device_id, &fault).await;
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&fault) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&device_id),
                            &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                            "DEVICE_FAULT",
                            fault.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                return;
            }
        }
    }

    let Some(key) = config.device_hmac_keys.get(&device_id) else {
        warn!(device_id=%device_id, "ignoring dispatch request: missing device HMAC key");
        let fault = CoreFault {
            schema: SCHEMA_VERSION.to_string(),
            room_id: config.room_id.clone(),
            kind: "DISPATCH_BLOCKED_MISSING_DEVICE_KEY".to_string(),
            severity: "WARN".to_string(),
            message: "Dispatch blocked: missing device HMAC key on core".to_string(),
            observed_at_unix_ms: unix_ms_now(),
            details: serde_json::json!({"device_id": device_id}),
        };
        publish_device_fault(client, &config.room_id, &device_id, &fault).await;
        if let Some(db) = db {
            if let Ok(v) = serde_json::to_value(&fault) {
                db.enqueue_json(
                    &config.room_id,
                    Some(&device_id),
                    &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                    "DEVICE_FAULT",
                    fault.observed_at_unix_ms,
                    v,
                );
            }
        }
        return;
    };

    let next_seq = device_sequences
        .get(&device_id)
        .copied()
        .unwrap_or(0)
        .wrapping_add(1);
    device_sequences.insert(device_id.clone(), next_seq);

    let correlation_id = req.correlation_id.unwrap_or_else(Uuid::new_v4);

    // Control-plane idempotency: if tools retry the dispatch request with the same correlation_id,
    // do not generate new command_ids while the original is inflight (or recently completed).
    dispatch_tracker.sweep_recent(Duration::from_secs(60 * 10));
    if dispatch_tracker.is_recent(correlation_id, Duration::from_secs(60 * 10)) {
        warn!(device_id=%device_id, correlation_id=%correlation_id, "duplicate dispatch request (recently completed)");
        return;
    }
    if let Some(existing_cmd_id) = dispatch_tracker.inflight_command_id(correlation_id) {
        if pending.contains_key(&existing_cmd_id) {
            warn!(
                device_id=%device_id,
                correlation_id=%correlation_id,
                command_id=%existing_cmd_id,
                "duplicate dispatch request (already inflight)"
            );
            return;
        }
        // stale inflight mapping (e.g. removed by timeout); allow a fresh dispatch
        dispatch_tracker.inflight.remove(&correlation_id);
    }

    let mut cmd = CommandEnvelope {
        schema: SCHEMA_VERSION.to_string(),
        room_id: config.room_id.clone(),
        device_id: device_id.clone(),
        command_id: Uuid::new_v4(),
        correlation_id,
        sequence: next_seq,
        issued_at_unix_ms: unix_ms_now(),
        action: req.action,
        parameters: req.parameters,
        safety_class: effective_req_safety_class,
        auth: None,
    };

    if let Err(err) = sign_command_hmac_sha256(&mut cmd, key, None) {
        warn!(device_id=%device_id, error=%err, "failed to sign dispatch command");
        return;
    }

    let retries_left = req.retries.unwrap_or(config.dispatch_default_retries);
    let ack_timeout_ms = req.ack_timeout_ms.unwrap_or(config.dispatch_ack_timeout_ms);
    let complete_timeout_ms = req
        .complete_timeout_ms
        .unwrap_or(config.dispatch_complete_timeout_ms);

    if publish_device_command(client, &config.room_id, &device_id, &cmd).await {
        if let Some(db) = db {
            if let Ok(v) = serde_json::to_value(&cmd) {
                db.enqueue_json(
                    &config.room_id,
                    Some(&device_id),
                    &format!("room/{}/device/{}/cmd", config.room_id, device_id),
                    "CMD",
                    cmd.issued_at_unix_ms,
                    v,
                );
            }
        }
        dispatch_tracker.track_inflight(correlation_id, cmd.command_id);
        pending.insert(
            cmd.command_id,
            PendingCommand {
                device_id,
                cmd,
                published_at: Instant::now(),
                last_update: Instant::now(),
                retries_left,
                ack_timeout_ms,
                complete_timeout_ms,
                accepted: false,
                accepted_at: None,
                completed: false,
                rejected: false,
                reason_code: None,
            },
        );
    }
}

async fn handle_core_control(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &mut RuntimeState,
    graph_runner: &mut GraphRunner,
    db: Option<&DbWriter>,
    devices: &std::collections::HashMap<String, DeviceStatus>,
    payload: &[u8],
) {
    let req: CoreControlRequest = match serde_json::from_slice(payload) {
        Ok(v) => v,
        Err(err) => {
            warn!(error=%err, "invalid core control payload (json)");
            return;
        }
    };

    if req.schema != SCHEMA_VERSION || req.room_id != config.room_id {
        warn!(schema=%req.schema, room_id=%req.room_id, "ignoring control request for wrong schema/room");
        return;
    }

    if let Some(expected) = config.core_control_token.as_deref() {
        let provided = req
            .parameters
            .get("token")
            .and_then(|v| v.as_str())
            .unwrap_or("");
        if provided != expected {
            warn!(op=%req.op, "unauthorized core control request (token mismatch)");
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "CONTROL_UNAUTHORIZED".to_string(),
                    severity: "WARN".to_string(),
                    message: "Core control request denied (token mismatch)".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({"op": req.op}),
                },
            )
            .await;
            return;
        }
    }

    if let Some(db) = db {
        if let Ok(v) = serde_json::to_value(&req) {
            db.enqueue_json(
                &config.room_id,
                None,
                &format!("room/{}/core/control", config.room_id),
                "CORE_CONTROL",
                req.requested_at_unix_ms,
                v,
            );
        }
    }

    match req.op.as_str() {
        CORE_CONTROL_OP_PAUSE_DISPATCH => {
            runtime.manual_pause = true;
            runtime.recompute_dispatch_pause_reason();
            warn!("dispatch paused via core control request");
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "DISPATCH_PAUSED".to_string(),
                    severity: "WARN".to_string(),
                    message: "Dispatch paused by operator".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({}),
                },
            )
            .await;
        }
        CORE_CONTROL_OP_RESUME_DISPATCH => {
            runtime.broker_outage_since_unix_ms = None;
            runtime.manual_pause = false;
            runtime.recompute_dispatch_pause_reason();
            warn!("dispatch resumed via core control request");
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "DISPATCH_RESUMED".to_string(),
                    severity: "INFO".to_string(),
                    message: "Dispatch resumed by operator".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({}),
                },
            )
            .await;
        }
        CORE_CONTROL_OP_RESET_SAFETY_LATCH => {
            if runtime.safety_latched_since_unix_ms.is_none() {
                warn!("safety reset requested but no latch is active");
                return;
            }

            let mut blockers: Vec<String> = Vec::new();
            for (device_id, st) in devices {
                if st.is_offline {
                    blockers.push(format!("{device_id}:OFFLINE"));
                    continue;
                }
                let Some(safety) = st.last_reported_safety.as_ref() else {
                    blockers.push(format!("{device_id}:UNKNOWN"));
                    continue;
                };
                if safety.latched || safety.kind != SafetyStateKind::Safe {
                    blockers.push(format!("{device_id}:{:?}", safety.kind));
                }
            }

            if !blockers.is_empty() {
                blockers.sort();
                warn!(blockers=?blockers, "safety reset denied (devices not SAFE/offline)");
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "SAFETY_RESET_DENIED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Safety reset denied: devices not SAFE/offline".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({ "blockers": blockers }),
                    },
                )
                .await;
                return;
            }

            runtime.safety_latched_since_unix_ms = None;
            runtime.recompute_dispatch_pause_reason();
            runtime.room_safety = compute_room_safety(devices, false);
            warn!("safety latch reset via core control request");
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "SAFETY_LATCH_RESET".to_string(),
                    severity: "INFO".to_string(),
                    message: "Safety latch reset by operator".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({}),
                },
            )
            .await;
        }
        CORE_CONTROL_OP_START_GRAPH => {
            if runtime.dispatch_is_paused() {
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_START_DENIED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph start denied: dispatch is paused".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({}),
                    },
                )
                .await;
                return;
            }
            let Some(graph) = graph_runner.graph.as_ref() else {
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_START_DENIED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph start denied: no graph loaded".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({}),
                    },
                )
                .await;
                return;
            };
            if graph_runner.is_running() {
                return;
            }
            let starts = graph.start.to_vec();
            graph_runner.active_nodes = starts
                .into_iter()
                .map(|node_id| ActiveNodeState {
                    node_id,
                    ..Default::default()
                })
                .collect();
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "GRAPH_STARTED".to_string(),
                    severity: "INFO".to_string(),
                    message: "Graph started".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({
                        "version": graph_runner.graph_version,
                        "starts": graph_runner.active_nodes.iter().map(|n| n.node_id.clone()).collect::<Vec<_>>(),
                    }),
                },
            )
            .await;
        }
        CORE_CONTROL_OP_STOP_GRAPH => {
            if !graph_runner.is_running() {
                return;
            }
            graph_runner.active_nodes.clear();
            publish_core_fault(
                client,
                &config.room_id,
                CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "GRAPH_STOPPED".to_string(),
                    severity: "WARN".to_string(),
                    message: "Graph stopped by operator".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({ "version": graph_runner.graph_version }),
                },
            )
            .await;
        }
        CORE_CONTROL_OP_RELOAD_GRAPH => {
            if graph_runner.is_running() {
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_RELOAD_DENIED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph reload denied: graph is running".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({}),
                    },
                )
                .await;
                return;
            }
            if !runtime.dispatch_is_paused() {
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_RELOAD_DENIED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph reload denied: pause dispatch first".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({}),
                    },
                )
                .await;
                return;
            }
            if !config.db_enabled {
                publish_core_fault(
                    client,
                    &config.room_id,
                    CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_RELOAD_FAILED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph reload failed: CORE_DB_ENABLED is false".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({}),
                    },
                )
                .await;
                return;
            }
            match load_active_graph_from_db(&config.database_url, &config.room_id).await {
                Ok(Some((g, version))) => {
                    if g.schema != SCHEMA_VERSION || g.room_id != config.room_id {
                        publish_core_fault(
                            client,
                            &config.room_id,
                            CoreFault {
                                schema: SCHEMA_VERSION.to_string(),
                                room_id: config.room_id.clone(),
                                kind: "GRAPH_RELOAD_FAILED".to_string(),
                                severity: "WARN".to_string(),
                                message: "Graph reload failed: wrong schema/room_id".to_string(),
                                observed_at_unix_ms: unix_ms_now(),
                                details: serde_json::json!({
                                    "graph_schema": g.schema,
                                    "graph_room_id": g.room_id,
                                }),
                            },
                        )
                        .await;
                        return;
                    }
                    graph_runner.graph = Some(g);
                    graph_runner.graph_version = Some(version);
                    publish_core_fault(
                        client,
                        &config.room_id,
                        CoreFault {
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "GRAPH_RELOADED".to_string(),
                            severity: "INFO".to_string(),
                            message: "Graph reloaded from DB".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({ "version": version }),
                        },
                    )
                    .await;
                }
                Ok(None) => {
                    publish_core_fault(
                        client,
                        &config.room_id,
                        CoreFault {
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "GRAPH_RELOAD_FAILED".to_string(),
                            severity: "WARN".to_string(),
                            message: "Graph reload failed: no active graph in DB".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({}),
                        },
                    )
                    .await;
                }
                Err(err) => {
                    warn!(error=%err, "graph reload failed");
                    publish_core_fault(
                        client,
                        &config.room_id,
                        CoreFault {
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "GRAPH_RELOAD_FAILED".to_string(),
                            severity: "WARN".to_string(),
                            message: "Graph reload failed (db error)".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({ "error": err.to_string() }),
                        },
                    )
                    .await;
                }
            }
        }
        other => {
            warn!(op=%other, "unknown core control op");
        }
    }
}

async fn publish_core_fault(client: &rumqttc::AsyncClient, room_id: &str, fault: CoreFault) {
    let topic = format!("room/{}/core/fault", room_id);
    match serde_json::to_vec(&fault) {
        Ok(payload) => {
            if let Err(err) = client
                .publish(topic, rumqttc::QoS::AtLeastOnce, true, payload)
                .await
            {
                warn!(error=%err, "failed to publish core fault");
            }
        }
        Err(err) => warn!(error=%err, "failed to serialize core fault"),
    }
}

async fn publish_device_fault(
    client: &rumqttc::AsyncClient,
    room_id: &str,
    device_id: &str,
    fault: &CoreFault,
) {
    let topic = format!("room/{}/core/device/{}/fault", room_id, device_id);
    match serde_json::to_vec(fault) {
        Ok(payload) => {
            if let Err(err) = client
                .publish(topic, rumqttc::QoS::AtLeastOnce, true, payload)
                .await
            {
                warn!(error=%err, "failed to publish device fault");
            }
        }
        Err(err) => warn!(error=%err, "failed to serialize device fault"),
    }
}

async fn tick_graph_runner(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &RuntimeState,
    db: Option<&DbWriter>,
    runner: &mut GraphRunner,
    devices: &std::collections::HashMap<String, DeviceStatus>,
    device_sequences: &mut std::collections::HashMap<String, u64>,
    pending: &mut std::collections::HashMap<Uuid, PendingCommand>,
    dispatch_tracker: &mut DispatchTracker,
) {
    if runtime.dispatch_is_paused() || !config.dispatch_enabled || config.dry_run {
        return;
    }
    let Some(graph) = runner.graph.as_ref() else {
        return;
    };
    if runner.active_nodes.is_empty() {
        return;
    }

    let mut next_active: Vec<ActiveNodeState> = Vec::new();
    let mut transitions_this_tick: usize = 0;
    let now = Instant::now();

    let push_next = |out: &mut Vec<ActiveNodeState>, next: Option<&NextRef>| {
        let Some(next) = next else {
            return;
        };
        for node_id in next.to_vec() {
            out.push(ActiveNodeState {
                node_id,
                ..Default::default()
            });
        }
    };

    for mut state in std::mem::take(&mut runner.active_nodes) {
        if transitions_this_tick >= 128 {
            next_active.push(state);
            continue;
        }

        if let Some(cmd_id) = state.waiting_on_command_id {
            if pending.contains_key(&cmd_id) {
                next_active.push(state);
                continue;
            }
            // Command completed/cleared; advance into next nodes.
            let next = state.next_after_wait.take();
            state.waiting_on_command_id = None;
            transitions_this_tick += 1;
            push_next(&mut next_active, next.as_ref());
            continue;
        }

        let Some(node) = graph.nodes.get(&state.node_id) else {
            warn!(node_id=%state.node_id, "graph error: unknown node id");
            runner.active_nodes.clear();
            return;
        };

        match node {
            GraphNode::Noop { next } => {
                transitions_this_tick += 1;
                push_next(&mut next_active, next.as_ref());
            }
            GraphNode::Delay { ms, next } => {
                let entered = state.entered_at.get_or_insert(now);
                if entered.elapsed() >= Duration::from_millis(*ms) {
                    transitions_this_tick += 1;
                    push_next(&mut next_active, next.as_ref());
                } else {
                    next_active.push(state);
                }
            }
            GraphNode::WaitStateEquals {
                device_id,
                pointer,
                equals,
                timeout_ms,
                next,
            } => {
                let entered = state.entered_at.get_or_insert(now);
                if let Some(timeout_ms) = timeout_ms {
                    if entered.elapsed() >= Duration::from_millis(*timeout_ms) {
                        let fault = CoreFault {
                            schema: SCHEMA_VERSION.to_string(),
                            room_id: config.room_id.clone(),
                            kind: "GRAPH_TIMEOUT".to_string(),
                            severity: "WARN".to_string(),
                            message: "Graph node timed out waiting for device state".to_string(),
                            observed_at_unix_ms: unix_ms_now(),
                            details: serde_json::json!({
                                "node_id": state.node_id,
                                "device_id": device_id,
                                "pointer": pointer,
                                "equals": equals,
                                "timeout_ms": timeout_ms,
                            }),
                        };
                        publish_core_fault(client, &config.room_id, fault.clone()).await;
                        if let Some(db) = db {
                            if let Ok(v) = serde_json::to_value(&fault) {
                                db.enqueue_json(
                                    &config.room_id,
                                    None,
                                    &format!("room/{}/core/fault", config.room_id),
                                    "CORE_FAULT",
                                    fault.observed_at_unix_ms,
                                    v,
                                );
                            }
                        }
                        runner.active_nodes.clear();
                        return;
                    }
                }

                let Some(st) = devices.get(device_id).and_then(|d| d.last_state.as_ref()) else {
                    next_active.push(state);
                    continue;
                };
                let Some(actual) = st.pointer(pointer) else {
                    next_active.push(state);
                    continue;
                };
                if actual == equals {
                    transitions_this_tick += 1;
                    push_next(&mut next_active, next.as_ref());
                } else {
                    next_active.push(state);
                }
            }
            GraphNode::Dispatch {
                device_id,
                action,
                parameters,
                safety_class,
                next,
            } => {
                let correlation_id = Uuid::new_v4();
                let req = CoreDispatchRequest {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    device_id: device_id.clone(),
                    action: *action,
                    parameters: parameters.clone(),
                    safety_class: *safety_class,
                    correlation_id: Some(correlation_id),
                    retries: None,
                    ack_timeout_ms: None,
                    complete_timeout_ms: None,
                };
                let payload = match serde_json::to_vec(&req) {
                    Ok(v) => v,
                    Err(err) => {
                        warn!(error=%err, node_id=%state.node_id, "graph error: failed to serialize dispatch request");
                        runner.active_nodes.clear();
                        return;
                    }
                };
                handle_dispatch_request(
                    config,
                    client,
                    runtime,
                    db,
                    &payload,
                    devices,
                    device_sequences,
                    pending,
                    dispatch_tracker,
                )
                .await;

                let Some(cmd_id) = dispatch_tracker.inflight_command_id(correlation_id) else {
                    let fault = CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "GRAPH_DISPATCH_FAILED".to_string(),
                        severity: "WARN".to_string(),
                        message: "Graph dispatch did not create an inflight command".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({
                            "node_id": state.node_id,
                            "device_id": device_id,
                            "correlation_id": correlation_id,
                        }),
                    };
                    publish_core_fault(client, &config.room_id, fault.clone()).await;
                    if let Some(db) = db {
                        if let Ok(v) = serde_json::to_value(&fault) {
                            db.enqueue_json(
                                &config.room_id,
                                None,
                                &format!("room/{}/core/fault", config.room_id),
                                "CORE_FAULT",
                                fault.observed_at_unix_ms,
                                v,
                            );
                        }
                    }
                    runner.active_nodes.clear();
                    return;
                };

                transitions_this_tick += 1;
                state.waiting_on_command_id = Some(cmd_id);
                state.next_after_wait = next.clone();
                state.entered_at = None;
                next_active.push(state);
            }
        }
    }

    runner.active_nodes = next_active;
}

async fn publish_device_command(
    client: &rumqttc::AsyncClient,
    room_id: &str,
    device_id: &str,
    cmd: &CommandEnvelope,
) -> bool {
    let topic = format!("room/{}/device/{}/cmd", room_id, device_id);
    match serde_json::to_vec(cmd) {
        Ok(bytes) => {
            if let Err(err) = client
                .publish(topic, rumqttc::QoS::AtLeastOnce, false, bytes)
                .await
            {
                warn!(device_id, error=%err, "failed to publish device command");
                return false;
            }
            info!(
                device_id,
                command_id=%cmd.command_id,
                correlation_id=%cmd.correlation_id,
                sequence=cmd.sequence,
                "published device command"
            );
            true
        }
        Err(err) => {
            warn!(device_id, error=%err, "failed to serialize device command");
            false
        }
    }
}

async fn tick_pending_commands(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &RuntimeState,
    db: Option<&DbWriter>,
    pending: &mut std::collections::HashMap<Uuid, PendingCommand>,
    dispatch_tracker: &mut DispatchTracker,
) {
    if pending.is_empty() {
        return;
    }
    if runtime.dispatch_is_paused() {
        return;
    }

    let now = Instant::now();
    let mut to_remove: Vec<Uuid> = Vec::new();

    for (command_id, p) in pending.iter_mut() {
        if p.rejected {
            warn!(
                device_id=%p.device_id,
                command_id=%command_id,
                reason_code=?p.reason_code,
                "command rejected"
            );
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "COMMAND_REJECTED".to_string(),
                severity: "WARN".to_string(),
                message: "Device rejected command".to_string(),
                observed_at_unix_ms: unix_ms_now(),
                details: serde_json::json!({
                    "device_id": p.device_id,
                    "command_id": command_id,
                    "correlation_id": p.cmd.correlation_id,
                    "reason_code": p.reason_code,
                }),
            };
            publish_device_fault(client, &config.room_id, &p.device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(&p.device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, p.device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
            dispatch_tracker.mark_done(p.cmd.correlation_id);
            to_remove.push(*command_id);
            continue;
        }
        if p.completed {
            info!(device_id=%p.device_id, command_id=%command_id, "command completed");
            dispatch_tracker.mark_done(p.cmd.correlation_id);
            to_remove.push(*command_id);
            continue;
        }

        if p.accepted {
            if let Some(accepted_at) = p.accepted_at {
                if now.duration_since(accepted_at).as_millis() as u64 > p.complete_timeout_ms {
                    warn!(device_id=%p.device_id, command_id=%command_id, "command completion timeout (no retry after ACCEPTED)");
                    let fault = CoreFault {
                        schema: SCHEMA_VERSION.to_string(),
                        room_id: config.room_id.clone(),
                        kind: "COMMAND_COMPLETE_TIMEOUT".to_string(),
                        severity: "WARN".to_string(),
                        message: "Command completion timeout after ACCEPTED".to_string(),
                        observed_at_unix_ms: unix_ms_now(),
                        details: serde_json::json!({
                            "device_id": p.device_id,
                            "command_id": command_id,
                            "correlation_id": p.cmd.correlation_id,
                            "complete_timeout_ms": p.complete_timeout_ms,
                        }),
                    };
                    publish_device_fault(client, &config.room_id, &p.device_id, &fault).await;
                    if let Some(db) = db {
                        if let Ok(v) = serde_json::to_value(&fault) {
                            db.enqueue_json(
                                &config.room_id,
                                Some(&p.device_id),
                                &format!(
                                    "room/{}/core/device/{}/fault",
                                    config.room_id, p.device_id
                                ),
                                "DEVICE_FAULT",
                                fault.observed_at_unix_ms,
                                v,
                            );
                        }
                    }
                    dispatch_tracker.mark_done(p.cmd.correlation_id);
                    to_remove.push(*command_id);
                }
            }
            continue;
        }

        if now.duration_since(p.published_at).as_millis() as u64 > p.ack_timeout_ms {
            if p.retries_left == 0 {
                warn!(device_id=%p.device_id, command_id=%command_id, "command ack timeout (exhausted retries)");
                let fault = CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "COMMAND_ACK_TIMEOUT".to_string(),
                    severity: "WARN".to_string(),
                    message: "Command ACK timeout (exhausted retries)".to_string(),
                    observed_at_unix_ms: unix_ms_now(),
                    details: serde_json::json!({
                        "device_id": p.device_id,
                        "command_id": command_id,
                        "correlation_id": p.cmd.correlation_id,
                        "ack_timeout_ms": p.ack_timeout_ms,
                    }),
                };
                publish_device_fault(client, &config.room_id, &p.device_id, &fault).await;
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&fault) {
                        db.enqueue_json(
                            &config.room_id,
                            Some(&p.device_id),
                            &format!("room/{}/core/device/{}/fault", config.room_id, p.device_id),
                            "DEVICE_FAULT",
                            fault.observed_at_unix_ms,
                            v,
                        );
                    }
                }
                dispatch_tracker.mark_done(p.cmd.correlation_id);
                to_remove.push(*command_id);
                continue;
            }

            p.retries_left = p.retries_left.saturating_sub(1);
            p.published_at = now;
            if publish_device_command(client, &config.room_id, &p.device_id, &p.cmd).await {
                info!(
                    device_id=%p.device_id,
                    command_id=%command_id,
                    retries_left=p.retries_left,
                    "resent device command"
                );
            }
        }
    }

    for id in to_remove {
        pending.remove(&id);
    }
}

fn should_be_offline(config: &Config, status: &DeviceStatus, now: u64) -> bool {
    if let Some(p) = status.presence {
        if p == PresenceStatus::Offline {
            return true;
        }
    }
    let Some(last) = status.last_heartbeat_at_unix_ms else {
        return status.is_offline;
    };
    now.saturating_sub(last) > config.device_offline_ms
}

async fn recompute_device_liveness(
    config: &Config,
    client: &rumqttc::AsyncClient,
    db: Option<&DbWriter>,
    device_id: &str,
    status: &mut DeviceStatus,
    source: &'static str,
) {
    let now = unix_ms_now();
    let next_offline = should_be_offline(config, status, now);
    if next_offline != status.is_offline {
        status.is_offline = next_offline;
        if status.is_offline {
            warn!(device_id = %device_id, source, "device offline");
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DEVICE_OFFLINE".to_string(),
                severity: "WARN".to_string(),
                message: "Device is offline (presence/heartbeat)".to_string(),
                observed_at_unix_ms: now,
                details: serde_json::json!({
                    "device_id": device_id,
                    "source": source,
                    "presence": status.presence.map(|p| format!("{:?}", p)),
                    "last_heartbeat_at_unix_ms": status.last_heartbeat_at_unix_ms,
                    "device_offline_ms": config.device_offline_ms,
                }),
            };
            publish_device_fault(client, &config.room_id, device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
        } else {
            info!(device_id = %device_id, source, "device online");
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DEVICE_ONLINE".to_string(),
                severity: "INFO".to_string(),
                message: "Device is online".to_string(),
                observed_at_unix_ms: now,
                details: serde_json::json!({
                    "device_id": device_id,
                    "source": source,
                    "presence": status.presence.map(|p| format!("{:?}", p)),
                    "last_heartbeat_at_unix_ms": status.last_heartbeat_at_unix_ms,
                }),
            };
            publish_device_fault(client, &config.room_id, device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
        }
        publish_device_status(config, client, device_id, status).await;
    } else if source == "presence" {
        // Presence changes may arrive even if offline state stays constant; publish updated status.
        publish_device_status(config, client, device_id, status).await;
    }
}

async fn sweep_device_offline(
    config: &Config,
    client: &rumqttc::AsyncClient,
    db: Option<&DbWriter>,
    devices: &mut std::collections::HashMap<String, DeviceStatus>,
) {
    let now = unix_ms_now();
    for (device_id, status) in devices.iter_mut() {
        let next_offline = should_be_offline(config, status, now);
        if next_offline && !status.is_offline {
            status.is_offline = true;
            warn!(device_id = %device_id, "device offline");
            // Publish offline fault when the sweep detects the transition.
            let fault = CoreFault {
                schema: SCHEMA_VERSION.to_string(),
                room_id: config.room_id.clone(),
                kind: "DEVICE_OFFLINE".to_string(),
                severity: "WARN".to_string(),
                message: "Device is offline (timeout sweep)".to_string(),
                observed_at_unix_ms: now,
                details: serde_json::json!({
                    "device_id": device_id,
                    "source": "sweep",
                    "last_heartbeat_at_unix_ms": status.last_heartbeat_at_unix_ms,
                    "device_offline_ms": config.device_offline_ms,
                }),
            };
            publish_device_fault(client, &config.room_id, device_id, &fault).await;
            if let Some(db) = db {
                if let Ok(v) = serde_json::to_value(&fault) {
                    db.enqueue_json(
                        &config.room_id,
                        Some(device_id),
                        &format!("room/{}/core/device/{}/fault", config.room_id, device_id),
                        "DEVICE_FAULT",
                        fault.observed_at_unix_ms,
                        v,
                    );
                }
            }
            publish_device_status(config, client, device_id, status).await;
        }
    }
}

async fn publish_device_status(
    config: &Config,
    client: &rumqttc::AsyncClient,
    device_id: &str,
    status: &DeviceStatus,
) {
    let topic = format!("room/{}/core/device/{}/status", config.room_id, device_id);
    let presence: Option<&'static str> = match status.presence {
        Some(PresenceStatus::Online) => Some("ONLINE"),
        Some(PresenceStatus::Offline) => Some("OFFLINE"),
        None => None,
    };

    let payload = serde_json::json!({
        "schema": SCHEMA_VERSION,
        "room_id": config.room_id,
        "device_id": device_id,
        "is_offline": status.is_offline,
        "reported_safety_kind": status.last_reported_safety.as_ref().map(|s| format!("{:?}", s.kind)),
        "reported_safety_latched": status.last_reported_safety.as_ref().map(|s| s.latched).unwrap_or(false),
        "reported_safety_reason_code": status.last_reported_safety.as_ref().and_then(|s| s.reason_code.clone()),
        "computed_at_unix_ms": unix_ms_now(),
        "last_heartbeat_at_unix_ms": status.last_heartbeat_at_unix_ms.unwrap_or(0),
        "last_presence_at_unix_ms": status.last_presence_at_unix_ms.unwrap_or(0),
        "last_state_at_unix_ms": status.last_state_at_unix_ms.unwrap_or(0),
        "presence": presence
    });
    if let Ok(bytes) = serde_json::to_vec(&payload) {
        if let Err(err) = client
            .publish(topic, rumqttc::QoS::AtLeastOnce, true, bytes)
            .await
        {
            warn!(error = %err, "failed to publish device status");
        }
    }
}

async fn maybe_publish_dev_test_command(
    config: &Config,
    client: &rumqttc::AsyncClient,
    runtime: &RuntimeState,
    devices: &std::collections::HashMap<String, DeviceStatus>,
    device_sequences: &mut std::collections::HashMap<String, u64>,
    last_sent: &mut Instant,
) {
    if config.dry_run {
        return;
    }
    if runtime.dispatch_is_paused() {
        return;
    }
    let Some(device_id) = config.dev_test_command_device_id.as_deref() else {
        return;
    };
    if config.dev_test_command_interval_ms == 0 {
        return;
    }
    if last_sent.elapsed() < Duration::from_millis(config.dev_test_command_interval_ms) {
        return;
    }

    let Some(status) = devices.get(device_id) else {
        return;
    };
    if status.is_offline {
        return;
    }

    let Some(key) = config.device_hmac_keys.get(device_id) else {
        warn!(
            device_id,
            "DEV_TEST_COMMAND_DEVICE_ID set but no key present in DEVICE_HMAC_KEYS_JSON"
        );
        return;
    };

    let next_seq = device_sequences
        .get(device_id)
        .copied()
        .unwrap_or(0)
        .wrapping_add(1);
    device_sequences.insert(device_id.to_string(), next_seq);

    let mut cmd = CommandEnvelope {
        schema: SCHEMA_VERSION.to_string(),
        room_id: config.room_id.clone(),
        device_id: device_id.to_string(),
        command_id: Uuid::new_v4(),
        correlation_id: Uuid::new_v4(),
        sequence: next_seq,
        issued_at_unix_ms: unix_ms_now(),
        action: CommandAction::Set,
        parameters: serde_json::json!({"kind":"DEV_TEST"}),
        safety_class: SafetyClass::NonCritical,
        auth: None,
    };

    if let Err(err) = sign_command_hmac_sha256(&mut cmd, key, None) {
        warn!(device_id, error=%err, "failed to sign dev test command");
        return;
    }

    let topic = format!("room/{}/device/{}/cmd", config.room_id, device_id);
    match serde_json::to_vec(&cmd) {
        Ok(bytes) => {
            if let Err(err) = client
                .publish(topic, rumqttc::QoS::AtLeastOnce, false, bytes)
                .await
            {
                warn!(device_id, error=%err, "failed to publish dev test command");
                return;
            }
            info!(
                device_id,
                command_id=%cmd.command_id,
                correlation_id=%cmd.correlation_id,
                sequence=cmd.sequence,
                "published dev test command"
            );
            *last_sent = Instant::now();
        }
        Err(err) => warn!(device_id, error=%err, "failed to serialize dev test command"),
    }
}

async fn handle_mqtt_event(
    config: &Config,
    client: &rumqttc::AsyncClient,
    ev: MqttEvent,
    db: Option<&DbWriter>,
    runtime: &mut RuntimeState,
    graph_runner: &mut GraphRunner,
    devices: &mut std::collections::HashMap<String, DeviceStatus>,
    device_sequences: &mut std::collections::HashMap<String, u64>,
    pending: &mut std::collections::HashMap<Uuid, PendingCommand>,
    dispatch_tracker: &mut DispatchTracker,
) {
    match ev {
        MqttEvent::Publish(msg) => {
            handle_incoming_mqtt(
                config,
                client,
                msg,
                db,
                runtime,
                graph_runner,
                devices,
                device_sequences,
                pending,
                dispatch_tracker,
            )
            .await;
        }
        MqttEvent::Disconnected(err) => {
            if runtime.broker_outage_since_unix_ms.is_some() {
                return;
            }
            runtime.broker_outage_since_unix_ms = Some(unix_ms_now());
            runtime.recompute_dispatch_pause_reason();

            // Safety: do not allow delayed replays/retries after a broker outage.
            pending.clear();
            dispatch_tracker.inflight.clear();
            graph_runner.active_nodes.clear();

            warn!(error=%err, "mqtt broker disconnected; room dispatch paused (manual recovery required)");
        }
        MqttEvent::Connected => {
            if let Some(down_since) = runtime.broker_outage_since_unix_ms {
                let recovered_at = unix_ms_now();
                let fault = CoreFault {
                    schema: SCHEMA_VERSION.to_string(),
                    room_id: config.room_id.clone(),
                    kind: "BROKER_OUTAGE".to_string(),
                    severity: "CRITICAL".to_string(),
                    message: "MQTT broker outage detected; dispatch remains paused until manually resumed".to_string(),
                    observed_at_unix_ms: recovered_at,
                    details: serde_json::json!({
                        "down_since_unix_ms": down_since,
                        "recovered_at_unix_ms": recovered_at,
                    }),
                };
                publish_core_fault(client, &config.room_id, fault.clone()).await;
                if let Some(db) = db {
                    if let Ok(v) = serde_json::to_value(&fault) {
                        db.enqueue_json(
                            &config.room_id,
                            None,
                            &format!("room/{}/core/fault", config.room_id),
                            "CORE_FAULT",
                            fault.observed_at_unix_ms,
                            v,
                        );
                    }
                }
            }
        }
    }
}

#[derive(Debug)]
struct DbEvent {
    room_id: String,
    device_id: Option<String>,
    topic: String,
    kind: String,
    observed_at_unix_ms: u64,
    payload: serde_json::Value,
}

#[derive(Clone)]
struct DbWriter {
    tx: mpsc::Sender<DbEvent>,
}

impl DbWriter {
    async fn connect(database_url: &str) -> anyhow::Result<Self> {
        let (client, connection) = tokio_postgres::connect(database_url, tokio_postgres::NoTls)
            .await
            .context("connect postgres")?;
        tokio::spawn(async move {
            if let Err(err) = connection.await {
                warn!(error=%err, "postgres connection error");
            }
        });

        let (tx, mut rx) = mpsc::channel::<DbEvent>(4096);
        tokio::spawn(async move {
            while let Some(ev) = rx.recv().await {
                let observed_at: std::time::SystemTime = std::time::UNIX_EPOCH
                    .checked_add(Duration::from_millis(ev.observed_at_unix_ms))
                    .unwrap_or_else(std::time::SystemTime::now);

                if let Err(err) = client
                    .execute(
                        "INSERT INTO events (room_id, device_id, topic, kind, observed_at, payload) VALUES ($1,$2,$3,$4,$5,$6)",
                        &[
                            &ev.room_id,
                            &ev.device_id,
                            &ev.topic,
                            &ev.kind,
                            &observed_at,
                            &ev.payload,
                        ],
                    )
                    .await
                {
                    warn!(error=%err, kind=%ev.kind, topic=%ev.topic, "failed to insert event");
                }
            }
        });

        Ok(Self { tx })
    }

    fn enqueue_json(
        &self,
        room_id: &str,
        device_id: Option<&str>,
        topic: &str,
        kind: &str,
        observed_at_unix_ms: u64,
        payload: serde_json::Value,
    ) {
        let ev = DbEvent {
            room_id: room_id.to_string(),
            device_id: device_id.map(|s| s.to_string()),
            topic: topic.to_string(),
            kind: kind.to_string(),
            observed_at_unix_ms,
            payload,
        };
        let _ = self.tx.try_send(ev);
    }
}
