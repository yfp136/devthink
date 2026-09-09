// Windows Service 管理器实现
// Windows: 使用 SCM API (OpenSCManager / CreateService / StartServiceCtrlDispatcher)
// 非 Windows: stub（写入 systemd unit 文件供测试）
#include "remote/service_manager.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "advapi32.lib")

// ---- 全局状态（ServiceMain 回调）----
static SERVICE_STATUS        g_svc_status;
static SERVICE_STATUS_HANDLE g_svc_handle = nullptr;
static sm::remote::ServiceRunFn g_run_fn;
static sm::remote::ServiceConfig g_svc_cfg;

// SCM 控制处理器
static void WINAPI svc_ctrl_handler(DWORD ctrl) {
  switch (ctrl) {
    case SERVICE_CONTROL_STOP:
      g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
      g_svc_status.dwCheckPoint = 1;
      SetServiceStatus(g_svc_handle, &g_svc_status);
      // 通知业务循环退出（由 report_service_state 配合）
      g_svc_status.dwCurrentState = SERVICE_STOPPED;
      g_svc_status.dwWin32ExitCode = 0;
      break;
    case SERVICE_CONTROL_PAUSE:
      g_svc_status.dwCurrentState = SERVICE_PAUSE_PENDING;
      SetServiceStatus(g_svc_handle, &g_svc_status);
      g_svc_status.dwCurrentState = SERVICE_PAUSED;
      break;
    case SERVICE_CONTROL_CONTINUE:
      g_svc_status.dwCurrentState = SERVICE_CONTINUE_PENDING;
      SetServiceStatus(g_svc_handle, &g_svc_status);
      g_svc_status.dwCurrentState = SERVICE_RUNNING;
      break;
    case SERVICE_CONTROL_INTERROGATE:
      break;
    default:
      break;
  }
  SetServiceStatus(g_svc_handle, &g_svc_status);
}

// ServiceMain：SCM 调用的入口
static void WINAPI svc_main(DWORD argc, char** argv) {
  g_svc_handle = RegisterServiceCtrlHandlerA(g_svc_cfg.name.c_str(),
                                              svc_ctrl_handler);
  if (!g_svc_handle) return;

  // 报告 START_PENDING
  g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_svc_status.dwCurrentState = SERVICE_START_PENDING;
  g_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP |
                                     SERVICE_ACCEPT_PAUSE_CONTINUE;
  g_svc_status.dwWin32ExitCode = 0;
  g_svc_status.dwServiceSpecificExitCode = 0;
  g_svc_status.dwCheckPoint = 0;
  g_svc_status.dwWaitHint = 10000;  // 10 秒启动超时
  SetServiceStatus(g_svc_handle, &g_svc_status);

  // 调用业务函数
  int rc = 0;
  if (g_run_fn) {
    // 构造 argv：ServiceMain 的 argc/argv 不含服务名
    g_svc_status.dwCurrentState = SERVICE_RUNNING;
    g_svc_status.dwCheckPoint = 0;
    g_svc_status.dwWaitHint = 0;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    // 转为 char**
    char** run_argv = new char*[argc];
    for (DWORD i = 0; i < argc; ++i)
      run_argv[i] = argv[i];
    rc = g_run_fn(int(argc), run_argv);
    delete[] run_argv;
  }

  // 报告 STOPPED
  g_svc_status.dwCurrentState = SERVICE_STOPPED;
  g_svc_status.dwWin32ExitCode = DWORD(rc);
  SetServiceStatus(g_svc_handle, &g_svc_status);
}

#endif // _WIN32

namespace sm {
namespace remote {

// ============================================================================
// Windows 实现
// ============================================================================

#if defined(_WIN32)

bool install_service(const ServiceConfig& cfg) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    std::fprintf(stderr, "[service] OpenSCManager 失败: %lu\n", GetLastError());
    return false;
  }

  // 构建命令行：sm_headless.exe --service --port <port>
  std::string cmd = cfg.binary_path + " --service --port "
                    + std::to_string(cfg.port);

  SC_HANDLE svc = CreateServiceA(
    scm,
    cfg.name.c_str(),
    cfg.display_name.c_str(),
    SERVICE_ALL_ACCESS,
    SERVICE_WIN32_OWN_PROCESS,
    cfg.auto_start ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
    SERVICE_ERROR_NORMAL,
    cmd.c_str(),
    nullptr,           // 无加载顺序组
    nullptr,           // 无 tag
    "Tcpip\0",         // 依赖 TCP/IP 协议栈
    nullptr,           // LocalSystem 账户
    nullptr);          // 无密码

