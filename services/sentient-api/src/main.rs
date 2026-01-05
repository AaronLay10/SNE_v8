use std::{collections::HashMap, net::SocketAddr, sync::Arc, time::Duration};

use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::{
    extract::{Path, State},
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
    routing::{get, post},
    Json, Router,
};
use sentient_protocol::{
    CoreControlRequest, CoreDispatchRequest, CoreFault, CoreStatus, OscCue,
    CORE_CONTROL_OP_RELOAD_GRAPH, SCHEMA_VERSION,
};
use tokio::sync::Mutex;
use tokio::sync::{broadcast, mpsc, RwLock};
use tracing::{info, warn};
use uuid::Uuid;

#[derive(Debug, Clone)]
struct Config {
    room_id: String,
    mqtt_host: String,
    mqtt_port: u16,
    mqtt_username: Option<String>,
    mqtt_password: Option<String>,
    bind_addr: SocketAddr,
    api_token: Option<String>,
    jwt_secret: Option<Vec<u8>>,
    database_url: Option<String>,
    core_control_token: Option<String>,
}

impl Config {
    fn from_env() -> anyhow::Result<Self> {
        let room_id = std::env::var("ROOM_ID").unwrap_or_else(|_| "room1".to_string());
        let mqtt_host = std::env::var("MQTT_HOST").unwrap_or_else(|_| "mqtt".to_string());
        let mqtt_port = std::env::var("MQTT_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(1883);
        let mqtt_username = std::env::var("MQTT_USERNAME")
            .ok()
            .filter(|v| !v.is_empty());
        let mqtt_password = std::env::var("MQTT_PASSWORD")
            .ok()
            .filter(|v| !v.is_empty());
        let bind_ip = std::env::var("API_BIND_IP").unwrap_or_else(|_| "0.0.0.0".to_string());
        let bind_port: u16 = std::env::var("API_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(8080);
        let bind_addr: SocketAddr = format!("{bind_ip}:{bind_port}").parse()?;
        let api_token = std::env::var("API_TOKEN").ok().filter(|v| !v.is_empty());
        let jwt_secret = std::env::var("SENTIENT_JWT_SECRET")
            .ok()
            .filter(|v| !v.is_empty())
            .map(|v| v.into_bytes());
        let database_url = std::env::var("DATABASE_URL").ok().filter(|v| !v.is_empty());
        let core_control_token = std::env::var("CORE_CONTROL_TOKEN")
            .ok()
            .filter(|v| !v.trim().is_empty());

        Ok(Self {
            room_id,
            mqtt_host,
            mqtt_port,
            mqtt_username,
            mqtt_password,
            bind_addr,
            api_token,
            jwt_secret,
            database_url,
            core_control_token,
        })
    }
}

#[derive(Debug, Default, Clone)]
struct Cache {
    core_status: Option<CoreStatus>,
    core_fault: Option<CoreFault>,
    audio_fault: Option<CoreFault>,
    last_audio_ack: Option<serde_json::Value>,
    device_status: HashMap<String, serde_json::Value>,
    device_fault: HashMap<String, CoreFault>,
}

#[derive(Clone)]
struct AppState {
    config: Arc<Config>,
    mqtt: rumqttc::AsyncClient,
    cache: Arc<RwLock<Cache>>,
    stream: broadcast::Sender<serde_json::Value>,
    db: Option<Arc<tokio_postgres::Client>>,
    safety_reset_tokens: Arc<Mutex<HashMap<Uuid, PendingSafetyReset>>>,
}

fn unix_ms_now() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as u64
}

fn auth_ok(headers: &HeaderMap, token: &Option<String>) -> bool {
    let Some(expected) = token.as_deref() else {
        return true;
    };
    let Some(h) = headers.get(axum::http::header::AUTHORIZATION) else {
        return false;
    };
    let Ok(h) = h.to_str() else {
        return false;
    };
    let Some(actual) = h.strip_prefix("Bearer ") else {
        return false;
    };
    actual == expected
}

#[derive(Debug, serde::Deserialize)]
struct Claims {
    sub: String,
    role: String,
    exp: usize,
    iat: usize,
}

#[derive(Debug, Clone)]
struct Actor {
    sub: String,
    role: String,
}

#[derive(Debug, Clone)]
struct PendingSafetyReset {
    created_at_unix_ms: u64,
    expires_at_unix_ms: u64,
    actor: Actor,
    reason: Option<String>,
}

fn jwt_claims(headers: &HeaderMap, jwt_secret: &Option<Vec<u8>>) -> Option<Claims> {
    let Some(secret) = jwt_secret.as_deref() else {
        return None;
    };
    let h = headers
        .get(axum::http::header::AUTHORIZATION)?
        .to_str()
        .ok()?;
    let token = h.strip_prefix("Bearer ")?;
    let key = jsonwebtoken::DecodingKey::from_secret(secret);
    let mut validation = jsonwebtoken::Validation::default();
    validation.validate_exp = true;
    jsonwebtoken::decode::<Claims>(token, &key, &validation)
        .ok()
        .map(|d| d.claims)
}

fn authorized(headers: &HeaderMap, cfg: &Config) -> bool {
    if auth_ok(headers, &cfg.api_token) {
        return true;
    }
    jwt_claims(headers, &cfg.jwt_secret).is_some()
}

fn require_role(headers: &HeaderMap, cfg: &Config, allowed: &[&str]) -> bool {
    // API_TOKEN bypass (room-local break-glass)
    if auth_ok(headers, &cfg.api_token) {
        return true;
    }
    let Some(claims) = jwt_claims(headers, &cfg.jwt_secret) else {
        return false;
    };
    allowed.iter().any(|r| *r == claims.role)
}

fn actor_from_headers(headers: &HeaderMap, cfg: &Config) -> Actor {
    if auth_ok(headers, &cfg.api_token) {
        return Actor {
            sub: "API_TOKEN".to_string(),
            role: "ADMIN".to_string(),
        };
    }
    if let Some(c) = jwt_claims(headers, &cfg.jwt_secret) {
        return Actor {
            sub: c.sub,
            role: c.role,
        };
    }
    Actor {
        sub: "UNKNOWN".to_string(),
        role: "UNKNOWN".to_string(),
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let config = Arc::new(Config::from_env()?);
    info!(?config, "sentient-api starting");

    let (mqtt, mut events) = connect_mqtt(&config).await?;
    let cache: Arc<RwLock<Cache>> = Arc::new(RwLock::new(Cache::default()));
    let (stream_tx, _) = broadcast::channel::<serde_json::Value>(4096);

    let db = if let Some(url) = config.database_url.as_deref() {
        match connect_db(url).await {
            Ok(c) => {
                info!("api db connected");
                Some(Arc::new(c))
            }
            Err(err) => {
                warn!(error=%err, "api db disabled (connect failed)");
                None
            }
        }
    } else {
        None
    };

    subscribe_defaults(&mqtt, &config.room_id).await?;

    let state = AppState {
        config: config.clone(),
        mqtt: mqtt.clone(),
        cache: cache.clone(),
        stream: stream_tx.clone(),
        db,
        safety_reset_tokens: Arc::new(Mutex::new(HashMap::new())),
    };

    let cache_room_id = config.room_id.clone();
    tokio::spawn(async move {
        while let Some(p) = events.recv().await {
            let ev = mqtt_publish_event(&p);
            let _ = stream_tx.send(ev);
            apply_cache_update(&cache, &cache_room_id, p).await;
        }
    });

    let app = Router::new()
        .route("/health", get(|| async { "ok" }))
        .route("/v8/room/{room_id}/ws", get(ws_stream))
        .route("/v8/room/{room_id}/core/status", get(get_core_status))
        .route("/v8/room/{room_id}/core/fault", get(get_core_fault))
        .route("/v8/room/{room_id}/audio/fault", get(get_audio_fault))
        .route("/v8/room/{room_id}/audio/ack", get(get_audio_ack))
        .route(
            "/v8/room/{room_id}/safety/reset/request",
            post(post_safety_reset_request),
        )
        .route(
            "/v8/room/{room_id}/safety/reset/confirm",
            post(post_safety_reset_confirm),
        )
        .route(
            "/v8/room/{room_id}/graphs",
            get(get_graphs).post(post_graph),
        )
        .route("/v8/room/{room_id}/graphs/active", get(get_graph_active))
        .route(
            "/v8/room/{room_id}/graphs/activate",
            post(post_graph_activate),
        )
        .route("/v8/room/{room_id}/devices", get(list_devices))
        .route(
            "/v8/room/{room_id}/devices/{device_id}/status",
            get(get_device_status),
        )
        .route(
            "/v8/room/{room_id}/devices/{device_id}/fault",
            get(get_device_fault),
        )
        .route("/v8/room/{room_id}/events", get(get_events))
        .route("/v8/room/{room_id}/dispatch", post(post_dispatch))
        .route("/v8/room/{room_id}/control", post(post_control))
        .route("/v8/room/{room_id}/audio/cue", post(post_audio_cue))
        .with_state(state);

    let listener = tokio::net::TcpListener::bind(config.bind_addr).await?;
    info!(bind=%config.bind_addr, "http listening");
    axum::serve(listener, app).await?;
    Ok(())
}

#[derive(Debug, serde::Deserialize)]
struct GraphActivateBody {
    version: i64,
}

async fn get_graphs(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let Some(db) = state.db.as_deref() else {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    };
    let rows = match db
        .query(
            "SELECT version, (extract(epoch from created_at) * 1000)::bigint AS created_at_unix_ms \
             FROM graphs WHERE room_id = $1 ORDER BY version DESC LIMIT 200",
            &[&room_id],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "failed to query graphs");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };
    let out: Vec<serde_json::Value> = rows
        .into_iter()
        .map(|row| {
            let version: i64 = row.get(0);
            let created_at_unix_ms: i64 = row.get(1);
            serde_json::json!({"version": version, "created_at_unix_ms": created_at_unix_ms})
        })
        .collect();
    (StatusCode::OK, Json(out)).into_response()
}

async fn get_graph_active(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH", "GM", "VIEWER"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let Some(db) = state.db.as_deref() else {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    };
    let row = match db
        .query_opt(
            "SELECT active_version, (extract(epoch from activated_at) * 1000)::bigint AS activated_at_unix_ms \
             FROM graph_active WHERE room_id = $1",
            &[&room_id],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "failed to query graph_active");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };
    let Some(row) = row else {
        return StatusCode::NO_CONTENT.into_response();
    };
    let version: i64 = row.get(0);
    let activated_at_unix_ms: i64 = row.get(1);
    (
        StatusCode::OK,
        Json(serde_json::json!({"active_version": version, "activated_at_unix_ms": activated_at_unix_ms})),
    )
        .into_response()
}

