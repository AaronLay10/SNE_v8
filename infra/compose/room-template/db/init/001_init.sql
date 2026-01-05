-- Sentient v8 (room-local) schema bootstrap.

CREATE EXTENSION IF NOT EXISTS timescaledb;

CREATE TABLE IF NOT EXISTS events (
  id BIGSERIAL NOT NULL,
  room_id TEXT NOT NULL,
  device_id TEXT NULL,
  topic TEXT NOT NULL,
  kind TEXT NOT NULL,
  observed_at TIMESTAMPTZ NOT NULL,
  payload JSONB NOT NULL,
  inserted_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  -- Timescale hypertables require that unique indexes include the partitioning column.
  PRIMARY KEY (observed_at, id)
);

-- Optional: convert to hypertable for time-series efficiency (safe if Timescale is present).
DO $$
BEGIN
  PERFORM create_hypertable('events', 'observed_at', if_not_exists => TRUE);
EXCEPTION
  WHEN undefined_function THEN
    -- Timescale not available; keep as a normal table.
    NULL;
END$$;

CREATE INDEX IF NOT EXISTS events_room_time_idx ON events (room_id, observed_at DESC);
CREATE INDEX IF NOT EXISTS events_device_time_idx ON events (room_id, device_id, observed_at DESC);
