// Web 模块单元测试：SHA-1/Base64/WebSocket Accept + Auth 鉴权 + RemoteQueue
// HTTP Server 的网络层由 sm_headless 冒烟覆盖（编译通过即 ABI 可用）。
#include <string>

#include "test_common.h"

#include "web/auth.h"
#include "web/remote_queue.h"
#include "web/ws_crypto.h"

namespace {

using sm::web::Auth;
using sm::web::RemoteCommand;
using sm::web::RemoteQueue;
using sm::web::Role;
using sm::web::sha1;
using sm::web::ws_accept_key;
using sm::web::base64_encode;

// ---- SHA-1 标准测试向量 ----
void test_sha1() {
  // SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
  auto h = sha1("");
  SM_CHECK_EQ(h.size(), std::size_t(20));
  std::string hex;
  static const char* d = "0123456789abcdef";
  for (uint8_t b : h) { hex += d[b >> 4]; hex += d[b & 0xF]; }
  SM_CHECK_EQ(hex, std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));

  // SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
  auto h2 = sha1("abc");
  std::string hex2;
  for (uint8_t b : h2) { hex2 += d[b >> 4]; hex2 += d[b & 0xF]; }
  SM_CHECK_EQ(hex2, std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
}

// ---- Base64 标准测试 ----
void test_base64() {
  // Base64("") = ""
  SM_CHECK_EQ(base64_encode(""), std::string(""));
  // Base64("f") = "Zg=="
  SM_CHECK_EQ(base64_encode("f"), std::string("Zg=="));
  // Base64("fo") = "Zm8="
  SM_CHECK_EQ(base64_encode("fo"), std::string("Zm8="));
  // Base64("foo") = "Zm9v"
  SM_CHECK_EQ(base64_encode("foo"), std::string("Zm9v"));
}

// ---- WebSocket Accept Key（RFC 6455 §1.3 示例）----
void test_ws_accept() {
  // 客户端 key "dGhlIHNhbXBsZSBub25jZQ==" → Accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
  SM_CHECK_EQ(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ=="),
              std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

// ---- Auth 鉴权全流程 ----
void test_auth_login_validate() {
  Auth auth;
  auth.init_defaults();

  // 正确密码登录
  std::string token = auth.login("admin", "admin123", "192.168.1.100");
  SM_CHECK(!token.empty());

  // 校验 token 有效
  sm::web::WebSession session;
  SM_CHECK(auth.validate(token, session));
  SM_CHECK_EQ(session.username, std::string("admin"));
  SM_CHECK(session.role == Role::admin);

  // 错误密码登录失败
  std::string bad = auth.login("admin", "wrong", "1.2.3.4");
  SM_CHECK(bad.empty());

  // 不存在用户失败
  SM_CHECK(auth.login("nobody", "x", "1.2.3.4").empty());
}

void test_auth_role_access() {
  Auth auth;
  auth.init_defaults();

  // viewer 只能 GET，不能 POST
  std::string vt = auth.login("viewer", "view123", "1.2.3.4");
  SM_CHECK(auth.can_access(Role::viewer, "GET"));
  SM_CHECK(!auth.can_access(Role::viewer, "POST"));

  // operator 可 GET + POST
  SM_CHECK(auth.can_access(Role::operator_, "GET"));
  SM_CHECK(auth.can_access(Role::operator_, "POST"));

  // admin 全权
  SM_CHECK(auth.can_access(Role::admin, "GET"));
  SM_CHECK(auth.can_access(Role::admin, "POST"));
  SM_CHECK(auth.can_access(Role::admin, "DELETE"));
}

void test_auth_session_timeout_logout() {
  Auth auth;
  auth.init_defaults();
  std::string t = auth.login("admin", "admin123", "1.2.3.4");
  SM_CHECK_EQ(auth.session_count(), std::size_t(1));

  auth.logout(t);
  sm::web::WebSession s;
  SM_CHECK(!auth.validate(t, s));
  SM_CHECK_EQ(auth.session_count(), std::size_t(0));
}

void test_auth_single_device_login() {
  Auth auth;
  auth.init_defaults();
  std::string t1 = auth.login("admin", "admin123", "1.2.3.4");
  SM_CHECK_EQ(auth.session_count(), std::size_t(1));
  // 同用户再次登录 → 旧会话失效
  std::string t2 = auth.login("admin", "admin123", "5.6.7.8");
  SM_CHECK_EQ(auth.session_count(), std::size_t(1));
  sm::web::WebSession s;
  SM_CHECK(!auth.validate(t1, s));  // 旧 token 失效
  SM_CHECK(auth.validate(t2, s));  // 新 token 有效
}

void test_auth_change_password() {
  Auth auth;
  auth.init_defaults();
  std::string t = auth.login("admin", "admin123", "1.2.3.4");
  SM_CHECK(auth.change_password(t, "admin123", "newpass456"));
  // 旧密码登录失败
  SM_CHECK(auth.login("admin", "admin123", "1.2.3.4").empty());
  // 新密码登录成功
  SM_CHECK(!auth.login("admin", "newpass456", "1.2.3.4").empty());
  // 改密错误旧密码
  std::string t2 = auth.login("admin", "newpass456", "1.2.3.4");
  SM_CHECK(!auth.change_password(t2, "wrong", "x"));
}

// ---- RemoteQueue 推拉与丢弃 ----
void test_queue_basic() {
  RemoteQueue q(8);
  SM_CHECK_EQ(q.size(), std::size_t(0));

  int got = 0;
  q.push({"id1", "media.play", "{}", "remote.web:1", 100});
  q.push({"id2", "transport.stop", "{}", "remote.web:1", 101});
  SM_CHECK_EQ(q.size(), std::size_t(2));

  size_t n = q.drain([&](const RemoteCommand& cmd) { ++got; });
  SM_CHECK_EQ(n, std::size_t(2));
  SM_CHECK_EQ(got, 2);
  SM_CHECK_EQ(q.size(), std::size_t(0));  // 排空
}

void test_queue_overflow_drop() {
  RemoteQueue q(4);
  for (int i = 0; i < 10; ++i) {
    RemoteCommand cmd{"id" + std::to_string(i), "sys.ping", "{}", "x", i};
    q.push(std::move(cmd));
  }
  SM_CHECK_EQ(q.total_pushed(), uint64_t(10));
  SM_CHECK(q.total_dropped() > 0);  // 有丢弃

  // drain 取出最新 4 条（容量限制）
  int got = 0;
  q.drain([&](const RemoteCommand&) { ++got; });
  SM_CHECK_EQ(std::size_t(got), std::size_t(4));
}

void test_queue_drain_empty() {
  RemoteQueue q(8);
  int got = 0;
  size_t n = q.drain([&](const RemoteCommand&) { ++got; });
  SM_CHECK_EQ(n, std::size_t(0));
  SM_CHECK_EQ(got, 0);
}

}  // namespace

int main() {
  test_sha1();
  test_base64();
  test_ws_accept();
  test_auth_login_validate();
  test_auth_role_access();
  test_auth_session_timeout_logout();
  test_auth_single_device_login();
  test_auth_change_password();
  test_queue_basic();
  test_queue_overflow_drop();
  test_queue_drain_empty();
  return smtest::finish("test_web");
}
