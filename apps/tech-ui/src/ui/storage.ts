export type Settings = {
  authBaseUrl: string;
  roomApiBaseUrl: string;
  roomId: string;
};

const SETTINGS_KEY = "sentient.tech.settings.v1";
const TOKEN_KEY = "sentient.tech.jwt.v1";

export function loadSettings(): Settings {
  const raw = localStorage.getItem(SETTINGS_KEY);
  if (!raw) {
    return {
      authBaseUrl: "https://auth.sentientengine.ai",
      roomApiBaseUrl: "https://api.clockwork.sentientengine.ai",
      roomId: "clockwork",
    };
  }
  try {
    const parsed = JSON.parse(raw) as Partial<Settings>;
    return {
      authBaseUrl: parsed.authBaseUrl || "https://auth.sentientengine.ai",
      roomApiBaseUrl:
        parsed.roomApiBaseUrl || "https://api.clockwork.sentientengine.ai",
      roomId: parsed.roomId || "clockwork",
    };
  } catch {
    return {
      authBaseUrl: "https://auth.sentientengine.ai",
      roomApiBaseUrl: "https://api.clockwork.sentientengine.ai",
      roomId: "clockwork",
    };
  }
}

export function saveSettings(s: Settings) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
}

export function loadToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}

export function saveToken(token: string) {
  localStorage.setItem(TOKEN_KEY, token);
}

export function clearToken() {
  localStorage.removeItem(TOKEN_KEY);
}
