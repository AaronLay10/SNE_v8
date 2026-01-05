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

  async function pause() {
    setMessage("");
    await postJson(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/control`,
      { op: "PAUSE_DISPATCH", parameters: {} },
      token,
    );
  }

  async function resume() {
    setMessage("");
    await postJson(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/control`,
      { op: "RESUME_DISPATCH", parameters: {} },
      token,
    );
  }

  return (
    <div style={{ fontFamily: "ui-sans-serif, system-ui", padding: 24, maxWidth: 900 }}>
      <h1>Sentient Gamemaster UI (v8)</h1>
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
        <div style={{ marginTop: 16, display: "flex", gap: 8 }}>
          <button
            onClick={() => {
              clearToken();
              setToken(null);
            }}
          >
            Logout
          </button>
          <button onClick={() => pause().catch((e) => setMessage(String(e)))}>Pause</button>
          <button onClick={() => resume().catch((e) => setMessage(String(e)))}>Resume</button>
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

      {message ? <p style={{ marginTop: 16, color: "#b00" }}>{message}</p> : null}
    </div>
  );
}

