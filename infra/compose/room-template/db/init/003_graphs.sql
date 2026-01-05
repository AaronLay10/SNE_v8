-- Sentient v8 graphs (room-local).

CREATE TABLE IF NOT EXISTS graphs (
  id BIGSERIAL PRIMARY KEY,
  room_id TEXT NOT NULL,
  version BIGINT NOT NULL,
  graph JSONB NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  UNIQUE (room_id, version)
);

CREATE TABLE IF NOT EXISTS graph_active (
  room_id TEXT PRIMARY KEY,
  active_version BIGINT NOT NULL,
  activated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS graphs_room_version_idx ON graphs (room_id, version DESC);

