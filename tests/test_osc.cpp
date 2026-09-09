// OSC 协议适配器单元测试
// 验证 OSC 1.0 编解码逻辑（不依赖网络，纯内存往返）
#include "protocols/osc_adapter.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace sm::proto;

static void test_osc_encode_decode_simple() {
  OscAdapter osc;

  // 编码：/test/address,if s="hello"
  std::vector<OscArg> args;
  OscArg a1; a1.type = 'i'; a1.i = 42;
  OscArg a2; a2.type = 'f'; a2.f = 3.14f;
  OscArg a3; a3.type = 's'; a3.s = "hello";
  args = {a1, a2, a3};

  auto pkt = osc.encode_message("/test/address", args);
  assert(!pkt.empty());

  // 验证地址以 / 开头
  assert(pkt[0] == '/');

  // 解码回原值
  std::string addr;
  std::vector<OscArg> decoded;
  bool ok = osc.parse_packet(pkt.data(), static_cast<int>(pkt.size()),
                              addr, decoded);
  assert(ok);
  assert(addr == "/test/address");
  assert(decoded.size() == 3);
  assert(decoded[0].type == 'i');
  assert(decoded[0].i == 42);
  assert(decoded[1].type == 'f');
  // float 比较：允许精度误差
  assert(std::abs(decoded[1].f - 3.14f) < 0.01f);
  assert(decoded[2].type == 's');
  assert(decoded[2].s == "hello");

  std::cout << "[PASS] test_osc_encode_decode_simple\n";
}

static void test_osc_encode_decode_no_args() {
  OscAdapter osc;

  std::vector<OscArg> args;
  auto pkt = osc.encode_message("/go", args);
  assert(!pkt.empty());

  std::string addr;
  std::vector<OscArg> decoded;
  bool ok = osc.parse_packet(pkt.data(), static_cast<int>(pkt.size()),
                              addr, decoded);
  assert(ok);
  assert(addr == "/go");
  assert(decoded.empty());

  std::cout << "[PASS] test_osc_encode_decode_no_args\n";
}

static void test_osc_encode_decode_bool_null() {
  OscAdapter osc;

  OscArg a1; a1.type = 'T';
  OscArg a2; a2.type = 'F';
  OscArg a3; a3.type = 'N';
  std::vector<OscArg> args = {a1, a2, a3};

  auto pkt = osc.encode_message("/bool/test", args);

  std::string addr;
  std::vector<OscArg> decoded;
  bool ok = osc.parse_packet(pkt.data(), static_cast<int>(pkt.size()),
                              addr, decoded);
  assert(ok);
  assert(addr == "/bool/test");
  assert(decoded.size() == 3);
  assert(decoded[0].type == 'T');
  assert(decoded[1].type == 'F');
  assert(decoded[2].type == 'N');

  std::cout << "[PASS] test_osc_encode_decode_bool_null\n";
}

static void test_osc_alignment() {
  OscAdapter osc;

  // 短地址（1 字节）应该 padding 到 4
  auto pkt = osc.encode_message("/a", {});
  // 地址 "/a\0" → 3 字节 → padding 到 4
  // 然后类型标签 ",\0" → 2 字节 → padding 到 4
  // 总共 8 字节
  assert(pkt.size() >= 8);
  assert(pkt.size() % 4 == 0);

  std::string addr;
  std::vector<OscArg> decoded;
  assert(osc.parse_packet(pkt.data(), static_cast<int>(pkt.size()),
                            addr, decoded));
  assert(addr == "/a");

  std::cout << "[PASS] test_osc_alignment\n";
}

static void test_osc_blob() {
  OscAdapter osc;

  OscArg a; a.type = 'b';
  a.b = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<OscArg> args = {a};

  auto pkt = osc.encode_message("/blob", args);

  std::string addr;
  std::vector<OscArg> decoded;
  assert(osc.parse_packet(pkt.data(), static_cast<int>(pkt.size()),
                            addr, decoded));
  assert(decoded.size() == 1);
  assert(decoded[0].type == 'b');
  assert(decoded[0].b.size() == 5);
  assert(decoded[0].b[0] == 0x01);
  assert(decoded[0].b[4] == 0x05);

  std::cout << "[PASS] test_osc_blob\n";
}

int main() {
  test_osc_encode_decode_simple();
  test_osc_encode_decode_no_args();
  test_osc_encode_decode_bool_null();
  test_osc_alignment();
  test_osc_blob();

  std::cout << "\nAll OSC adapter tests passed!\n";
  return 0;
}
