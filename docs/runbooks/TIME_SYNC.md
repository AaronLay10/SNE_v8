# Runbook — Time Sync (NTP) (v8)

Sentient v8 timing assumes the server clock is stable and that clients can sync to it.

This runbook configures the R710 host as:

- an NTP client (syncs to upstream), and
- an NTP server (serves time to room VLAN clients).

## Install and Configure Chrony (Recommended)

Run on the R710 host:

```bash
sudo ./scripts/host-ntp-chrony.sh
```

The script will:

- Install `chrony`
- Disable `systemd-timesyncd` (to avoid two NTP clients)
- Allow LAN subnets (RFC1918) to query NTP (tighten to exact VLAN CIDRs later)
- Enable and start `chrony`

## Verify

```bash
timedatectl status
chronyc tracking
chronyc sources -v
ss -ulpn | rg ':123\\b'
```

## Notes

- Once the room VLAN CIDRs are finalized, update `/etc/chrony/chrony.conf` to `allow` only the required subnets.

