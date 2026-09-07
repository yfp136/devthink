#include "core/util.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <vector>

namespace sm {

std::int64_t now_monotonic_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {
long tz_offset_seconds(const std::tm& local) {
#if defined(_WIN32)
  _tzset();
  long off = -_timezone;
  return off;
#else
  return static_cast<long>(local.tm_gmtoff);
#endif
}
}  // namespace

std::string iso8601_now() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(ms));

  long off = tz_offset_seconds(local);
  const char sign = (off >= 0) ? '+' : '-';
  if (off < 0) off = -off;
  const long hh = off / 3600, mm = (off % 3600) / 60;
  std::string out(buf);
  out += sign;
  out += (hh < 10 ? "0" : "") + std::to_string(hh) + ":";
  out += (mm < 10 ? "0" : "") + std::to_string(mm);
  return out;
}

std::string uuid_hex32() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  uint64_t a = rng(), b = rng();
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                static_cast<unsigned long long>(a),
                static_cast<unsigned long long>(b));
  return std::string(buf, 32);
}

// ---- SHA-256 实现（公有域风格，紧凑标准实现）----
namespace {

struct Sha256Ctx {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t  buffer[64];
  size_t   buflen = 0;
};

constexpr uint32_t K[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256_init(Sha256Ctx& ctx) {
  ctx.state[0] = 0x6a09e667u; ctx.state[1] = 0xbb67ae85u;
  ctx.state[2] = 0x3c6ef372u; ctx.state[3] = 0xa54ff53au;
  ctx.state[4] = 0x510e527fu; ctx.state[5] = 0x9b05688cu;
  ctx.state[6] = 0x1f83d9abu; ctx.state[7] = 0x5be0cd19u;
  ctx.bitlen = 0;
  ctx.buflen = 0;
}

void sha256_block(Sha256Ctx& ctx, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
           (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
  uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + S1 + ch + K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
  ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
  ctx.bitlen += uint64_t(len) * 8;
  while (len > 0) {
    const size_t take = (len > 64 - ctx.buflen) ? (64 - ctx.buflen) : len;
    std::memcpy(ctx.buffer + ctx.buflen, data, take);
    ctx.buflen += take; data += take; len -= take;
    if (ctx.buflen == 64) { sha256_block(ctx, ctx.buffer); ctx.buflen = 0; }
  }
}

std::string sha256_final(Sha256Ctx& ctx) {
  // 记录原始消息位长（填充不得计入 bitlen，标准 SHA-256 padding）
  const uint64_t total_bits = ctx.bitlen;

  // 1) 追加 0x80
  ctx.buffer[ctx.buflen++] = 0x80;

  // 2) 若放不下 8 字节长度域，则补零到 64 并压缩一个块
  if (ctx.buflen > 56) {
    while (ctx.buflen < 64) ctx.buffer[ctx.buflen++] = 0;
    sha256_block(ctx, ctx.buffer);
    ctx.buflen = 0;
  }

  // 3) 补零到 56 字节
  while (ctx.buflen < 56) ctx.buffer[ctx.buflen++] = 0;

  // 4) 写入原始位长（大端 64bit）并压缩末块
  for (int i = 0; i < 8; ++i)
    ctx.buffer[ctx.buflen++] = uint8_t(total_bits >> (56 - i * 8));
  sha256_block(ctx, ctx.buffer);

  char hex[65];
  int pos = 0;
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + pos, 9, "%08x", ctx.state[i]);
    pos += 8;
  }
  return std::string(hex, 64);
}

}  // namespace

std::string sha256_hex(const std::string& data) {
  Sha256Ctx ctx;
  sha256_init(ctx);
  sha256_update(ctx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return sha256_final(ctx);
}

bool sha256_file_hex(const std::string& path, std::string& hex_out, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { err = "无法打开文件: " + path; return false; }
  Sha256Ctx ctx;
  sha256_init(ctx);
  char buf[1 << 16];
  while (in) {
    in.read(buf, sizeof(buf));
    const std::streamsize got = in.gcount();
    if (got > 0) sha256_update(ctx, reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(got));
  }
  if (!in.eof()) { err = "读取中断: " + path; return false; }
  hex_out = sha256_final(ctx);
  return true;
}

std::string pack_media_name(const std::string& sha256_hex_full, const std::string& original_name) {
  const std::string head = sha256_hex_full.size() >= 16 ? sha256_hex_full.substr(0, 16) : sha256_hex_full;
  return head + "_" + original_name;
}

}  // namespace sm
