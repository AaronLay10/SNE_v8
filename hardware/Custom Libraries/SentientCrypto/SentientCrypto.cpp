#include "SentientCrypto.h"

namespace sentient_crypto {

static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t big0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
static inline uint32_t big1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
static inline uint32_t sml0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
static inline uint32_t sml1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

static uint32_t load_be_u32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static void store_be_u32(uint32_t v, uint8_t *p) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

static void sha256_transform(Sha256Ctx &ctx, const uint8_t block[64]) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; i++) {
    w[i] = load_be_u32(block + i * 4);
  }
  for (size_t i = 16; i < 64; i++) {
    w[i] = sml1(w[i - 2]) + w[i - 7] + sml0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = ctx.state[0];
  uint32_t b = ctx.state[1];
  uint32_t c = ctx.state[2];
  uint32_t d = ctx.state[3];
  uint32_t e = ctx.state[4];
  uint32_t f = ctx.state[5];
  uint32_t g = ctx.state[6];
  uint32_t h = ctx.state[7];

  for (size_t i = 0; i < 64; i++) {
    uint32_t t1 = h + big1(e) + ch(e, f, g) + K[i] + w[i];
    uint32_t t2 = big0(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx.state[0] += a;
  ctx.state[1] += b;
  ctx.state[2] += c;
  ctx.state[3] += d;
  ctx.state[4] += e;
  ctx.state[5] += f;
  ctx.state[6] += g;
  ctx.state[7] += h;
}

void sha256_init(Sha256Ctx &ctx) {
  ctx.state[0] = 0x6a09e667u;
  ctx.state[1] = 0xbb67ae85u;
  ctx.state[2] = 0x3c6ef372u;
  ctx.state[3] = 0xa54ff53au;
  ctx.state[4] = 0x510e527fu;
  ctx.state[5] = 0x9b05688cu;
  ctx.state[6] = 0x1f83d9abu;
  ctx.state[7] = 0x5be0cd19u;
  ctx.bitlen = 0;
  ctx.buffer_len = 0;
}

void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
  ctx.bitlen += uint64_t(len) * 8u;
  while (len > 0) {
    size_t to_copy = 64 - ctx.buffer_len;
    if (to_copy > len) {
      to_copy = len;
    }
    memcpy(ctx.buffer + ctx.buffer_len, data, to_copy);
    ctx.buffer_len += to_copy;
    data += to_copy;
    len -= to_copy;
    if (ctx.buffer_len == 64) {
      sha256_transform(ctx, ctx.buffer);
      ctx.buffer_len = 0;
    }
  }
}

void sha256_final(Sha256Ctx &ctx, uint8_t out[32]) {
  uint8_t pad[64] = {0};
  pad[0] = 0x80;

  size_t pad_len = (ctx.buffer_len < 56) ? (56 - ctx.buffer_len) : (64 + 56 - ctx.buffer_len);
  sha256_update(ctx, pad, pad_len);

  uint8_t len_be[8];
  uint64_t bits = ctx.bitlen;
  for (int i = 0; i < 8; i++) {
    len_be[7 - i] = uint8_t(bits >> (i * 8));
  }
  sha256_update(ctx, len_be, 8);

  for (size_t i = 0; i < 8; i++) {
    store_be_u32(ctx.state[i], out + i * 4);
  }
}

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t out[32]) {
  uint8_t k0[64];
  memset(k0, 0, sizeof(k0));

  if (key_len > 64) {
    Sha256Ctx h;
    sha256_init(h);
    sha256_update(h, key, key_len);
    uint8_t digest[32];
    sha256_final(h, digest);
    memcpy(k0, digest, 32);
    memset(digest, 0, sizeof(digest));
  } else {
    memcpy(k0, key, key_len);
  }

  uint8_t ipad[64];
  uint8_t opad[64];
  for (size_t i = 0; i < 64; i++) {
    ipad[i] = k0[i] ^ 0x36;
    opad[i] = k0[i] ^ 0x5c;
  }

  Sha256Ctx inner;
  sha256_init(inner);
  sha256_update(inner, ipad, 64);
  sha256_update(inner, data, len);
  uint8_t inner_digest[32];
  sha256_final(inner, inner_digest);

  Sha256Ctx outer;
  sha256_init(outer);
  sha256_update(outer, opad, 64);
  sha256_update(outer, inner_digest, 32);
  sha256_final(outer, out);

  memset(k0, 0, sizeof(k0));
  memset(ipad, 0, sizeof(ipad));
  memset(opad, 0, sizeof(opad));
  memset(inner_digest, 0, sizeof(inner_digest));
}

void bytes_to_hex_lower(const uint8_t *bytes, size_t len, char *out_hex, size_t out_len) {
  static const char *hex = "0123456789abcdef";
  if (out_len < (len * 2 + 1)) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    out_hex[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
    out_hex[i * 2 + 1] = hex[bytes[i] & 0x0f];
  }
  out_hex[len * 2] = '\0';
}

static uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') return uint8_t(c - '0');
  if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return uint8_t(c - 'A' + 10);
  return 0xff;
}

bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  if (!hex) return false;
  size_t n = strlen(hex);
  if (n != out_len * 2) return false;
  for (size_t i = 0; i < out_len; i++) {
    uint8_t hi = hex_nibble(hex[i * 2]);
    uint8_t lo = hex_nibble(hex[i * 2 + 1]);
    if (hi == 0xff || lo == 0xff) return false;
    out[i] = uint8_t((hi << 4) | lo);
  }
  return true;
}

bool constant_time_eq_hex(const char *a, const char *b) {
  if (!a || !b) return false;
  size_t al = strlen(a);
  size_t bl = strlen(b);
  if (al != bl) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < al; i++) {
    diff |= uint8_t(a[i] ^ b[i]);
  }
  return diff == 0;
}

} // namespace sentient_crypto

