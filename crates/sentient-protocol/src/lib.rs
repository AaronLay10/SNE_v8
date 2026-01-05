use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const SCHEMA_VERSION: &str = "v8";
pub const AUTH_ALG_HMAC_SHA256: &str = "HMAC-SHA256";
pub const CORE_CONTROL_OP_PAUSE_DISPATCH: &str = "PAUSE_DISPATCH";
pub const CORE_CONTROL_OP_RESUME_DISPATCH: &str = "RESUME_DISPATCH";
pub const CORE_CONTROL_OP_RESET_SAFETY_LATCH: &str = "RESET_SAFETY_LATCH";
pub const CORE_CONTROL_OP_START_GRAPH: &str = "START_GRAPH";
pub const CORE_CONTROL_OP_STOP_GRAPH: &str = "STOP_GRAPH";
pub const CORE_CONTROL_OP_RELOAD_GRAPH: &str = "RELOAD_GRAPH";

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SafetyClass {
    Critical,
    NonCritical,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SafetyStateKind {
    Safe,
    Blocked,
    Fault,
    EStop,
    Maintenance,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SafetyState {
    pub kind: SafetyStateKind,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub reason_code: Option<String>,
    pub latched: bool,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum CommandAction {
    Open,
    Close,
    Move,
    Set,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CommandAuth {
    /// Authentication scheme identifier.
    /// v8 default: "HMAC-SHA256"
    pub alg: String,
    /// Key identifier (device-side), to support rotation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kid: Option<String>,
    /// Hex-encoded MAC over the canonical signing bytes.
    pub mac_hex: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CommandEnvelope {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub command_id: Uuid,
    pub correlation_id: Uuid,
    pub sequence: u64,
    pub issued_at_unix_ms: u64,
    pub action: CommandAction,
    #[serde(default)]
    pub parameters: serde_json::Value,
    pub safety_class: SafetyClass,
    /// Optional at the protocol layer; required for real hardware deployments.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub auth: Option<CommandAuth>,
}

fn command_action_str(a: CommandAction) -> &'static str {
    match a {
        CommandAction::Open => "OPEN",
        CommandAction::Close => "CLOSE",
        CommandAction::Move => "MOVE",
        CommandAction::Set => "SET",
    }
}

fn safety_class_str(s: SafetyClass) -> &'static str {
    match s {
        SafetyClass::Critical => "CRITICAL",
        SafetyClass::NonCritical => "NON_CRITICAL",
    }
}

fn canonicalize_json(value: &serde_json::Value) -> serde_json::Value {
    match value {
        serde_json::Value::Object(map) => {
            let mut keys: Vec<&String> = map.keys().collect();
            keys.sort();
            let mut out = serde_json::Map::new();
            for k in keys {
                let v = map.get(k).expect("key exists");
                out.insert(k.clone(), canonicalize_json(v));
            }
            serde_json::Value::Object(out)
        }
        serde_json::Value::Array(items) => {
            serde_json::Value::Array(items.iter().map(canonicalize_json).collect())
        }
        other => other.clone(),
    }
}

pub fn canonical_parameters_json(parameters: &serde_json::Value) -> serde_json::Result<String> {
    let canon = canonicalize_json(parameters);
    serde_json::to_string(&canon)
}

pub fn signing_string(cmd: &CommandEnvelope) -> serde_json::Result<String> {
    let params = canonical_parameters_json(&cmd.parameters)?;

    Ok(format!(
        "schema={}\nroom_id={}\ndevice_id={}\ncommand_id={}\ncorrelation_id={}\nsequence={}\nissued_at_unix_ms={}\naction={}\nsafety_class={}\nparameters={}",
        cmd.schema,
        cmd.room_id,
        cmd.device_id,
        cmd.command_id,
        cmd.correlation_id,
        cmd.sequence,
        cmd.issued_at_unix_ms,
        command_action_str(cmd.action),
        safety_class_str(cmd.safety_class),
        params
    ))
}

pub fn hmac_sha256_hex(key: &[u8], signing_bytes: &[u8]) -> String {
    use hmac::{Hmac, Mac};
    use sha2::Sha256;

    let mut mac = Hmac::<Sha256>::new_from_slice(key).expect("HMAC can take key of any size");
    mac.update(signing_bytes);
    let out = mac.finalize().into_bytes();
    hex::encode(out)
}

pub fn sign_command_hmac_sha256(
    cmd: &mut CommandEnvelope,
    key: &[u8],
    kid: Option<String>,
) -> serde_json::Result<()> {
    let s = signing_string(cmd)?;
    let mac_hex = hmac_sha256_hex(key, s.as_bytes());
    cmd.auth = Some(CommandAuth {
        alg: AUTH_ALG_HMAC_SHA256.to_string(),
        kid,
        mac_hex,
    });
    Ok(())
}

pub fn verify_command_hmac_sha256(cmd: &CommandEnvelope, key: &[u8]) -> serde_json::Result<bool> {
    let Some(auth) = &cmd.auth else {
        return Ok(false);
    };
    if auth.alg != AUTH_ALG_HMAC_SHA256 {
        return Ok(false);
    }
    let s = signing_string(cmd)?;
    let expected = hmac_sha256_hex(key, s.as_bytes());
    Ok(constant_time_eq_hex(&expected, &auth.mac_hex))
}

fn constant_time_eq_hex(a: &str, b: &str) -> bool {
    use subtle::ConstantTimeEq;
    a.as_bytes().ct_eq(b.as_bytes()).into()
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AckStatus {
    Accepted,
    Rejected,
    Completed,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CommandAck {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub command_id: Uuid,
    pub correlation_id: Uuid,
    pub status: AckStatus,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub reason_code: Option<String>,
    pub safety_state: SafetyState,
    pub observed_at_unix_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Heartbeat {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub uptime_ms: u64,
    pub firmware_version: String,
    pub safety_state: SafetyState,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_error: Option<String>,
    pub observed_at_unix_ms: u64,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum PresenceStatus {
    Online,
    Offline,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Presence {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub status: PresenceStatus,
    pub observed_at_unix_ms: u64,
}

/// Generic device state snapshot.
///
/// Each controller can publish a retained "last known" state for UIs/tools and
/// for core restart recovery. The `state` object is device-specific.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct DeviceState {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub safety_state: SafetyState,
    #[serde(default)]
    pub state: serde_json::Value,
    pub observed_at_unix_ms: u64,
}

/// Request payload for tools/UIs to ask `sentient-core` to dispatch a device command.
///
/// This is an MQTT-only control plane intended for commissioning and early
/// integration before the HTTP/WebSocket APIs exist.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CoreDispatchRequest {
    pub schema: String,
    pub room_id: String,
    pub device_id: String,
    pub action: CommandAction,
    /// Device-specific parameters (JSON object preferred; may be `{}`).
    #[serde(default)]
    pub parameters: serde_json::Value,
    #[serde(default = "default_safety_class_non_critical")]
    pub safety_class: SafetyClass,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub correlation_id: Option<Uuid>,
    /// Override retry count (defaults are core-configured).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub retries: Option<u32>,
    /// Override ack timeout (ms).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ack_timeout_ms: Option<u64>,
    /// Override completion timeout (ms).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub complete_timeout_ms: Option<u64>,
}

/// Request payload for tools/UIs to control core runtime gates (pause/resume).
///
/// This is a commissioning/ops control plane intended to be replaced by the
/// authenticated HTTP/WebSocket APIs later.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CoreControlRequest {
    pub schema: String,
    pub room_id: String,
    /// Operation identifier (string) to keep the control plane flexible.
    ///
    /// Current ops:
    /// - "PAUSE_DISPATCH"
    /// - "RESUME_DISPATCH"
    /// - "RESET_SAFETY_LATCH"
    /// - "START_GRAPH"
    /// - "STOP_GRAPH"
    /// - "RELOAD_GRAPH"
    pub op: String,
    /// Optional parameters for future ops.
    #[serde(default)]
    pub parameters: serde_json::Value,
    pub requested_at_unix_ms: u64,
}

/// Core fault/incident message intended for tools/UIs/notification systems.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CoreFault {
    pub schema: String,
    pub room_id: String,
    /// Machine-readable identifier (e.g. "BROKER_OUTAGE").
    pub kind: String,
    /// Severity level: "INFO" | "WARN" | "CRITICAL"
    pub severity: String,
    pub message: String,
    pub observed_at_unix_ms: u64,
    #[serde(default)]
    pub details: serde_json::Value,
}

/// Core status snapshot intended for tools/UIs/health dashboards.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CoreStatus {
    pub schema: String,
    pub room_id: String,
    pub uptime_ms: u64,
    pub tick_ms: u64,
    pub dry_run: bool,
    pub dispatch_enabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub dispatch_paused_reason: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub broker_outage_since_unix_ms: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub safety_latched_since_unix_ms: Option<u64>,
    #[serde(default = "default_safety_state_safe")]
    pub room_safety: SafetyState,
    pub device_count: u64,
    pub offline_device_count: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub graph_active_node: Option<String>,
    /// Optional richer graph state for UIs/tools (v8).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub graph_active_nodes: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub graph_version: Option<i64>,
    pub observed_at_unix_ms: u64,
}

fn default_safety_state_safe() -> SafetyState {
    SafetyState {
        kind: SafetyStateKind::Safe,
        reason_code: None,
        latched: false,
    }
}

fn default_safety_class_non_critical() -> SafetyClass {
    SafetyClass::NonCritical
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct OscCue {
    pub schema: String,
    pub room_id: String,
    pub cue_id: String,
    pub correlation_id: Uuid,
    pub address: String,
    #[serde(default)]
    pub args: Vec<OscArg>,
    pub issued_at_unix_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type", content = "value", rename_all = "SCREAMING_SNAKE_CASE")]
pub enum OscArg {
    Int(i32),
    Float(f32),
    String(String),
    Bool(bool),
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum OscAckStatus {
    Sent,
    Failed,
}
