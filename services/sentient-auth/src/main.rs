use std::net::SocketAddr;
use std::time::Duration;

use anyhow::Context;
use argon2::{password_hash::SaltString, Argon2, PasswordHash, PasswordHasher, PasswordVerifier};
use axum::{
    extract::ConnectInfo,
    extract::State,
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
    response::Response,
    routing::{get, post},
    Json, Router,
};
use jsonwebtoken::{DecodingKey, EncodingKey, Header, Validation};
use rand::rngs::OsRng;
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use tokio_postgres::NoTls;
use tracing::{info, warn};

#[derive(Debug, Clone)]
struct Config {
    bind_addr: SocketAddr,
    database_url: String,
    jwt_secret: Vec<u8>,
    bootstrap_token: Option<String>,
    jwt_ttl_seconds: u64,
    min_password_len: usize,
    login_rate_limit_per_window: u32,
    login_rate_limit_window_seconds: u64,
}

impl Config {
    fn from_env() -> anyhow::Result<Self> {
        let database_url = std::env::var("DATABASE_URL").context("DATABASE_URL")?;
        let bind_ip = std::env::var("AUTH_BIND_IP").unwrap_or_else(|_| "0.0.0.0".to_string());
        let bind_port: u16 = std::env::var("AUTH_PORT")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(8081);
        let bind_addr: SocketAddr = format!("{bind_ip}:{bind_port}").parse()?;
        let jwt_secret = std::env::var("SENTIENT_JWT_SECRET")
            .context("SENTIENT_JWT_SECRET")?
            .into_bytes();
        if jwt_secret.len() < 16 {
            anyhow::bail!("SENTIENT_JWT_SECRET must be at least 16 bytes");
        }
        let bootstrap_token = std::env::var("AUTH_BOOTSTRAP_TOKEN")
            .ok()
            .filter(|v| !v.is_empty());

        let jwt_ttl_seconds: u64 = std::env::var("AUTH_JWT_TTL_SECONDS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(60 * 60 * 12);
        let min_password_len: usize = std::env::var("AUTH_MIN_PASSWORD_LEN")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(12);
        let login_rate_limit_per_window: u32 = std::env::var("AUTH_LOGIN_RATE_LIMIT_PER_WINDOW")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(10);
        let login_rate_limit_window_seconds: u64 =
            std::env::var("AUTH_LOGIN_RATE_LIMIT_WINDOW_SECONDS")
                .ok()
                .and_then(|v| v.parse().ok())
                .unwrap_or(60);

        Ok(Self {
            bind_addr,
            database_url,
            jwt_secret,
            bootstrap_token,
            jwt_ttl_seconds,
            min_password_len,
            login_rate_limit_per_window,
            login_rate_limit_window_seconds,
        })
    }
}

#[derive(Clone)]
struct AppState {
    config: Config,
    db: std::sync::Arc<tokio_postgres::Client>,
    limiter: std::sync::Arc<LoginRateLimiter>,
}

#[derive(Debug, Serialize, Deserialize)]
struct Claims {
    sub: String,
    role: String,
    exp: usize,
    iat: usize,
}

#[derive(Debug, Deserialize)]
struct LoginRequest {
    username: String,
    password: String,
}

#[derive(Debug, Serialize)]
struct LoginResponse {
    access_token: String,
    token_type: &'static str,
}

#[derive(Debug, Deserialize)]
struct BootstrapRequest {
    username: String,
    password: String,
    role: String,
}

#[derive(Debug, Deserialize)]
struct UpdatePasswordRequest {
    password: String,
}

#[derive(Debug, Deserialize)]
struct SetEnabledRequest {
    enabled: bool,
}

#[derive(Debug)]
struct LoginRateLimiter {
    state: Mutex<std::collections::HashMap<String, Vec<std::time::Instant>>>,
}

impl LoginRateLimiter {
    fn new() -> Self {
        Self {
            state: Mutex::new(std::collections::HashMap::new()),
        }
    }

