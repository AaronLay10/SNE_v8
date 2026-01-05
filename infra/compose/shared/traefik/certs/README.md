# Traefik TLS Certificates (Generated)

This directory is mounted into the `traefik` container at `/etc/traefik/certs`.

Expected files (generated on the host):

- `sentient.crt` (server certificate; should include SANs for `*.sentientengine.ai` and `*.{room}.sentientengine.ai`)
- `sentient.key` (server private key)
- `sentient-root-ca.crt` (root CA certificate to install on operator/admin devices)

Generate with:

`scripts/tls-bootstrap-internal-ca.sh`
