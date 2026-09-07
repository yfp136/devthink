#include "web/ws_crypto.h"

#include <cstring>

namespace sm {
namespace web {

// ---- SHA-1 (FIPS 180-4) ----
namespace {

struct Sha1Ctx {
  uint32_t h[5];
  uint64_t bitlen;
  uint8_t buffer[64];
  size_t buflen;
};

void sha1_init(Sha1Ctx& c) {
  c.h[0] = 0x67452301; c.h[1] = 0xEFCDAB89; c.h[2] = 0x98BADCFE;
  c.h[3] = 0x10325476; c.h[4] = 0xC3D2E1F0;
  c.bitlen = 0; c.buflen = 0;
}

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void sha1_block(Sha1Ctx& c, const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
           (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
  }
  for (int i = 16; i < 80; ++i)
    w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

  uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20)      { f = (b & cc) | (~b & d);       k = 0x5A827999; }
    else if (i < 40) { f = b ^ cc ^ d;                k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
    else             { f = b ^ cc ^ d;                k = 0xCA62C1D6; }
    uint32_t t = rotl(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = rotl(b, 30); b = a; a = t;
  }
  c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d; c.h[4] += e;
}

}  // namespace

std::vector<uint8_t> sha1(const uint8_t* data, size_t len) {
  Sha1Ctx c;
  sha1_init(c);
  c.bitlen = uint64_t(len) * 8;

  // 处理完整块
  while (len >= 64) {
    sha1_block(c, data);
    data += 64; len -= 64;
  }
  // 拷贝剩余 + padding
  uint8_t block[64] = {0};
  std::memcpy(block, data, len);
  block[len] = 0x80;
  if (len >= 56) {
    sha1_block(c, block);
    std::memset(block, 0, 64);
  }
  for (int i = 0; i < 8; ++i)
    block[56 + i] = uint8_t(c.bitlen >> (56 - i * 8));
  sha1_block(c, block);

  std::vector<uint8_t> out(20);
  for (int i = 0; i < 5; ++i) {
    out[i*4]   = uint8_t(c.h[i] >> 24);
    out[i*4+1] = uint8_t(c.h[i] >> 16);
    out[i*4+2] = uint8_t(c.h[i] >> 8);
    out[i*4+3] = uint8_t(c.h[i]);
  }
  return out;
}

// ---- Base64 ----
std::string base64_encode(const uint8_t* data, size_t len) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = uint32_t(data[i]) << 16;
    if (i + 1 < len) n |= uint32_t(data[i+1]) << 8;
    if (i + 2 < len) n |= uint32_t(data[i+2]);
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? tbl[n & 0x3F] : '=';
  }
  return out;
}

// ---- WebSocket Accept ----
std::string ws_accept_key(const std::string& client_key) {
  static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  return base64_encode(sha1(client_key + kGuid));
}

}  // namespace web
}  // namespace sm
