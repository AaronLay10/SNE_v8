import React, { useMemo, useState } from "react";
import { clearToken, loadSettings, loadToken, saveSettings, saveToken } from "./storage";
import { getJson, postJson } from "./http";

type Claims = { sub: string; role: string; exp: number; iat: number };
function decodeJwt(token: string): Claims | null {
  try {
    const [, payload] = token.split(".");
    if (!payload) return null;
    const json = atob(payload.replace(/-/g, "+").replace(/_/g, "/"));
    return JSON.parse(json) as Claims;
  } catch {
    return null;
  }
}

export function App() {
  const [settings, setSettings] = useState(loadSettings);
  const [token, setToken] = useState<string | null>(loadToken());
  const claims = useMemo(() => (token ? decodeJwt(token) : null), [token]);
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [status, setStatus] = useState<any>(null);
  const [message, setMessage] = useState<string>("");
  const [graphJson, setGraphJson] = useState<string>(
    '{\n  "schema": "v8",\n  "room_id": "room1",\n  "start": "boot",\n  "nodes": {\n    "boot": { "kind": "NOOP" }\n  }\n}\n',
  );
  const [graphVersions, setGraphVersions] = useState<Array<{ version: number; created_at_unix_ms: number }>>([]);
  const [activeGraph, setActiveGraph] = useState<{ active_version: number; activated_at_unix_ms: number } | null>(null);

  async function doLogin() {
    setMessage("");
    const res = await postJson<{ access_token: string }>(
      `${settings.authBaseUrl}/v8/auth/login`,
      { username, password },
    );
    saveToken(res.access_token);
    setToken(res.access_token);
  }

  async function refreshCoreStatus() {
    setMessage("");
    const s = await getJson<any>(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/core/status`,
      token,
    );
    setStatus(s);
  }

  async function refreshGraphs() {
    setMessage("");
    const versions = await getJson<Array<{ version: number; created_at_unix_ms: number }>>(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/graphs`,
      token,
    );
    setGraphVersions(versions);
    const active = await fetch(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/graphs/active`,
      { headers: token ? { Authorization: `Bearer ${token}` } : {} },
    );
    if (active.status === 204) {
      setActiveGraph(null);
    } else if (active.ok) {
      setActiveGraph((await active.json()) as any);
    } else {
      setActiveGraph(null);
    }
  }

  async function uploadGraph() {
    setMessage("");
    const parsed = JSON.parse(graphJson);
    const res = await postJson<{ version: number }>(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/graphs`,
      parsed,
      token,
    );
    setMessage(`Uploaded graph v${res.version}`);
    await refreshGraphs();
  }

  async function activate(version: number) {
    setMessage("");
    await postJson(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/graphs/activate`,
      { version },
      token,
    );
    setMessage(`Activated graph v${version} (core loads on restart for now)`);
    await refreshGraphs();
  }

  return (
    <div style={{ fontFamily: "ui-sans-serif, system-ui", padding: 24, maxWidth: 900 }}>
      <h1>Sentient Creative UI (v8)</h1>
      <p style={{ color: "#555" }}>
        Auth: {claims ? `${claims.sub} (${claims.role})` : "not logged in"}
      </p>

      <fieldset style={{ padding: 12 }}>
        <legend>Settings</legend>
        <label style={{ display: "block", marginBottom: 8 }}>
          Auth Base URL
          <input
            style={{ width: "100%" }}
            value={settings.authBaseUrl}
            onChange={(e) => setSettings({ ...settings, authBaseUrl: e.target.value })}
          />
        </label>
        <label style={{ display: "block", marginBottom: 8 }}>
          Room API Base URL
          <input
            style={{ width: "100%" }}
            value={settings.roomApiBaseUrl}
            onChange={(e) => setSettings({ ...settings, roomApiBaseUrl: e.target.value })}
          />
        </label>
        <label style={{ display: "block", marginBottom: 8 }}>
          Room ID
          <input
            style={{ width: "100%" }}
            value={settings.roomId}
            onChange={(e) => setSettings({ ...settings, roomId: e.target.value })}
          />
        </label>
        <button
          onClick={() => {
            saveSettings(settings);
            setMessage("Saved settings.");
          }}
        >
          Save Settings
        </button>
      </fieldset>

      {!token ? (
        <fieldset style={{ padding: 12, marginTop: 16 }}>
          <legend>Login</legend>
          <label style={{ display: "block", marginBottom: 8 }}>
            Username
            <input
              style={{ width: "100%" }}
              value={username}
              onChange={(e) => setUsername(e.target.value)}
            />
          </label>
          <label style={{ display: "block", marginBottom: 8 }}>
            Password
            <input
              style={{ width: "100%" }}
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
            />
          </label>
          <button onClick={() => doLogin().catch((e) => setMessage(String(e)))}>
            Login
          </button>
        </fieldset>
      ) : (
        <div style={{ marginTop: 16 }}>
          <button
            onClick={() => {
              clearToken();
              setToken(null);
            }}
          >
            Logout
          </button>
        </div>
      )}

      <fieldset style={{ padding: 12, marginTop: 16 }}>
        <legend>Room Status</legend>
        <button onClick={() => refreshCoreStatus().catch((e) => setMessage(String(e)))}>
          Refresh core/status
        </button>
        <pre style={{ background: "#111", color: "#ddd", padding: 12, marginTop: 12, overflow: "auto" }}>
          {status ? JSON.stringify(status, null, 2) : "No data"}
        </pre>
      </fieldset>

      <fieldset style={{ padding: 12, marginTop: 16 }}>
        <legend>Graphs (placeholder)</legend>
        <p style={{ color: "#555" }}>Upload a graph JSON to the room DB and activate a version.</p>
        <div style={{ display: "flex", gap: 8, marginBottom: 8 }}>
          <button onClick={() => uploadGraph().catch((e) => setMessage(String(e)))}>
            Upload graph
          </button>
          <button onClick={() => refreshGraphs().catch((e) => setMessage(String(e)))}>
            Refresh versions
          </button>
          <span style={{ color: "#555" }}>
            Active: {activeGraph ? `v${activeGraph.active_version}` : "none"}
          </span>
        </div>
        <textarea
          style={{ width: "100%", minHeight: 220, fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace" }}
          value={graphJson}
          onChange={(e) => setGraphJson(e.target.value)}
        />
        <div style={{ marginTop: 12 }}>
          <strong>Versions</strong>
          <ul>
            {graphVersions.map((v) => (
              <li key={v.version}>
                v{v.version}{" "}
                <button onClick={() => activate(v.version).catch((e) => setMessage(String(e)))}>
                  Activate
                </button>
              </li>
            ))}
          </ul>
        </div>
      </fieldset>

      {message ? <p style={{ marginTop: 16, color: "#b00" }}>{message}</p> : null}
    </div>
  );
}
