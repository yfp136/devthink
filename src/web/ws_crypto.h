// SHA-1 + Base64 编码（WebSocket 握手专用，RFC 6455 §1.3）
// SHA-1：手写 FIPS 180-4；Base64：标准 RFC 4648
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sm {
namespace web {

// SHA-1：输入字节串，输出 20 字节摘要
std::vector<uint8_t> sha1(const uint8_t* data, size_t len);
inline std::vector<uint8_t> sha1(const std::string& s) {
  return sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// Base64 编码：输入字节串，输出 ASCII 字符串
std::string base64_encode(const uint8_t* data, size_t len);
inline std::string base64_encode(const std::vector<uint8_t>& v) {
  return base64_encode(v.data(), v.size());
}
inline std::string base64_encode(const std::string& s) {
  return base64_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// WebSocket Accept 值：Base64(SHA1(key + magic_guid))
std::string ws_accept_key(const std::string& client_key);

}  // namespace web
}  // namespace sm
