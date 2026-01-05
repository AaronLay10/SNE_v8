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
  const [resetId, setResetId] = useState<string>("");
  const [users, setUsers] = useState<any[] | null>(null);
  const [newUser, setNewUser] = useState({ username: "", password: "", role: "TECH" });
  const [pwReset, setPwReset] = useState({ username: "", password: "" });

  async function doLogin() {
    setMessage("");
    const res = await postJson<{ access_token: string }>(
      `${settings.authBaseUrl}/v8/auth/login`,
      { username, password },
    );
    saveToken(res.access_token);
    setToken(res.access_token);
  }

  async function refreshUsers() {
    setMessage("");
    const u = await getJson<any[]>(`${settings.authBaseUrl}/v8/auth/users`, token);
    setUsers(u);
  }

  async function createUser() {
    setMessage("");
    await postJson(
      `${settings.authBaseUrl}/v8/auth/users`,
      { username: newUser.username, password: newUser.password, role: newUser.role },
      token,
    );
    await refreshUsers();
  }

  async function setUserEnabled(u: string, enabled: boolean) {
    setMessage("");
    await postJson(
      `${settings.authBaseUrl}/v8/auth/users/${encodeURIComponent(u)}/enabled`,
      { enabled },
      token,
    );
    await refreshUsers();
  }

  async function resetPassword() {
    setMessage("");
    await postJson(
      `${settings.authBaseUrl}/v8/auth/users/${encodeURIComponent(pwReset.username)}/password`,
      { password: pwReset.password },
      token,
    );
    setPwReset({ username: "", password: "" });
    setMessage("Password updated.");
  }

  async function refreshCoreStatus() {
    setMessage("");
    const s = await getJson<any>(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/core/status`,
      token,
    );
    setStatus(s);
  }

  async function requestSafetyReset() {
    setMessage("");
    const res = await postJson<{ reset_id: string }>(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/safety/reset/request`,
      { reason: "tech-ui request" },
      token,
    );
    setResetId(res.reset_id);
  }

  async function confirmSafetyReset() {
    setMessage("");
    await postJson(
      `${settings.roomApiBaseUrl}/v8/room/${settings.roomId}/safety/reset/confirm`,
      { reset_id: resetId },
      token,
    );
  }

  return (
    <div style={{ fontFamily: "ui-sans-serif, system-ui", padding: 24, maxWidth: 900 }}>
      <h1>Sentient Tech UI (v8)</h1>
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
            <input style={{ width: "100%" }} value={username} onChange={(e) => setUsername(e.target.value)} />
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
          <button onClick={() => doLogin().catch((e) => setMessage(String(e)))}>Login</button>
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
        <legend>Safety Reset (Dual Confirm)</legend>
        <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
          <button onClick={() => requestSafetyReset().catch((e) => setMessage(String(e)))}>
            Request reset
          </button>
          <input
            style={{ flex: 1 }}
            placeholder="reset_id"
            value={resetId}
            onChange={(e) => setResetId(e.target.value)}
          />
          <button onClick={() => confirmSafetyReset().catch((e) => setMessage(String(e)))}>
            Confirm reset
          </button>
        </div>
        <p style={{ color: "#555" }}>
          Requires TECH/ADMIN and room devices must be SAFE; confirm must occur within 60 seconds.
        </p>
      </fieldset>

      {claims?.role === "ADMIN" ? (
        <fieldset style={{ padding: 12, marginTop: 16 }}>
          <legend>Users (ADMIN)</legend>
          <div style={{ display: "flex", gap: 8 }}>
            <button onClick={() => refreshUsers().catch((e) => setMessage(String(e)))}>
              Refresh users
            </button>
            <button
              onClick={() => setUsers(null)}
              style={{ background: "#eee", border: "1px solid #ccc" }}
            >
              Clear
            </button>
          </div>

          <div style={{ marginTop: 12 }}>
            <h3 style={{ margin: "12px 0 8px" }}>Create user</h3>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 140px", gap: 8 }}>
              <input
                placeholder="username"
                value={newUser.username}
                onChange={(e) => setNewUser({ ...newUser, username: e.target.value })}
              />
              <input
                placeholder="password"
                type="password"
                value={newUser.password}
                onChange={(e) => setNewUser({ ...newUser, password: e.target.value })}
              />
              <select
                value={newUser.role}
                onChange={(e) => setNewUser({ ...newUser, role: e.target.value })}
              >
                <option value="ADMIN">ADMIN</option>
                <option value="TECH">TECH</option>
                <option value="GM">GM</option>
                <option value="VIEWER">VIEWER</option>
              </select>
            </div>
            <button onClick={() => createUser().catch((e) => setMessage(String(e)))} style={{ marginTop: 8 }}>
              Create
            </button>
          </div>

          <div style={{ marginTop: 12 }}>
            <h3 style={{ margin: "12px 0 8px" }}>Reset password</h3>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8 }}>
              <input
                placeholder="username"
                value={pwReset.username}
                onChange={(e) => setPwReset({ ...pwReset, username: e.target.value })}
              />
              <input
                placeholder="new password"
                type="password"
                value={pwReset.password}
                onChange={(e) => setPwReset({ ...pwReset, password: e.target.value })}
              />
            </div>
            <button onClick={() => resetPassword().catch((e) => setMessage(String(e)))} style={{ marginTop: 8 }}>
              Update password
            </button>
          </div>

          <div style={{ marginTop: 12 }}>
            <h3 style={{ margin: "12px 0 8px" }}>User list</h3>
            <pre
              style={{
                background: "#111",
                color: "#ddd",
                padding: 12,
                marginTop: 12,
                overflow: "auto",
              }}
            >
              {users ? JSON.stringify(users, null, 2) : "No data"}
            </pre>
            {users?.length ? (
              <div style={{ marginTop: 8, display: "flex", flexDirection: "column", gap: 6 }}>
                {users.map((u) => (
                  <div key={u.username} style={{ display: "flex", alignItems: "center", gap: 8 }}>
                    <code style={{ minWidth: 180 }}>{u.username}</code>
                    <code style={{ minWidth: 80 }}>{u.role}</code>
                    <span style={{ minWidth: 80 }}>{u.enabled ? "ENABLED" : "DISABLED"}</span>
                    <button
                      onClick={() => setUserEnabled(u.username, !u.enabled).catch((e) => setMessage(String(e)))}
                    >
                      {u.enabled ? "Disable" : "Enable"}
                    </button>
                  </div>
                ))}
              </div>
            ) : null}
          </div>
        </fieldset>
      ) : null}

      {message ? <p style={{ marginTop: 16, color: "#b00" }}>{message}</p> : null}
    </div>
  );
}