  if (!svc) {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_EXISTS) {
      std::printf("[service] 服务 %s 已存在，跳过创建\n", cfg.name.c_str());
      CloseServiceHandle(scm);
      return true;
    }
    std::fprintf(stderr, "[service] CreateService 失败: %lu\n", err);
    CloseServiceHandle(scm);
    return false;
  }

  // 设置描述
  SERVICE_DESCRIPTIONA desc;
  desc.lpDescription = const_cast<char*>(cfg.description.c_str());
  ChangeServiceConfig2A(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

  // 配置崩溃恢复
  SC_ACTION actions[3];
  actions[0].Type = SC_ACTION_RESTART;
  actions[0].Delay = cfg.restart_delay_ms_1;
  actions[1].Type = SC_ACTION_RESTART;
  actions[1].Delay = cfg.restart_delay_ms_2;
  actions[2].Type = SC_ACTION_RESTART;
  actions[2].Delay = cfg.restart_delay_ms_3;

  SERVICE_FAILURE_ACTIONSA fa = {};
  fa.dwResetPeriod = 86400;  // 24 小时重置计数
  fa.lpRebootMsg = nullptr;
  fa.lpCommand = nullptr;
  fa.cActions = 3;
  fa.lpsaActions = actions;

  // 需设置 SERVICE_CONFIG_FAILURE_ACTIONS_FLAG 才能让 SCM 在崩溃时执行恢复
  ChangeServiceConfig2A(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

  // 允许服务在崩溃时触发恢复操作（而非仅"正常退出"）
  BOOL fFailureActionsOnNonCrash = TRUE;
  ChangeServiceConfig2A(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,
                         &fFailureActionsOnNonCrash);

  // 设置延迟自动启动（若 auto_start，延迟 30 秒以等待网络就绪）
  if (cfg.auto_start) {
    SERVICE_DELAYED_AUTO_START_INFO delayed = {};
    delayed.fDelayedAutostart = TRUE;
    ChangeServiceConfig2A(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                           &delayed);
  }

  std::printf("[service] 服务 %s 安装成功\n", cfg.name.c_str());
  std::printf("[service]   命令行: %s\n", cmd.c_str());
  std::printf("[service]   自动启动: %s\n", cfg.auto_start ? "是" : "否");
  std::printf("[service]   崩溃恢复: 5s/10s/30s 自动重启\n");

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return true;
}

bool uninstall_service(const std::string& service_name) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) return false;

  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(),
                               SERVICE_STOP | DELETE);
  if (!svc) {
    std::fprintf(stderr, "[service] 服务 %s 未安装\n", service_name.c_str());
    CloseServiceHandle(scm);
    return false;
  }

  // 先停止服务
  SERVICE_STATUS status;
  ControlService(svc, SERVICE_CONTROL_STOP, &status);

  // 删除服务
  BOOL ok = DeleteService(svc);

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);

  if (ok) {
    std::printf("[service] 服务 %s 已卸载\n", service_name.c_str());
    return true;
  }
  return false;
}

bool is_service_installed(const std::string& service_name) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(), SERVICE_QUERY_STATUS);
  bool exists = (svc != nullptr);
  if (svc) CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return exists;
}

bool is_service_running(const std::string& service_name) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(), SERVICE_QUERY_STATUS);
  if (!svc) { CloseServiceHandle(scm); return false; }

  SERVICE_STATUS status;
  BOOL ok = QueryServiceStatus(svc, &status);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);

  return ok && status.dwCurrentState == SERVICE_RUNNING;
}

bool start_service(const std::string& service_name) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(), SERVICE_START);
  if (!svc) { CloseServiceHandle(scm); return false; }
  BOOL ok = StartServiceA(svc, 0, nullptr);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}

bool stop_service(const std::string& service_name) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(), SERVICE_STOP);
  if (!svc) { CloseServiceHandle(scm); return false; }
  SERVICE_STATUS status;
  BOOL ok = ControlService(svc, SERVICE_CONTROL_STOP, &status);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return ok;
}

