// test_web_ui.cpp - 内嵌 Web 控制台（kWebUIHtml）静态一致性测试（P1-4 收尾）。
// -----------------------------------------------------------------------------
// 背景：src/web/web_ui.cpp 里的整个前端是一块裸 R"HTML(...)" 字符串，
// C++ 编译器**完全不检查**它 —— 所以「按钮 onclick 指向某个函数、该函数却不存在」、
// 「函数 fetch 了某个路径、服务端却没有这条路由」这类缺口只能靠测试来钉住。
// 本轮真实修掉的就是后者：控制台有 sendPause() 却没有 sendResume()，
// 即浏览器端“暂停后无法继续”（服务端 /api/transport/resume 早已就绪）。
//
// 本用例做四件事，全部只依赖 kWebUIHtml 这一个符号：
//   1. 页面本体完整性（DOCTYPE / charset / 收尾标签 / 体量下限）；
//   2. 每个静态 onclick 处理器都必须能找到对应的 function 定义；
//   3. 页面里出现的每个 /api、/ws 路径都必须落在服务端已注册路由白名单内
//      （白名单镜像 src/web/web_gateway.cpp 的 register_routes，防止打字错误）；
//   4. 传输控制四件套（play/stop/pause/resume）必须端到端齐全：
//      页面有控件 → 有函数 → 路径在白名单中。
//
// 说明：真实 HTTP 往返（状态码/鉴权/JSON 体）不在本用例范围，由 sm_headless
// 冒烟与真机验证清单覆盖；这里只做「不依赖网络、不依赖 Qt」的静态契约。
// =============================================================================
#include "test_common.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

// 定义在 src/web/web_ui.cpp（web_gateway.cpp 亦以同样方式 extern 声明）
namespace sm {
namespace web {
extern const char* kWebUIHtml;
}
}  // namespace sm

namespace {

const std::string& page() {
  static const std::string html = sm::web::kWebUIHtml;
  return html;
}

// 收集 html 中所有以 /api/ 或 /ws/ 开头的路径字面量（去重、去掉 query 前缀后的部分）
std::vector<std::string> collect_paths(const std::string& html) {
  std::set<std::string> uniq;
  for (const char* prefix : {"/api/", "/ws/"}) {
    std::size_t pos = 0;
    const std::size_t plen = std::char_traits<char>::length(prefix);
    while ((pos = html.find(prefix, pos)) != std::string::npos) {
      std::size_t end = pos + plen;
      while (end < html.size()) {
        const char c = html[end];
        if (c == '\'' || c == '"' || c == '?' || c == ')' || c == ' ' || c == '`' ||
            c == ',' || c == '+' || c == '\n') {
          break;
        }
        ++end;
      }
      uniq.insert(html.substr(pos, end - pos));
      pos = end;
    }
  }
  return std::vector<std::string>(uniq.begin(), uniq.end());
}

// 收集全部静态 onclick="NAME(" 形式的处理器名（动态赋值 onclick=()=>... 不在其列）
std::vector<std::string> collect_onclick_handlers(const std::string& html) {
  std::set<std::string> uniq;
  const std::string key = "onclick=\"";
  std::size_t pos = 0;
  while ((pos = html.find(key, pos)) != std::string::npos) {
    std::size_t begin = pos + key.size();
    std::size_t end = begin;
    while (end < html.size() && (std::isalnum(static_cast<unsigned char>(html[end])) ||
                                 html[end] == '_')) {
      ++end;
    }
    if (end > begin && end < html.size() && html[end] == '(') {
      uniq.insert(html.substr(begin, end - begin));
    }
    pos = end;
  }
  return std::vector<std::string>(uniq.begin(), uniq.end());
}

// 服务端已注册路由（镜像 src/web/web_gateway.cpp::register_routes）
const char* const kServedRoutes[] = {
    // GET
    "/api/status", "/api/engines", "/api/playlist", "/api/scene/list",
    "/api/timeline/tracks", "/api/media/query",
    // POST
    "/api/login", "/api/logout", "/api/cmd", "/api/password",
    "/api/scene/go", "/api/scene/save",
    "/api/transport/play", "/api/transport/stop",
    "/api/transport/pause", "/api/transport/resume",
    "/api/go",
    "/api/playlist/start", "/api/playlist/stop", "/api/playlist/next",
    "/api/playlist/load",
    "/api/timeline/item", "/api/media/import",
    // WebSocket
    "/ws/events", "/ws/preview",
};

bool is_served(const std::string& path) {
  for (const char* r : kServedRoutes) {
    if (path == r) return true;
  }
  return false;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

// ---- 1. 页面本体完整性 ----
void test_page_basics() {
  const std::string& html = page();
  SM_CHECK(html.size() > 4000);                       // 体量下限：防止误改成空壳
  SM_CHECK(contains(html, "<!DOCTYPE html>"));
  SM_CHECK(contains(html, "</html>"));
  SM_CHECK(contains(html, "charset=\"utf-8\""));       // 中文界面必须声明编码
  SM_CHECK(contains(html, "lang=\"zh-CN\""));
  // 登录态持久化与 401 自动登出（远控台的基本可用性）
  SM_CHECK(contains(html, "localStorage.getItem('sm_token')"));
  SM_CHECK(contains(html, "doLogout()"));
}

// ---- 2. 静态 onclick 处理器必须有定义 ----
void test_onclick_handlers_defined() {
  const std::vector<std::string> handlers = collect_onclick_handlers(page());
  SM_CHECK(handlers.size() >= 8);  // 登录/登出/GO/播放/暂停/续播/停止/节目单三键
  for (const std::string& h : handlers) {
    const std::string def = "function " + h + "(";
    SM_CHECK_MSG(contains(page(), def),
                 ("onclick 指向未定义的函数: " + h).c_str());
  }
}

// ---- 3. 页面引用的每个路径都必须是服务端已注册路由 ----
void test_paths_are_served() {
  const std::vector<std::string> paths = collect_paths(page());
  SM_CHECK(paths.size() >= 8);
  for (const std::string& p : paths) {
    SM_CHECK_MSG(is_served(p), ("页面引用了服务端未注册的路径: " + p).c_str());
  }
}

// ---- 4. 传输控制四件套端到端齐全（本轮修复点）----
void test_transport_controls_complete() {
  const std::string& html = page();
  const char* const paths[] = {"/api/transport/play", "/api/transport/stop",
                              "/api/transport/pause", "/api/transport/resume"};
  const char* const fns[] = {"sendPlay", "sendStop", "sendPause", "sendResume"};
  for (int i = 0; i < 4; ++i) {
    // 路径可被 UI 调到，且服务端已注册
    SM_CHECK_MSG(contains(html, paths[i]), paths[i]);
    SM_CHECK_MSG(is_served(paths[i]), paths[i]);
    // 函数已定义且被静态控件引用（否则只是死代码）
    SM_CHECK_MSG(contains(html, std::string("function ") + fns[i] + "("), fns[i]);
    SM_CHECK_MSG(contains(html, std::string("onclick=\"") + fns[i] + "()\""), fns[i]);
  }
  // 暂停后可继续：续播按钮与暂停按钮并存（回归本轮的“缺 sendResume”缺口）
  SM_CHECK(contains(html, "btn-resume"));
}

}  // namespace

int main() {
  test_page_basics();
  test_onclick_handlers_defined();
  test_paths_are_served();
  test_transport_controls_complete();
  return smtest::finish("test_web_ui");
}
