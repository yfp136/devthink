#include "web/web_gateway.h"

#include <ctime>
#include <sstream>

#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "core/util.h"
#include "engines/engine_registry.h"
#include "nlohmann/json.hpp"

namespace sm {
namespace web {

using json = nlohmann::json;

// 内嵌 Web UI HTML（定义在 web_ui.cpp）
extern const char* kWebUIHtml;

WebGateway::WebGateway(Auth& auth, RemoteQueue& queue, HeartbeatMonitor& hb)
    : auth_(auth), queue_(queue), hb_(hb) {}

// ---- 鉴权辅助 ----
bool WebGateway::check_auth(const HttpRequest& req, WebSession& session) {
  // 优先从 Authorization 头取，其次从 query 参数取
  std::string token;
  auto it = req.headers.find("authorization");
  if (it != req.headers.end()) {
    std::string val = it->second;
    if (val.compare(0, 7, "Bearer ") == 0)
      token = val.substr(7);
    else
      token = val;
  } else {
    // query 参数 ?token=xxx
    auto qpos = req.query.find("token=");
    if (qpos == 0) token = req.query.substr(6);
  }
  return auth_.validate(token, session);
}

HttpResponse WebGateway::json_ok(const std::string& body) {
  HttpResponse r;
  r.content_type = "application/json; charset=utf-8";
  r.body = body;
  return r;
}

HttpResponse WebGateway::json_err(int status, const std::string& msg) {
  json j = {{"error", true}, {"message", msg}};
  HttpResponse r;
  r.status = status;
  r.content_type = "application/json; charset=utf-8";
  r.body = j.dump();
  return r;
}

// ---- 路由注册 ----
void WebGateway::register_routes(HttpServer& server) {
  server_ = &server;

  // ---- 内嵌 Web UI（根路径返回 HTML 单页应用）----
  server.get("/", [this](const HttpRequest&) {
    HttpResponse r;
    r.content_type = "text/html; charset=utf-8";
    r.body = kWebUIHtml;
    return r;
  });

  // ---- 登录 ----
  server.post("/api/login", [this](const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (body.is_null() || !body.contains("username") || !body.contains("password"))
      return json_err(400, "missing username or password");

    std::string token = auth_.login(
        body["username"].get<std::string>(),
        body["password"].get<std::string>(),
        req.peer_ip);
    if (token.empty())
      return json_err(401, "invalid credentials");

    WebSession s;
    auth_.validate(token, s);
    json resp = {
      {"token", token},
      {"username", s.username},
      {"role", s.role == Role::admin ? "admin" :
              s.role == Role::operator_ ? "operator" : "viewer"}
    };
    return json_ok(resp.dump());
  });

  // ---- 登出 ----
  server.post("/api/logout", [this](const HttpRequest& req) {
    json body = json::parse(req.body, nullptr, false);
    if (body.is_null() || !body.contains("token"))
      return json_err(400, "missing token");
    auth_.logout(body["token"].get<std::string>());
    return json_ok(R"({"ok":true})");
  });

  // ---- 通用指令入口（核心：POST /api/cmd）----
  server.post("/api/cmd", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");

    // 权限检查：viewer 不能发指令
    if (!auth_.can_access(session.role, "POST"))
      return json_err(403, "viewer role cannot send commands");

    json body = json::parse(req.body, nullptr, false);
    if (body.is_null() || !body.contains("op"))
      return json_err(400, "missing op field");

    std::string op = body["op"].get<std::string>();

    // 校验 op 是否在字典中
    if (!is_cmd_op(op))
      return json_err(400, "unknown op: " + op);

    // 构造参数
    std::string params_json = body.value("params", json::object()).dump();

    // 入队（不直接投递总线，保持解耦）
    std::string cmd_id = sm::uuid_hex32();
    RemoteCommand cmd{
      cmd_id,  // 信封 id
      op,
      params_json,
      "remote.web:" + session.peer_ip,
      std::time(nullptr) * 1000
    };
    queue_.push(std::move(cmd));

    return json_ok(json({{"accepted", true}, {"id", cmd_id}}).dump());
  });

  // ---- 状态查询 ----
  server.get("/api/status", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");

    json j;
    // 引擎健康
    json engines = json::array();
    for (const auto& e : sm::engine_registry()) {
      engines.push_back({
        {"id", e.id},
        {"dll", e.dll},
        {"module", e.module},
        {"phase", e.phase},
        {"online", hb_.online(e.id)}
      });
    }
    j["engines"] = engines;
    j["queue"] = {{"pending", queue_.size()},
                  {"total_pushed", queue_.total_pushed()},
                  {"total_dropped", queue_.total_dropped()}};
    j["sessions"] = auth_.session_count();

    // 自定义状态（播放状态/当前场景等由引擎层提供）
    if (status_provider_)
      j["runtime"] = json::parse(status_provider_(), nullptr, false);
    return json_ok(j.dump());
  });