int run_as_service(const ServiceConfig& cfg, ServiceRunFn run_fn,
                   int argc, char** argv) {
  g_run_fn = std::move(run_fn);
  g_svc_cfg = cfg;

  SERVICE_TABLE_ENTRYA table[] = {
    {const_cast<char*>(cfg.name.c_str()), svc_main},
    {nullptr, nullptr}
  };

  // StartServiceCtrlDispatcher 阻塞直到服务停止
  if (!StartServiceCtrlDispatcherA(table)) {
    DWORD err = GetLastError();
    if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
      // 非 Service 模式启动（如直接运行 .exe），降级为普通进程
      std::fprintf(stderr, "[service] 未在 Service 模式下启动，降级为普通进程\n");
      return run_fn ? run_fn(argc, argv) : 1;
    }
    std::fprintf(stderr, "[service] StartServiceCtrlDispatcher 失败: %lu\n", err);
    return 1;
  }
  return 0;
}

void report_service_state(ServiceState state) {
  if (!g_svc_handle) return;

  DWORD scm_state;
  switch (state) {
    case ServiceState::StartPending:  scm_state = SERVICE_START_PENDING; break;
    case ServiceState::Running:       scm_state = SERVICE_RUNNING; break;
    case ServiceState::StopPending:   scm_state = SERVICE_STOP_PENDING; break;
    case ServiceState::Stopped:       scm_state = SERVICE_STOPPED; break;
    case ServiceState::Paused:        scm_state = SERVICE_PAUSED; break;
    default: return;
  }
  g_svc_status.dwCurrentState = scm_state;
  SetServiceStatus(g_svc_handle, &g_svc_status);
}

bool configure_recovery(const std::string& service_name,
                        int delay_ms_1, int delay_ms_2, int delay_ms_3) {
  SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceA(scm, service_name.c_str(), SERVICE_ALL_ACCESS);
  if (!svc) { CloseServiceHandle(scm); return false; }

  SC_ACTION actions[3];
  actions[0] = {SC_ACTION_RESTART, DWORD(delay_ms_1)};
  actions[1] = {SC_ACTION_RESTART, DWORD(delay_ms_2)};
  actions[2] = {SC_ACTION_RESTART, DWORD(delay_ms_3)};

  SERVICE_FAILURE_ACTIONSA fa = {};
  fa.dwResetPeriod = 86400;
  fa.cActions = 3;
  fa.lpsaActions = actions;

  BOOL ok = ChangeServiceConfig2A(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return ok != FALSE;
}

// ============================================================================
// 非 Windows 平台 stub（生成 systemd unit 文件供参考）
// ============================================================================

#else // !_WIN32

bool install_service(const ServiceConfig& cfg) {
  // 生成 systemd unit 文件
  std::ostringstream unit;
  unit << "[Unit]\n"
       << "Description=" << cfg.display_name << "\n"
       << "After=network.target\n\n"
       << "[Service]\n"
       << "Type=simple\n"
       << "ExecStart=" << cfg.binary_path
       << " --service --port " << cfg.port << "\n"
       << "Restart=always\n"
       << "RestartSec=5\n"
       << "User=root\n"
       << "WorkingDirectory=/opt/showmaster\n\n"
       << "[Install]\n"
       << "WantedBy=multi-user.target\n";

  std::string path = "/tmp/showmaster.service";
  std::ofstream f(path);
  if (!f) return false;
  f << unit.str();
  f.close();

  std::printf("[service] systemd unit 文件已生成: %s\n", path.c_str());
  std::printf("[service] 安装命令: sudo cp %s /etc/systemd/system/ && "
              "sudo systemctl daemon-reload && sudo systemctl enable %s\n",
              path.c_str(), cfg.name.c_str());
  return true;
}

bool uninstall_service(const std::string& service_name) {
  std::printf("[service] 非 Windows 平台，请手动执行: "
              "sudo systemctl disable %s && sudo rm /etc/systemd/system/showmaster.service\n",
              service_name.c_str());
  return true;
}

bool is_service_installed(const std::string&) { return false; }
bool is_service_running(const std::string&) { return false; }
bool start_service(const std::string&) { return false; }
bool stop_service(const std::string&) { return false; }

int run_as_service(const ServiceConfig& cfg, ServiceRunFn run_fn,
                   int argc, char** argv) {
  (void)cfg;
  // 非 Windows 直接运行业务函数
  return run_fn ? run_fn(argc, argv) : 1;
}

void report_service_state(ServiceState) {
  // 非 Windows 平台无需向 SCM 报告
}

bool configure_recovery(const std::string&, int, int, int) {
  return false;
}

#endif // _WIN32

} // namespace remote
} // namespace sm