    async fn allow(&self, key: String, max: u32, window: Duration) -> bool {
        let now = std::time::Instant::now();
        let mut guard = self.state.lock().await;
        let bucket = guard.entry(key).or_default();
        bucket.retain(|t| now.duration_since(*t) <= window);
        if bucket.len() as u32 >= max {
            return false;
        }
        bucket.push(now);
        true
    }
}

fn bearer(headers: &HeaderMap) -> Option<&str> {
    let h = headers
        .get(axum::http::header::AUTHORIZATION)?
        .to_str()
        .ok()?;
    h.strip_prefix("Bearer ")
}

fn verify_bootstrap(headers: &HeaderMap, cfg: &Config) -> bool {
    let Some(expected) = cfg.bootstrap_token.as_deref() else {
        return false;
    };
    bearer(headers).is_some_and(|t| t == expected)
}

fn verify_jwt(headers: &HeaderMap, cfg: &Config) -> Option<Claims> {
    let token = bearer(headers)?;
    let key = DecodingKey::from_secret(&cfg.jwt_secret);
    let mut validation = Validation::default();
    validation.validate_exp = true;
    jsonwebtoken::decode::<Claims>(token, &key, &validation)
        .ok()
        .map(|d| d.claims)
}

fn client_ip(headers: &HeaderMap, connect: SocketAddr) -> String {
    if let Some(h) = headers.get("x-forwarded-for").and_then(|v| v.to_str().ok()) {
        if let Some(first) = h
            .split(',')
            .next()
            .map(|s| s.trim())
            .filter(|s| !s.is_empty())
        {
            return first.to_string();
        }
    }
    connect.ip().to_string()
}

fn user_agent(headers: &HeaderMap) -> Option<String> {
    headers
        .get(axum::http::header::USER_AGENT)
        .and_then(|v| v.to_str().ok())
        .map(|s| s.to_string())
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let config = Config::from_env()?;
    info!(bind=%config.bind_addr, "sentient-auth starting");

    let (db, conn) = tokio_postgres::connect(&config.database_url, NoTls)
        .await
        .context("connect db")?;
    tokio::spawn(async move {
        if let Err(err) = conn.await {
            warn!(error=%err, "postgres connection error (auth)");
        }
    });

    let state = AppState {
        config: config.clone(),
        db: std::sync::Arc::new(db),
        limiter: std::sync::Arc::new(LoginRateLimiter::new()),
    };

    let app = Router::new()
        .route("/health", get(|| async { "ok" }))
        .route("/v8/auth/login", post(login))
        .route("/v8/auth/bootstrap", post(bootstrap))
        .route("/v8/auth/users", get(list_users).post(create_user))
        .route("/v8/auth/users/{username}/password", post(set_password))
        .route("/v8/auth/users/{username}/enabled", post(set_enabled))
        .with_state(state);

    let listener = tokio::net::TcpListener::bind(config.bind_addr).await?;
    axum::serve(
        listener,
        app.into_make_service_with_connect_info::<SocketAddr>(),
    )
    .await?;
    Ok(())
}

async fn login(
    ConnectInfo(connect): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    State(state): State<AppState>,
    Json(req): Json<LoginRequest>,
) -> impl IntoResponse {
    let ip = client_ip(&headers, connect);
    let ua = user_agent(&headers);
    let limiter_key = format!("{}:{}", ip, req.username);
    let allowed = state
        .limiter
        .allow(
            limiter_key,
            state.config.login_rate_limit_per_window,
            Duration::from_secs(state.config.login_rate_limit_window_seconds),
        )
        .await;
    if !allowed {
        audit(
            &state.db,
            "LOGIN_RATE_LIMIT",
            Some(&req.username),
            None,
            Some(&ip),
            ua.as_deref(),
            false,
            serde_json::json!({}),
        )
        .await;
        return StatusCode::TOO_MANY_REQUESTS.into_response();
    }

    let row = match state
        .db
        .query_opt(
            "SELECT password_hash, role, enabled FROM users WHERE username = $1",
            &[&req.username],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "db error on login");
            audit(
                &state.db,
                "LOGIN_DB_ERROR",
                Some(&req.username),
                None,
                Some(&ip),
                ua.as_deref(),
                false,
                serde_json::json!({"error": err.to_string()}),
            )
            .await;
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };

