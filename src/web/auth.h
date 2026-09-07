// Web 鉴权模块（规格 §9.5 遥控鉴权扩展）
// 职责：用户登录校验 → 发放会话 Token → 后续请求校验 Token + 角色 → 访问日志入库。
// 角色：admin（全权）/ operator（操作：播放/GO/切换/灯光/视频/硬件）/ viewer（只读预览）
// Token 为随机 hex 字符串，内存态管理，进程重启即失效（工控机常驻运行下足够）。
// 访问日志写入 system_log 表（如已建 schema）或 stdout 降级。
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace sm {
namespace web {

enum class Role { admin, operator_, viewer };

struct WebUser {
  std::string username;
  std::string password_hash;  // SHA-256(password)
  Role role;
};

struct WebSession {
  std::string token;
  std::string username;
  Role role;
  int64_t created_ms;
  int64_t last_active_ms;
  std::string peer_ip;
};

class Auth {
 public:
  Auth();

  // 初始化默认用户（admin/admin123, operator/op123, viewer/view123）
  // 实际部署应由管理员改密；此处仅开发期默认。
  void init_defaults();

  // 登录成功返回 token，失败返回空串。
  std::string login(const std::string& username, const std::string& password,
                    const std::string& peer_ip);

  // 校验 token；有效返回 true 并刷新 last_active；无效返回 false。
  bool validate(const std::string& token, WebSession& out_session);

  // 角色权限检查：viewer 只允许 GET；operator 允许 GET+POST；admin 全部。
  bool can_access(Role role, const std::string& method);

  // 删除会话（登出）。
  void logout(const std::string& token);

  // 修改密码（仅 admin 可改他人，其余只能改自己）。
  bool change_password(const std::string& token,
                       const std::string& old_pw, const std::string& new_pw);

  // 在线会话数
  size_t session_count();

  // 设置访问日志回调（默认 stdout）
  void set_log_callback(std::function<void(const std::string&)> cb);

 private:
  std::mutex mu_;
  std::map<std::string, WebUser> users_;
  std::map<std::string, WebSession> sessions_;
  std::function<void(const std::string&)> log_cb_;

  std::string gen_token();
  void log(const std::string& msg);
  static std::string sha256_hex(const std::string& s);
};

}  // namespace web
}  // namespace sm
