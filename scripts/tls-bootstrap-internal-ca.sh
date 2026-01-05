#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

WORKDIR="${WORKDIR:-${repo_root}/infra/pki}"
CERT_DIR="${CERT_DIR:-${repo_root}/infra/compose/shared/traefik/certs}"

# Comma-separated list of wildcard DNS names to include as SANs.
# Example: "*.sentientengine.ai,*.clockwork.sentientengine.ai"
TLS_WILDCARDS="${TLS_WILDCARDS:-*.sentientengine.ai}"

CA_KEY="${WORKDIR}/sentient-root-ca.key"
CA_CRT="${WORKDIR}/sentient-root-ca.crt"
SERVER_KEY="${WORKDIR}/sentient.key"
SERVER_CSR="${WORKDIR}/sentient.csr"
SERVER_CRT="${WORKDIR}/sentient.crt"

mkdir -p "${WORKDIR}" "${CERT_DIR}"

if [[ ! -f "${CA_KEY}" || ! -f "${CA_CRT}" ]]; then
  echo "Generating root CA in ${WORKDIR} ..."
  openssl genrsa -out "${CA_KEY}" 4096 >/dev/null 2>&1
  openssl req -x509 -new -nodes -key "${CA_KEY}" -sha256 -days 3650 \
    -subj "/C=US/ST=TX/O=Sentient Engine/OU=SNE v8/CN=Sentient Root CA" \
    -out "${CA_CRT}" >/dev/null 2>&1
else
  echo "Root CA exists: ${CA_CRT}"
fi

IFS=',' read -r -a SAN_LIST <<<"${TLS_WILDCARDS}"
SAN_JOINED=""
for name in "${SAN_LIST[@]}"; do
  name="$(echo "${name}" | xargs)"
  [[ -z "${name}" ]] && continue
  if [[ -z "${SAN_JOINED}" ]]; then
    SAN_JOINED="DNS:${name}"
  else
    SAN_JOINED="${SAN_JOINED},DNS:${name}"
  fi
done
if [[ -z "${SAN_JOINED}" ]]; then
  echo "TLS_WILDCARDS produced no SANs" >&2
  exit 2
fi

echo "Generating server cert with SANs: ${TLS_WILDCARDS}"
openssl genrsa -out "${SERVER_KEY}" 2048 >/dev/null 2>&1

TMP_CONF="$(mktemp)"
cat >"${TMP_CONF}" <<EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
C = US
ST = TX
O = Sentient Engine
OU = SNE v8
CN = sentient.local

[v3_req]
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = ${SAN_JOINED}
EOF

openssl req -new -key "${SERVER_KEY}" -out "${SERVER_CSR}" -config "${TMP_CONF}" >/dev/null 2>&1
openssl x509 -req -in "${SERVER_CSR}" -CA "${CA_CRT}" -CAkey "${CA_KEY}" -CAcreateserial \
  -out "${SERVER_CRT}" -days 825 -sha256 -extensions v3_req -extfile "${TMP_CONF}" >/dev/null 2>&1
rm -f "${TMP_CONF}" "${SERVER_CSR}" "${WORKDIR}/sentient-root-ca.srl" 2>/dev/null || true

install -m 0644 "${SERVER_CRT}" "${CERT_DIR}/sentient.crt"
install -m 0600 "${SERVER_KEY}" "${CERT_DIR}/sentient.key"
install -m 0644 "${CA_CRT}" "${CERT_DIR}/sentient-root-ca.crt"

echo "Wrote:"
echo "  ${CERT_DIR}/sentient.crt"
echo "  ${CERT_DIR}/sentient.key"
echo "  ${CERT_DIR}/sentient-root-ca.crt"
echo
echo "Next:"
echo "  - Install ${CERT_DIR}/sentient-root-ca.crt on operator/admin devices as a trusted root"
echo "  - Restart shared stack: (from infra/compose/shared) docker compose up -d --build"
