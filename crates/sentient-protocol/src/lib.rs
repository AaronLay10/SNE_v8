use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const SCHEMA_VERSION: &str = "v8";

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

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
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
