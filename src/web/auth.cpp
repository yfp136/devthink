#include "web/auth.h"

#include <cstdio>
#include <random>
#include <sstream>

// 复用内核已有的 SHA-256
#include "core/util.h"

namespace sm {
namespace web {

Auth::Auth() {
  log_cb_ = [](const std::string& msg) {
    std::fprintf(stdout, "[web_auth] %s\n", msg.c_str());
  };
}

void Auth::init_defaults() {
  std::lock_guard<std::mutex> lk(mu_);
  users_["admin"]    = {"admin",    sha256_hex("admin123"), Role::admin};
  users_["operator"] = {"operator", sha256_hex("op123"),    Role::operator_};
  users_["viewer"]   = {"viewer",   sha256_hex("view123"),  Role::viewer};
  log("init_defaults: 3 default users created (change passwords in production)");
}

std::string Auth::sha256_hex(const std::string& s) {
  return sm::sha256_hex(s);
}

std::string Auth::gen_token() {
  std::random_device rd;
  std::mt19937_64 gen(rd() ^ std::time(nullptr));
  std::ostringstream ss;
  for (int i = 0; i < 4; ++i)
    ss << std::hex << gen();
  return ss.str();
}

std::string Auth::login(const std::string& username, const std::string& password,
                        const std::string& peer_ip) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = users_.find(username);
  if (it == users_.end() || it->second.password_hash != sha256_hex(password)) {
    log("login FAIL user=" + username + " ip=" + peer_ip);
    return "";
  }

  // 清除该用户旧会话（单设备登录）
  for (auto s_it = sessions_.begin(); s_it != sessions_.end(); ) {
    if (s_it->second.username == username)
      s_it = sessions_.erase(s_it);
    else
      ++s_it;
  }

  std::string token = gen_token();
  auto now = std::time(nullptr);
  sessions_[token] = {token, username, it->second.role,
                     now * 1000, now * 1000, peer_ip};
  log("login OK user=" + username + " ip=" + peer_ip + " role=" +
      (it->second.role == Role::admin ? "admin" :
       it->second.role == Role::operator_ ? "operator" : "viewer"));
  return token;
}

bool Auth::validate(const std::string& token, WebSession& out) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;

  // 会话超时：8 小时无活动自动失效
  auto now = std::time(nullptr);
  if (now * 1000 - it->second.last_active_ms > 8 * 3600 * 1000) {
    sessions_.erase(it);
    log("session expired user=" + it->second.username);
    return false;
  }
  it->second.last_active_ms = now * 1000;
  out = it->second;
  return true;
}

bool Auth::can_access(Role role, const std::string& method) {
  if (method == "GET" || method == "OPTIONS") return true;  // 所有角色可读
  if (method == "POST") return role != Role::viewer;        // viewer 不可写
  return role == Role::admin;                                 // 其他仅 admin
}

void Auth::logout(const std::string& token) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sessions_.find(token);
  if (it != sessions_.end()) {
    log("logout user=" + it->second.username);
    sessions_.erase(it);
  }
}

bool Auth::change_password(const std::string& token,
                           const std::string& old_pw,
                           const std::string& new_pw) {
  std::lock_guard<std::mutex> lk(mu_);
  auto sit = sessions_.find(token);
  if (sit == sessions_.end()) return false;
  const std::string& user = sit->second.username;
  auto uit = users_.find(user);
  if (uit == users_.end()) return false;
  if (uit->second.password_hash != sha256_hex(old_pw)) return false;
  uit->second.password_hash = sha256_hex(new_pw);
  log("password changed user=" + user);
  return true;
}

size_t Auth::session_count() {
  std::lock_guard<std::mutex> lk(mu_);
  return sessions_.size();
}

void Auth::set_log_callback(std::function<void(const std::string&)> cb) {
  log_cb_ = std::move(cb);
}

void Auth::log(const std::string& msg) {
  if (log_cb_) log_cb_(msg);
}

}  // namespace web
}  // namespace sm