async fn post_graph(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(graph): Json<serde_json::Value>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let Some(db) = state.db.as_deref() else {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    };
    // Validate minimum schema/room_id on upload.
    let schema_ok = graph
        .get("schema")
        .and_then(|v| v.as_str())
        .is_some_and(|s| s == SCHEMA_VERSION);
    let room_ok = graph
        .get("room_id")
        .and_then(|v| v.as_str())
        .is_some_and(|s| s == state.config.room_id);
    if !schema_ok || !room_ok {
        return StatusCode::BAD_REQUEST.into_response();
    }

    let next_version_row = match db
        .query_one(
            "SELECT COALESCE(MAX(version), 0) + 1 FROM graphs WHERE room_id = $1",
            &[&room_id],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "failed to compute next graph version");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };
    let version: i64 = next_version_row.get(0);
    if let Err(err) = db
        .execute(
            "INSERT INTO graphs (room_id, version, graph) VALUES ($1,$2,$3)",
            &[&room_id, &version, &graph],
        )
        .await
    {
        warn!(error=%err, "failed to insert graph");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }

    if let Some(db) = state.db.as_deref() {
        let _ = insert_event(
            db,
            &state.config.room_id,
            None,
            "http://sentient-api/v8/graphs",
            "API_GRAPH_CREATE",
            unix_ms_now(),
            serde_json::json!({"version": version}),
        )
        .await;
    }

    (
        StatusCode::CREATED,
        Json(serde_json::json!({"version": version})),
    )
        .into_response()
}