    let Some(row) = row else {
        audit(
            &state.db,
            "LOGIN_NOUSER",
            Some(&req.username),
            None,
            Some(&ip),
            ua.as_deref(),
            false,
            serde_json::json!({}),
        )
        .await;
        return StatusCode::UNAUTHORIZED.into_response();
    };
    let password_hash: String = row.get(0);
    let role: String = row.get(1);
    let enabled: bool = row.get(2);
    if !enabled {
        audit(
            &state.db,
            "LOGIN_DISABLED",
            Some(&req.username),
            Some(&role),
            Some(&ip),
            ua.as_deref(),
            false,
            serde_json::json!({}),
        )
        .await;
        return StatusCode::UNAUTHORIZED.into_response();
    }

    let parsed = match PasswordHash::new(&password_hash) {
        Ok(p) => p,
        Err(_) => {
            audit(
                &state.db,
                "LOGIN_BAD_HASH",
                Some(&req.username),
                Some(&role),
                Some(&ip),
                ua.as_deref(),
                false,
                serde_json::json!({}),
            )
            .await;
            return StatusCode::UNAUTHORIZED.into_response();
        }
    };
    if Argon2::default()
        .verify_password(req.password.as_bytes(), &parsed)
        .is_err()
    {
        audit(
            &state.db,
            "LOGIN_BAD_PASSWORD",
            Some(&req.username),
            Some(&role),
            Some(&ip),
            ua.as_deref(),
            false,
            serde_json::json!({}),
        )
        .await;
        return StatusCode::UNAUTHORIZED.into_response();
    }

