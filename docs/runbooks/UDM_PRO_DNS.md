# Runbook — UDM Pro DNS Entries (Per Room) (v8)

Sentient v8 uses **per-room broker hostnames**:

- `mqtt.<room>.sentientengine.ai` (example: `mqtt.clockwork.sentientengine.ai`)

This runbook describes how to host those DNS entries on a **UDM Pro**.

## Goal

For each room VLAN/network, controllers should resolve:

- `mqtt.<that_room>.sentientengine.ai` → the room’s MQTT broker endpoint (R710 IP on that VLAN)

Optionally, on your admin LAN / management VLAN, tools should resolve:

- `api.<room>.sentientengine.ai` → Traefik on the R710 (routes to the room-scoped `sentient-api`)

## Required UDM Pro Settings

Per room VLAN/network:

- Ensure DHCP hands out the **UDM Pro (or your chosen DNS server)** as the DNS server for that network.
- Ensure clients in that VLAN can resolve `sentientengine.ai` locally (UDM “local DNS record” behavior).

## Per-Room DNS Records

Create one DNS A-record per room:

- Name: `mqtt.<room>.sentientengine.ai`
- Type: `A`
- Value: `<room_vlan_r710_ip>`

Example:

- `mqtt.clockwork.sentientengine.ai` → `192.168.30.2` (if R710 is `.2` on VLAN 30)

Current room VLANs/subnets:

- Clockwork: VLAN 30 (`192.168.30.0/24`)
- Requiem: VLAN 40 (`192.168.40.0/24`)
- Pharaohs: VLAN 50 (`192.168.50.0/24`)
- Fly-Tanic: VLAN 60 (`192.168.60.0/24`)

R710 room VLAN IPs (approved):

- Clockwork: `192.168.30.2`
- Requiem: `192.168.40.2`
- Pharaohs: `192.168.50.2`
- Fly-Tanic: `192.168.60.2`

## Warm Standby Notes (Manual)

If you use warm standby with manual promotion and want controllers to follow standby:

- Keep controllers pointed at `mqtt.<room>.sentientengine.ai`
- During promotion, manually repoint the UDM Pro DNS A-record to the standby broker endpoint (or VIP), **after** verifying safety.

See `docs/runbooks/WARM_STANDBY_PROMOTION.md`.
