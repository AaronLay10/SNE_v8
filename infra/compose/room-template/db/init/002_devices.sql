-- Sentient v8 device registry (room-local).
--
-- This table allows the core (and later UIs) to understand which devices are
-- safety-critical and whether they are enabled.

CREATE TABLE IF NOT EXISTS devices (
  device_id TEXT PRIMARY KEY,
  safety_class TEXT NOT NULL CHECK (safety_class IN ('CRITICAL','NON_CRITICAL')),
  enabled BOOLEAN NOT NULL DEFAULT TRUE,
  notes TEXT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS devices_safety_class_idx ON devices (safety_class);
CREATE INDEX IF NOT EXISTS devices_enabled_idx ON devices (enabled);

