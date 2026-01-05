#ifndef SENTIENT_CRYPTO_H
#define SENTIENT_CRYPTO_H

#include <Arduino.h>

namespace sentient_crypto {

struct Sha256Ctx {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t buffer[64];
  size_t buffer_len;
};

void sha256_init(Sha256Ctx &ctx);
void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx &ctx, uint8_t out[32]);

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t out[32]);

void bytes_to_hex_lower(const uint8_t *bytes, size_t len, char *out_hex, size_t out_len);
bool constant_time_eq_hex(const char *a, const char *b);
bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);

} // namespace sentient_crypto

#endif