  // ---- 引擎注册表 ----
  server.get("/api/engines", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");

    json arr = json::array();
    for (const auto& e : sm::engine_registry())
      arr.push_back({{"id", e.id}, {"dll", e.dll}, {"module", e.module}, {"phase", e.phase}});
    return json_ok(arr.dump());
  });

  // ---- 节目单 ----
  server.get("/api/playlist", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");
    if (playlist_provider_)
      return json_ok(playlist_provider_());
    return json_ok(R"({"items":[]})");
  });

  // ---- 场景切换快捷 ----
  server.post("/api/scene/go", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");
    if (!auth_.can_access(session.role, "POST"))
      return json_err(403, "forbidden");

    json body = json::parse(req.body, nullptr, false);
    if (body.is_null() || !body.contains("scene_id"))
      return json_err(400, "missing scene_id");

    std::string sid = sm::uuid_hex32();
    RemoteCommand cmd{
      sid, "scene.recall",
      json{{"scene_id", body["scene_id"]}}.dump(),
      "remote.web:" + session.peer_ip,
      std::time(nullptr) * 1000
    };
    queue_.push(std::move(cmd));
    return json_ok(json({{"accepted", true}, {"id", sid}}).dump());
  });

  // ---- 播放快捷 ----
  server.post("/api/transport/play", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");
    if (!auth_.can_access(session.role, "POST"))
      return json_err(403, "forbidden");

    json body = json::parse(req.body, nullptr, false);
    std::string media_id = body.value("media_id", "");
    std::string tid = sm::uuid_hex32();
    RemoteCommand cmd{
      tid, "transport.play",
      json{{"media_id", media_id}}.dump(),
      "remote.web:" + session.peer_ip,
      std::time(nullptr) * 1000
    };
    queue_.push(std::move(cmd));
    return json_ok(json({{"accepted", true}, {"id", tid}}).dump());
  });

  // ---- 停止快捷 ----
  server.post("/api/transport/stop", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");
    if (!auth_.can_access(session.role, "POST"))
      return json_err(403, "forbidden");

    std::string sid2 = sm::uuid_hex32();
    RemoteCommand cmd{
      sid2, "transport.stop",
      "{}",
      "remote.web:" + session.peer_ip,
      std::time(nullptr) * 1000
    };
    queue_.push(std::move(cmd));
    return json_ok(json({{"accepted", true}, {"id", sid2}}).dump());
  });

  // ---- GO 触发（节目单下一项）----
  server.post("/api/go", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");
    if (!auth_.can_access(session.role, "POST"))
      return json_err(403, "forbidden");

    std::string gid = sm::uuid_hex32();
    RemoteCommand cmd{
      gid, "playlist.go_next",
      "{}",
      "remote.web:" + session.peer_ip,
      std::time(nullptr) * 1000
    };
    queue_.push(std::move(cmd));
    return json_ok(json({{"accepted", true}, {"id", gid}}).dump());
  });

  // ---- 改密码 ----
  server.post("/api/password", [this](const HttpRequest& req) {
    WebSession session;
    if (!check_auth(req, session))
      return json_err(401, "unauthorized");

    json body = json::parse(req.body, nullptr, false);
    if (body.is_null() || !body.contains("old_password") || !body.contains("new_password"))
      return json_err(400, "missing old_password or new_password");

    if (!auth_.change_password(session.token,
                               body["old_password"].get<std::string>(),
                               body["new_password"].get<std::string>()))
      return json_err(400, "old password incorrect");
    return json_ok(R"({"ok":true})");
  });

  // ---- WebSocket：实时事件推送 ----
  server.ws("/ws/events", [this](const std::string& msg) {
    // 客户端可发送心跳文本，忽略即可
  });

  // ---- WebSocket：PGM 预览 ----
  server.ws("/ws/preview", [this](const std::string& msg) {
    // 客户端可发 "start"/"stop" 控制推流，当前忽略（默认推）
  });
}

void WebGateway::on_engine_event(const std::string& event_json) {
  if (server_)
    server_->ws_broadcast("/ws/events", event_json);
}

void WebGateway::on_pgm_frame(const std::string& jpeg_b64) {
  if (server_) {
    // 以 JSON 封装，方便浏览器解析
    std::string msg = "{\"type\":\"pgm_frame\",\"data\":\"data:image/jpeg;base64," +
                      jpeg_b64 + "\"}";
    server_->ws_broadcast("/ws/preview", msg);
  }
}

}  // namespace web
}  // namespace sm