async fn post_graph_activate(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<GraphActivateBody>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    {
        let cache = state.cache.read().await;
        if cache
            .core_status
            .as_ref()
            .is_some_and(|s| s.graph_active_node.is_some())
        {
            // v8: no hot-swap during live runs; graph execution must be pinned to a version.
            return StatusCode::CONFLICT.into_response();
        }
    }
    let Some(db) = state.db.as_deref() else {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    };
    // Ensure version exists.
    let exists = match db
        .query_opt(
            "SELECT 1 FROM graphs WHERE room_id = $1 AND version = $2",
            &[&room_id, &body.version],
        )
        .await
    {
        Ok(r) => r.is_some(),
        Err(err) => {
            warn!(error=%err, "failed to query graph version");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };
    if !exists {
        return StatusCode::NOT_FOUND.into_response();
    }
    if let Err(err) = db
        .execute(
            "INSERT INTO graph_active (room_id, active_version) VALUES ($1,$2) \
             ON CONFLICT (room_id) DO UPDATE SET active_version = EXCLUDED.active_version, activated_at = now()",
            &[&room_id, &body.version],
        )
        .await
    {
        warn!(error=%err, "failed to activate graph");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    if let Some(db) = state.db.as_deref() {
        let _ = insert_event(
            db,
            &state.config.room_id,
            None,
            "http://sentient-api/v8/graphs/activate",
            "API_GRAPH_ACTIVATE",
            unix_ms_now(),
            serde_json::json!({"version": body.version}),
        )
        .await;
    }

    // Best-effort: ask core to reload the active graph from DB (requires dispatch paused in core).
    let mut parameters = serde_json::json!({});
    if let Some(token) = state.config.core_control_token.as_deref() {
        parameters = serde_json::json!({ "token": token });
    }
    let req = CoreControlRequest {
        schema: SCHEMA_VERSION.to_string(),
        room_id: state.config.room_id.clone(),
        op: CORE_CONTROL_OP_RELOAD_GRAPH.to_string(),
        parameters,
        requested_at_unix_ms: unix_ms_now(),
    };
    if let Ok(payload) = serde_json::to_vec(&req) {
        let topic = format!("room/{}/core/control", state.config.room_id);
        let _ = state
            .mqtt
            .publish(topic, rumqttc::QoS::AtLeastOnce, false, payload)
            .await;
    }
    StatusCode::OK.into_response()
}

async fn subscribe_defaults(client: &rumqttc::AsyncClient, room_id: &str) -> anyhow::Result<()> {
    client
        .subscribe(
            format!("room/{}/core/status", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/core/fault", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/audio/ack", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/audio/fault", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/core/device/+/status", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    client
        .subscribe(
            format!("room/{}/core/device/+/fault", room_id),
            rumqttc::QoS::AtLeastOnce,
        )
        .await?;
    Ok(())
}

async fn apply_cache_update(cache: &Arc<RwLock<Cache>>, room_id: &str, p: rumqttc::Publish) {
    let mut c = cache.write().await;
    let topic = p.topic.clone();
    let bytes = p.payload.as_ref();

    if topic == format!("room/{}/core/status", room_id) {
        if let Ok(v) = serde_json::from_slice::<CoreStatus>(bytes) {
            c.core_status = Some(v);
        }
        return;
    }
    if topic == format!("room/{}/core/fault", room_id) {
        if let Ok(v) = serde_json::from_slice::<CoreFault>(bytes) {
            c.core_fault = Some(v);
        }
        return;
    }
    if topic == format!("room/{}/audio/fault", room_id) {
        if let Ok(v) = serde_json::from_slice::<CoreFault>(bytes) {
            c.audio_fault = Some(v);
        }
        return;
    }
    if topic == format!("room/{}/audio/ack", room_id) {
        if let Ok(v) = serde_json::from_slice::<serde_json::Value>(bytes) {
            c.last_audio_ack = Some(v);
        }
        return;
    }

    // room/{room}/core/device/{device}/status|fault
    let prefix = format!("room/{}/core/device/", room_id);
    if let Some(rest) = topic.strip_prefix(&prefix) {
        let mut parts = rest.split('/');
        let device_id = parts.next().unwrap_or("").to_string();
        let kind = parts.next().unwrap_or("");
        if device_id.is_empty() {
            return;
        }
        if kind == "status" {
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(bytes) {
                c.device_status.insert(device_id, v);
            }
        } else if kind == "fault" {
            if let Ok(v) = serde_json::from_slice::<CoreFault>(bytes) {
                c.device_fault.insert(device_id, v);
            }
        }
    }
}

fn mqtt_publish_event(p: &rumqttc::Publish) -> serde_json::Value {
    let mut payload: serde_json::Value =
        serde_json::json!({ "raw": String::from_utf8_lossy(p.payload.as_ref()).to_string() });
    if let Ok(v) = serde_json::from_slice::<serde_json::Value>(p.payload.as_ref()) {
        payload = v;
    }
    serde_json::json!({
        "type": "MQTT_PUBLISH",
        "topic": p.topic,
        "received_at_unix_ms": unix_ms_now(),
        "payload": payload,
    })
}

async fn ws_stream(
    headers: HeaderMap,
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }

    ws.on_upgrade(move |socket| ws_stream_inner(state, socket))
}

async fn ws_stream_inner(state: AppState, mut socket: WebSocket) {
    // Send initial snapshot so dashboards can render immediately.
    let snapshot = {
        let c = state.cache.read().await;
        serde_json::json!({
            "type": "SNAPSHOT",
            "received_at_unix_ms": unix_ms_now(),
            "core_status": c.core_status,
            "core_fault": c.core_fault,
            "device_status": c.device_status,
            "device_fault": c.device_fault,
        })
    };
    let _ = socket
        .send(Message::Text(snapshot.to_string().into()))
        .await;

    let mut rx = state.stream.subscribe();

    loop {
        tokio::select! {
            msg = socket.recv() => {
                match msg {
                    Some(Ok(Message::Close(_))) | None => break,
                    Some(Ok(_)) => {}
                    Some(Err(_)) => break,
                }
            }
            ev = rx.recv() => {
                match ev {
                    Ok(v) => {
                        if socket.send(Message::Text(v.to_string().into())).await.is_err() {
                            break;
                        }
                    }
                    Err(broadcast::error::RecvError::Closed) => break,
                    Err(broadcast::error::RecvError::Lagged(_)) => {
                        // Client is slow; continue.
                    }
                }
            }
        }
    }
}

#[derive(Debug, serde::Deserialize)]
struct EventsQuery {
    #[serde(default)]
    limit: Option<i64>,
}

async fn get_events(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    axum::extract::Query(q): axum::extract::Query<EventsQuery>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let Some(db) = state.db.as_ref() else {
        return StatusCode::NOT_IMPLEMENTED.into_response();
    };
    let limit = q.limit.unwrap_or(100).clamp(1, 1000);

    let rows = match db
        .query(
            "SELECT kind, topic, device_id, (extract(epoch from observed_at) * 1000)::bigint AS observed_at_unix_ms, payload \
             FROM events WHERE room_id = $1 ORDER BY observed_at DESC LIMIT $2",
            &[&room_id, &limit],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "failed to query events");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };

    let mut out: Vec<serde_json::Value> = Vec::with_capacity(rows.len());
    for row in rows {
        let kind: String = row.get(0);
        let topic: String = row.get(1);
        let device_id: Option<String> = row.get(2);
        let observed_at_unix_ms: i64 = row.get(3);
        let payload: serde_json::Value = row.get(4);
        out.push(serde_json::json!({
            "kind": kind,
            "topic": topic,
            "device_id": device_id,
            "observed_at_unix_ms": observed_at_unix_ms,
            "payload": payload,
        }));
    }
    (StatusCode::OK, Json(out)).into_response()
}

async fn get_core_status(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match &c.core_status {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn get_core_fault(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match &c.core_fault {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn get_audio_fault(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match &c.audio_fault {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn get_audio_ack(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match &c.last_audio_ack {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn list_devices(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    let mut devices: Vec<String> = c.device_status.keys().cloned().collect();
    devices.sort();
    (StatusCode::OK, Json(devices)).into_response()
}

async fn get_device_status(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path((room_id, device_id)): Path<(String, String)>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match c.device_status.get(&device_id) {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn get_device_fault(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path((room_id, device_id)): Path<(String, String)>,
) -> impl IntoResponse {
    if !authorized(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let c = state.cache.read().await;
    match c.device_fault.get(&device_id) {
        Some(v) => (StatusCode::OK, Json(v)).into_response(),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

#[derive(Debug, serde::Deserialize)]
struct DispatchBody {
    device_id: String,
    action: sentient_protocol::CommandAction,
    #[serde(default)]
    parameters: serde_json::Value,
    #[serde(default)]
    safety_class: Option<sentient_protocol::SafetyClass>,
    #[serde(default)]
    correlation_id: Option<Uuid>,
    #[serde(default)]
    retries: Option<u32>,
    #[serde(default)]
    ack_timeout_ms: Option<u64>,
    #[serde(default)]
    complete_timeout_ms: Option<u64>,
}

async fn post_dispatch(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<DispatchBody>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH", "GM"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }

    let req = CoreDispatchRequest {
        schema: SCHEMA_VERSION.to_string(),
        room_id,
        device_id: body.device_id,
        action: body.action,
        parameters: body.parameters,
        safety_class: body
            .safety_class
            .unwrap_or(sentient_protocol::SafetyClass::NonCritical),
        correlation_id: body.correlation_id,
        retries: body.retries,
        ack_timeout_ms: body.ack_timeout_ms,
        complete_timeout_ms: body.complete_timeout_ms,
    };
    let payload = match serde_json::to_vec(&req) {
        Ok(v) => v,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };
    let topic = format!("room/{}/core/dispatch", state.config.room_id);
    if let Err(err) = state
        .mqtt
        .publish(topic, rumqttc::QoS::AtLeastOnce, false, payload)
        .await
    {
        warn!(error=%err, "failed to publish dispatch");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    StatusCode::ACCEPTED.into_response()
}

#[derive(Debug, serde::Deserialize)]
struct ControlBody {
    op: String,
    #[serde(default)]
    parameters: serde_json::Value,
}

async fn post_control(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<ControlBody>,
) -> impl IntoResponse {
    let allowed_roles: &[&str] = match body.op.as_str() {
        sentient_protocol::CORE_CONTROL_OP_RESET_SAFETY_LATCH => &["ADMIN", "TECH"],
        _ => &["ADMIN", "TECH", "GM"],
    };
    if !require_role(&headers, &state.config, allowed_roles) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }

    // Enforce dual confirmation for safety reset: require a valid reset token.
    if body.op == sentient_protocol::CORE_CONTROL_OP_RESET_SAFETY_LATCH {
        let reset_id = body
            .parameters
            .get("reset_id")
            .and_then(|v| v.as_str())
            .and_then(|s| Uuid::parse_str(s).ok());
        let Some(reset_id) = reset_id else {
            return StatusCode::BAD_REQUEST.into_response();
        };
        let now = unix_ms_now();
        let mut guard = state.safety_reset_tokens.lock().await;
        let Some(pending) = guard.get(&reset_id).cloned() else {
            return StatusCode::BAD_REQUEST.into_response();
        };
        if pending.expires_at_unix_ms < now {
            guard.remove(&reset_id);
            return StatusCode::BAD_REQUEST.into_response();
        }
        // Consume token on use (single-use).
        guard.remove(&reset_id);

        if let Some(db) = state.db.as_deref() {
            let _ = insert_event(
                db,
                &state.config.room_id,
                None,
                "http://sentient-api/v8/safety/reset/confirm",
                "API_SAFETY_RESET_CONFIRM",
                unix_ms_now(),
                serde_json::json!({
                    "reset_id": reset_id,
                    "requested_by": pending.actor.sub,
                    "requested_role": pending.actor.role,
                    "reason": pending.reason,
                }),
            )
            .await;
        }
    }

    let mut parameters = body.parameters;
    if let Some(token) = state.config.core_control_token.as_deref() {
        if let Some(obj) = parameters.as_object_mut() {
            obj.insert(
                "token".to_string(),
                serde_json::Value::String(token.to_string()),
            );
        } else {
            parameters = serde_json::json!({ "token": token });
        }
    }

    let req = CoreControlRequest {
        schema: SCHEMA_VERSION.to_string(),
        room_id,
        op: body.op,
        parameters,
        requested_at_unix_ms: unix_ms_now(),
    };
    let payload = match serde_json::to_vec(&req) {
        Ok(v) => v,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };
    let topic = format!("room/{}/core/control", state.config.room_id);
    if let Err(err) = state
        .mqtt
        .publish(topic, rumqttc::QoS::AtLeastOnce, false, payload)
        .await
    {
        warn!(error=%err, "failed to publish control");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    StatusCode::ACCEPTED.into_response()
}

async fn post_audio_cue(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<OscCue>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH", "GM"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    if body.room_id != state.config.room_id {
        return StatusCode::BAD_REQUEST.into_response();
    }

    let payload = match serde_json::to_vec(&body) {
        Ok(v) => v,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };
    let topic = format!("room/{}/audio/cue", state.config.room_id);
    if let Err(err) = state
        .mqtt
        .publish(topic, rumqttc::QoS::AtLeastOnce, false, payload)
        .await
    {
        warn!(error=%err, "failed to publish audio cue");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    StatusCode::ACCEPTED.into_response()
}

#[derive(Debug, serde::Deserialize)]
struct SafetyResetRequestBody {
    #[serde(default)]
    reason: Option<String>,
}

async fn post_safety_reset_request(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<SafetyResetRequestBody>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }

    let actor = actor_from_headers(&headers, &state.config);
    let reset_id = Uuid::new_v4();
    let now = unix_ms_now();
    let expires_at_unix_ms = now + 60_000; // 60s

    state.safety_reset_tokens.lock().await.insert(
        reset_id,
        PendingSafetyReset {
            created_at_unix_ms: now,
            expires_at_unix_ms,
            actor: actor.clone(),
            reason: body.reason.clone().filter(|s| !s.trim().is_empty()),
        },
    );

    if let Some(db) = state.db.as_deref() {
        let _ = insert_event(
            db,
            &state.config.room_id,
            None,
            "http://sentient-api/v8/safety/reset/request",
            "API_SAFETY_RESET_REQUEST",
            now,
            serde_json::json!({
                "reset_id": reset_id,
                "expires_at_unix_ms": expires_at_unix_ms,
                "actor": actor.sub,
                "actor_role": actor.role,
                "reason": body.reason,
            }),
        )
        .await;
    }

    (
        StatusCode::OK,
        Json(serde_json::json!({
            "reset_id": reset_id,
            "expires_at_unix_ms": expires_at_unix_ms,
        })),
    )
        .into_response()
}

#[derive(Debug, serde::Deserialize)]
struct SafetyResetConfirmBody {
    reset_id: String,
}

async fn post_safety_reset_confirm(
    headers: HeaderMap,
    State(state): State<AppState>,
    Path(room_id): Path<String>,
    Json(body): Json<SafetyResetConfirmBody>,
) -> impl IntoResponse {
    if !require_role(&headers, &state.config, &["ADMIN", "TECH"]) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    if room_id != state.config.room_id {
        return StatusCode::NOT_FOUND.into_response();
    }
    let reset_id = match Uuid::parse_str(&body.reset_id) {
        Ok(v) => v,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };

    let now = unix_ms_now();
    let confirmer = actor_from_headers(&headers, &state.config);
    let pending = {
        let mut guard = state.safety_reset_tokens.lock().await;
        let Some(p) = guard.get(&reset_id).cloned() else {
            return StatusCode::BAD_REQUEST.into_response();
        };
        if p.expires_at_unix_ms < now {
            guard.remove(&reset_id);
            return StatusCode::BAD_REQUEST.into_response();
        }
        guard.remove(&reset_id);
        p
    };

    if let Some(db) = state.db.as_deref() {
        let _ = insert_event(
            db,
            &state.config.room_id,
            None,
            "http://sentient-api/v8/safety/reset/confirm",
            "API_SAFETY_RESET_CONFIRM",
            now,
            serde_json::json!({
                "reset_id": reset_id,
                "requested_by": pending.actor.sub,
                "requested_role": pending.actor.role,
                "confirmed_by": confirmer.sub,
                "confirmed_role": confirmer.role,
                "reason": pending.reason,
            }),
        )
        .await;
    }

    let req = CoreControlRequest {
        schema: SCHEMA_VERSION.to_string(),
        room_id: state.config.room_id.clone(),
        op: sentient_protocol::CORE_CONTROL_OP_RESET_SAFETY_LATCH.to_string(),
        parameters: serde_json::json!({
            "reset_id": reset_id,
            "requested_by": pending.actor.sub,
            "confirmed_by": confirmer.sub,
            "reason": pending.reason,
        }),
        requested_at_unix_ms: now,
    };
    let mut req = req;
    if let Some(token) = state.config.core_control_token.as_deref() {
        if let Some(obj) = req.parameters.as_object_mut() {
            obj.insert(
                "token".to_string(),
                serde_json::Value::String(token.to_string()),
            );
        }
    }
    let payload = match serde_json::to_vec(&req) {
        Ok(v) => v,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };
    let topic = format!("room/{}/core/control", state.config.room_id);
    if let Err(err) = state
        .mqtt
        .publish(topic, rumqttc::QoS::AtLeastOnce, false, payload)
        .await
    {
        warn!(error=%err, "failed to publish safety reset confirm");
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    StatusCode::ACCEPTED.into_response()
}

async fn connect_mqtt(
    config: &Config,
) -> anyhow::Result<(rumqttc::AsyncClient, mpsc::Receiver<rumqttc::Publish>)> {
    let client_id = format!("sentient-api-{}-{}", config.room_id, Uuid::new_v4());
    let mut opts = rumqttc::MqttOptions::new(client_id, config.mqtt_host.clone(), config.mqtt_port);
    opts.set_keep_alive(Duration::from_secs(5));
    if let (Some(user), Some(pass)) = (
        config.mqtt_username.as_deref(),
        config.mqtt_password.as_deref(),
    ) {
        opts.set_credentials(user, pass);
    }
    let (client, mut eventloop) = rumqttc::AsyncClient::new(opts, 200);
    let (tx, rx) = mpsc::channel::<rumqttc::Publish>(2048);
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
                    warn!(error=%err, "mqtt eventloop error (api)");
                    tokio::time::sleep(Duration::from_secs(1)).await;
                }
            }
        }
    });
    Ok((client, rx))
}

async fn connect_db(database_url: &str) -> anyhow::Result<tokio_postgres::Client> {
    let (client, connection) = tokio_postgres::connect(database_url, tokio_postgres::NoTls).await?;
    tokio::spawn(async move {
        if let Err(err) = connection.await {
            warn!(error=%err, "postgres connection error (api)");
        }
    });
    Ok(client)
}

async fn insert_event(
    db: &tokio_postgres::Client,
    room_id: &str,
    device_id: Option<&str>,
    topic: &str,
    kind: &str,
    observed_at_unix_ms: u64,
    payload: serde_json::Value,
) -> anyhow::Result<()> {
    let observed_at_ms_i64: i64 = observed_at_unix_ms as i64;
    db.execute(
        "INSERT INTO events (room_id, device_id, topic, kind, observed_at, payload) \
         VALUES ($1,$2,$3,$4,to_timestamp($5/1000.0),$6)",
        &[
            &room_id,
            &device_id,
            &topic,
            &kind,
            &observed_at_ms_i64,
            &payload,
        ],
    )
    .await?;
    Ok(())
}
