// Windows Service 管理器：SCM 注册 / 卸载 / 服务入口 / 状态报告 / 崩溃恢复
// 需求：工控机/服务器后台无界面常驻运行，开机自启，无需显示器键鼠。
// 7x24 稳定：服务崩溃自动重启，deadlock 检测，异常恢复。
#pragma once

#include <functional>
#include <string>

namespace sm {
namespace remote {

// 服务运行回调：由 main_headless 提供，服务入口调用此函数启动实际业务。
// 返回值：服务退出码（0 = 正常退出）。
using ServiceRunFn = std::function<int(int argc, char** argv)>;

// 服务状态（SCM 汇报）
enum class ServiceState {
  Stopped,
  StartPending,
  Running,
  StopPending,
  Paused
};

struct ServiceConfig {
  std::string name;          // 服务名（如 "ShowMasterSvc"）
  std::string display_name;  // 显示名（如 "ShowMaster 全域智能舞美管控平台"）
  std::string description;   // 描述
  std::string binary_path;   // 可执行文件路径（--service 模式）
  int port = 8080;           // Web 端口
  // 崩溃恢复策略
  int restart_delay_ms_1 = 5000;   // 第1次崩溃后 5 秒重启
  int restart_delay_ms_2 = 10000;  // 第2次 10 秒
  int restart_delay_ms_3 = 30000;  // 第3次+ 30 秒
  bool auto_start = true;          // SERVICE_AUTO_START
};

// ---- 公共接口 ----

// 注册为 Windows Service（SCM）
// 在 Windows 上调用 OpenSCManager → CreateService(SERVICE_AUTO_START)
// 非 Windows 平台为 stub，写入 systemd unit 文件到指定路径。
bool install_service(const ServiceConfig& cfg);

// 卸载 Windows Service
bool uninstall_service(const std::string& service_name);

// 查询服务安装状态
bool is_service_installed(const std::string& service_name);
bool is_service_running(const std::string& service_name);

// 启动 / 停止服务
bool start_service(const std::string& service_name);
bool stop_service(const std::string& service_name);

// ---- 服务模式入口 ----
// Windows: 以 ServiceMain 入口启动，向 SCM 报告状态，调用 run_fn 执行业务。
// 非 Windows: 直接调用 run_fn（兼容测试）。
int run_as_service(const ServiceConfig& cfg, ServiceRunFn run_fn, int argc, char** argv);

// ---- 状态报告（供业务循环调用）----
void report_service_state(ServiceState state);

// ---- 崩溃恢复配置（写入注册表 Recovery 策略）----
bool configure_recovery(const std::string& service_name,
                        int delay_ms_1, int delay_ms_2, int delay_ms_3);

} // namespace remote
} // namespace sm
