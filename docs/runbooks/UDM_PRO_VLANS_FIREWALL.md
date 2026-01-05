# Runbook — UDM Pro VLANs + Firewall (Per Room) (v8)

Sentient v8 assumes **one VLAN per room** and strict isolation:

- Room controllers can only reach **their own room stack** (MQTT/DB/core/osc-bridge) on the R710.
- Room VLANs cannot talk to each other.
- Admin/Tech network can reach all room stacks for maintenance and dashboards.

## 1) Create Networks (One Per Room)

For each room, create a VLAN network on the UDM Pro:

- Name: `<room>` (example: `clockwork`)
- VLAN ID: `<unique>`
- Subnet: `<room_subnet>/24`
- DHCP: enabled for controllers (recommended)
- DNS server: UDM Pro (so local DNS records for `mqtt.<room>.sentientengine.ai` work)

Current room VLANs (as configured):

| Room | VLAN | Subnet |
|---|---:|---|
| Clockwork | 30 | `192.168.30.0/24` |
| Requiem | 40 | `192.168.40.0/24` |
| Pharaohs | 50 | `192.168.50.0/24` |
| Fly-Tanic | 60 | `192.168.60.0/24` |

## 2) Assign R710 an IP in Each Room VLAN

The R710 must have a reachable IP in each room VLAN (either via VLAN-tagged trunk + VLAN interfaces or dedicated NICs).

Document per room:

- R710 IP in VLAN: `<room_vlan_r710_ip>`
- MQTT endpoint: `<room_vlan_r710_ip>:1883`
- DB endpoint (optional admin only): `<room_vlan_r710_ip>:5432`

Recommended IP convention (approved):

- UDM gateway: `.1` (typical)
- R710 room VLAN IP: `.2` (static)
- Reserve `.10`+ for controllers and other room devices (DHCP range)

Example (Clockwork):

- UDM gateway: `192.168.30.1`
- R710: `192.168.30.2`
- MQTT broker: `192.168.30.2:1883`

Room-by-room R710 IP plan:

| Room | VLAN | R710 IP |
|---|---:|---|
| Clockwork | 30 | `192.168.30.2` |
| Requiem | 40 | `192.168.40.2` |
| Pharaohs | 50 | `192.168.50.2` |
| Fly-Tanic | 60 | `192.168.60.2` |

## 3) Required Firewall Rules (High Level)

### Room VLAN → Room Stack (Allow)

Allow room VLAN clients to reach the R710 room stack:

- UDP/TCP 1883 (MQTT) to the R710’s room VLAN IP
- UDP 123 (NTP) to the R710 (if using `docs/runbooks/TIME_SYNC.md`)

Optional (if you want controllers to reach anything else, keep it explicit).

Recommended strict additions:

- Allow DNS to the UDM Pro in that VLAN (UDP/TCP 53) so `mqtt.<room>.sentientengine.ai` resolves.
- Deny all other egress from controllers.

### Room VLAN → Other Room VLANs (Deny)

Deny inter-room traffic:

- Any room VLAN → any other room VLAN (all ports/protocols)

### Room VLAN → Admin Network (Deny by default)

Deny room VLAN → admin VLAN by default (except NTP/DNS if needed).

### Admin Network → Room VLAN (Allow limited)

Allow admin/tech workstations to reach room stacks for ops:

- TCP 1883 (MQTT) to each room broker (for commissioning/tools)
- TCP 5432 (DB) only from trusted admin hosts (or require jump host)
- TCP 80/443 to Traefik/admin UIs (later)

## 3.1 Recommended UDM Pro Rule Set (Concrete)

UDM Pro rules are typically added under **Firewall & Security → Firewall Rules → LAN IN** (inter-VLAN).

Create these address groups (examples):

- `SENTIENT_R710_ROOM_IPS`: `192.168.30.2`, `192.168.40.2`, `192.168.50.2`, `192.168.60.2`
- `SENTIENT_ROOM_VLANS`: `192.168.30.0/24`, `192.168.40.0/24`, `192.168.50.0/24`, `192.168.60.0/24`
- `SENTIENT_ROOM_GWS`: `192.168.30.1`, `192.168.40.1`, `192.168.50.1`, `192.168.60.1`
- `SENTIENT_ADMIN_NETS`: (your VLAN 2 + VLAN 4 subnets)

Create these port groups:

- `SENTIENT_MQTT`: TCP `1883`
- `SENTIENT_NTP`: UDP `123`
- `SENTIENT_DNS`: UDP/TCP `53`

Rules (top to bottom):

1) **Allow** `SENTIENT_ROOM_VLANS` → `SENTIENT_ROOM_GWS` on `SENTIENT_DNS`
2) **Allow** `SENTIENT_ROOM_VLANS` → `SENTIENT_R710_ROOM_IPS` on `SENTIENT_MQTT`
3) **Allow** `SENTIENT_ROOM_VLANS` → `SENTIENT_R710_ROOM_IPS` on `SENTIENT_NTP`
4) **Deny** `SENTIENT_ROOM_VLANS` → `SENTIENT_ADMIN_NETS` (all ports)
5) **Deny** `SENTIENT_ROOM_VLANS` → `SENTIENT_ROOM_VLANS` (all ports) *(inter-room isolation)*
6) **Deny** `SENTIENT_ROOM_VLANS` → RFC1918 (or “LAN”) (all ports) *(catch-all internal deny; exclude needed allow rules above)*

Optional commissioning rule (temporary; restrict to specific admin devices):

- **Allow** `SENTIENT_ADMIN_NETS` → `SENTIENT_R710_ROOM_IPS` on `SENTIENT_MQTT` (for `mosquitto_pub/sub`, provisioning, etc.)

## 4) DNS Records

Per room, create:

- `mqtt.<room>.sentientengine.ai` → `<room_vlan_r710_ip>`

See `docs/runbooks/UDM_PRO_DNS.md`.