    let now = chrono::Utc::now().timestamp() as usize;
    let exp = now + state.config.jwt_ttl_seconds as usize;
    let claims = Claims {
        sub: req.username,
        role,
        iat: now,
        exp,
    };
    let token = match jsonwebtoken::encode(
        &Header::default(),
        &claims,
        &EncodingKey::from_secret(&state.config.jwt_secret),
    ) {
        Ok(t) => t,
        Err(err) => {
            warn!(error=%err, "failed to sign jwt");
            audit(
                &state.db,
                "LOGIN_JWT_SIGN_ERROR",
                Some(&claims.sub),
                Some(&claims.role),
                Some(&ip),
                ua.as_deref(),
                false,
                serde_json::json!({"error": err.to_string()}),
            )
            .await;
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };

    audit(
        &state.db,
        "LOGIN_OK",
        Some(&claims.sub),
        Some(&claims.role),
        Some(&ip),
        ua.as_deref(),
        true,
        serde_json::json!({}),
    )
    .await;

    (
        StatusCode::OK,
        Json(LoginResponse {
            access_token: token,
            token_type: "Bearer",
        }),
    )
        .into_response()
}

async fn bootstrap(
    ConnectInfo(connect): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    State(state): State<AppState>,
    Json(req): Json<BootstrapRequest>,
) -> Response {
    if !verify_bootstrap(&headers, &state.config) {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    let ip = client_ip(&headers, connect);
    let ua = user_agent(&headers);

    match state.db.query_one("SELECT COUNT(*) FROM users", &[]).await {
        Ok(row) => {
            let count: i64 = row.get(0);
            if count > 0 {
                audit(
                    &state.db,
                    "BOOTSTRAP_DENIED_ALREADY_INITIALIZED",
                    Some(&req.username),
                    Some(&req.role),
                    Some(&ip),
                    ua.as_deref(),
                    false,
                    serde_json::json!({"user_count": count}),
                )
                .await;
                return StatusCode::CONFLICT.into_response();
            }
        }
        Err(err) => {
            warn!(error=%err, "db error on bootstrap");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    }

    let res = create_user_inner(
        &state.db,
        &state.config,
        &req.username,
        &req.password,
        &req.role,
    )
    .await;
    let ok = res.status() == StatusCode::CREATED;
    audit(
        &state.db,
        "BOOTSTRAP_CREATE_USER",
        Some(&req.username),
        Some(&req.role),
        Some(&ip),
        ua.as_deref(),
        ok,
        serde_json::json!({}),
    )
    .await;
    res
}

async fn create_user(
    ConnectInfo(connect): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    State(state): State<AppState>,
    Json(req): Json<BootstrapRequest>,
) -> Response {
    let Some(claims) = verify_jwt(&headers, &state.config) else {
        return StatusCode::UNAUTHORIZED.into_response();
    };
    if claims.role != "ADMIN" {
        return StatusCode::FORBIDDEN.into_response();
    }
    let ip = client_ip(&headers, connect);
    let ua = user_agent(&headers);
    let res = create_user_inner(
        &state.db,
        &state.config,
        &req.username,
        &req.password,
        &req.role,
    )
    .await;
    let ok = res.status() == StatusCode::CREATED;
    audit(
        &state.db,
        "CREATE_USER",
        Some(&req.username),
        Some(&req.role),
        Some(&ip),
        ua.as_deref(),
        ok,
        serde_json::json!({"actor": claims.sub, "actor_role": claims.role}),
    )
    .await;
    res
}

async fn list_users(headers: HeaderMap, State(state): State<AppState>) -> Response {
    let Some(claims) = verify_jwt(&headers, &state.config) else {
        return StatusCode::UNAUTHORIZED.into_response();
    };
    if claims.role != "ADMIN" {
        return StatusCode::FORBIDDEN.into_response();
    }

    let rows = match state
        .db
        .query(
            "SELECT username, role, enabled, (extract(epoch from created_at) * 1000)::bigint AS created_at_unix_ms \
             FROM users ORDER BY username",
            &[],
        )
        .await
    {
        Ok(r) => r,
        Err(err) => {
            warn!(error=%err, "failed to list users");
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
    };

    let out: Vec<serde_json::Value> = rows
        .into_iter()
        .map(|row| {
            let username: String = row.get(0);
            let role: String = row.get(1);
            let enabled: bool = row.get(2);
            let created_at_unix_ms: i64 = row.get(3);
            serde_json::json!({
                "username": username,
                "role": role,
                "enabled": enabled,
                "created_at_unix_ms": created_at_unix_ms
            })
        })
        .collect();

    (StatusCode::OK, Json(out)).into_response()
}

async fn set_password(
    ConnectInfo(connect): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    State(state): State<AppState>,
    axum::extract::Path(username): axum::extract::Path<String>,
    Json(req): Json<UpdatePasswordRequest>,
) -> Response {
    let Some(claims) = verify_jwt(&headers, &state.config) else {
        return StatusCode::UNAUTHORIZED.into_response();
    };
    if claims.role != "ADMIN" {
        return StatusCode::FORBIDDEN.into_response();
    }
    if req.password.len() < state.config.min_password_len {
        return StatusCode::BAD_REQUEST.into_response();
    }

    let ip = client_ip(&headers, connect);
    let ua = user_agent(&headers);

    let salt = SaltString::generate(&mut OsRng);
    let hash = match Argon2::default().hash_password(req.password.as_bytes(), &salt) {
        Ok(h) => h.to_string(),
        Err(_) => return StatusCode::SERVICE_UNAVAILABLE.into_response(),
    };

    let res = match state
        .db
        .execute(
            "UPDATE users SET password_hash = $1 WHERE username = $2",
            &[&hash, &username],
        )
        .await
    {
        Ok(n) if n == 1 => StatusCode::OK.into_response(),
        Ok(_) => StatusCode::NOT_FOUND.into_response(),
        Err(err) => {
            warn!(error=%err, "failed to update password");
            StatusCode::SERVICE_UNAVAILABLE.into_response()
        }
    };

    audit(
        &state.db,
        "SET_PASSWORD",
        Some(&username),
        None,
        Some(&ip),
        ua.as_deref(),
        res.status() == StatusCode::OK,
        serde_json::json!({"actor": claims.sub, "actor_role": claims.role}),
    )
    .await;

    res
}

async fn set_enabled(
    ConnectInfo(connect): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    State(state): State<AppState>,
    axum::extract::Path(username): axum::extract::Path<String>,
    Json(req): Json<SetEnabledRequest>,
) -> Response {
    let Some(claims) = verify_jwt(&headers, &state.config) else {
        return StatusCode::UNAUTHORIZED.into_response();
    };
    if claims.role != "ADMIN" {
        return StatusCode::FORBIDDEN.into_response();
    }

    let ip = client_ip(&headers, connect);
    let ua = user_agent(&headers);

    let res = match state
        .db
        .execute(
            "UPDATE users SET enabled = $1 WHERE username = $2",
            &[&req.enabled, &username],
        )
        .await
    {
        Ok(n) if n == 1 => StatusCode::OK.into_response(),
        Ok(_) => StatusCode::NOT_FOUND.into_response(),
        Err(err) => {
            warn!(error=%err, "failed to update enabled");
            StatusCode::SERVICE_UNAVAILABLE.into_response()
        }
    };

    audit(
        &state.db,
        "SET_ENABLED",
        Some(&username),
        None,
        Some(&ip),
        ua.as_deref(),
        res.status() == StatusCode::OK,
        serde_json::json!({
            "enabled": req.enabled,
            "actor": claims.sub,
            "actor_role": claims.role
        }),
    )
    .await;

    res
}

async fn create_user_inner(
    db: &tokio_postgres::Client,
    cfg: &Config,
    username: &str,
    password: &str,
    role: &str,
) -> Response {
    let role = match role {
        "ADMIN" | "TECH" | "GM" | "VIEWER" => role,
        _ => return StatusCode::BAD_REQUEST.into_response(),
    };

    if username.trim().is_empty() {
        return StatusCode::BAD_REQUEST.into_response();
    }
    if password.len() < cfg.min_password_len || password.len() > 128 {
        return StatusCode::BAD_REQUEST.into_response();
    }

    let salt = SaltString::generate(&mut OsRng);
    let hash = match Argon2::default().hash_password(password.as_bytes(), &salt) {
        Ok(h) => h.to_string(),
        Err(_) => return StatusCode::SERVICE_UNAVAILABLE.into_response(),
    };

    match db
        .execute(
            "INSERT INTO users (username, password_hash, role) VALUES ($1,$2,$3)",
            &[&username, &hash, &role],
        )
        .await
    {
        Ok(_) => StatusCode::CREATED.into_response(),
        Err(err) => {
            warn!(error=%err, "failed to create user");
            StatusCode::CONFLICT.into_response()
        }
    }
}

async fn audit(
    db: &tokio_postgres::Client,
    event_type: &str,
    username: Option<&str>,
    role: Option<&str>,
    ip: Option<&str>,
    user_agent: Option<&str>,
    success: bool,
    details: serde_json::Value,
) {
    let details_json = serde_json::to_string(&details).unwrap_or_else(|_| "{}".to_string());
    let _ = db
        .execute(
            "INSERT INTO audit_log (event_type, username, role, ip, user_agent, success, details) \
             VALUES ($1,$2,$3,$4,$5,$6,$7::jsonb)",
            &[
                &event_type,
                &username,
                &role,
                &ip,
                &user_agent,
                &success,
                &details_json,
            ],
        )
        .await;
}
