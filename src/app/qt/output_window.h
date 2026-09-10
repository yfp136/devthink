// ShowMaster Qt 桌面端 —— 本地输出窗口（P1-2 收尾 / 规格第 2 章 1080p60 输出视口）
// -----------------------------------------------------------------------------
// 职责：为引擎的 D3D11 本地窗口输出提供宿主窗口，并把生命周期事件翻译成
// media_backend.h 的窗口输出接口调用：
//   open()             → platform::open_output_window(句柄, 尺寸)
//   窗口尺寸变化        → platform::resize_output_window(w, h)
//   窗口关闭 / 析构     → platform::close_output_window()
//
// 为什么是**独立窗口**（而不是把交换链挂到 QML 主窗口上）：
//   DXGI 交换链 Present 会覆盖窗口的**整个客户区**。若挂在主窗口（1600×900
//   六区 QML 布局）上，输出画面会把整片界面刷掉。故规格第 9 章的「输出视口」
//   用独立窗口承载 —— 即导播软件的 Program 输出窗口语义（可拖到第二显示器）。
//
// 职责边界（与 rhi.h 一致）：
//   · Qt 侧只创建/持有窗口与句柄生命周期，**不碰任何 D3D/DXGI 对象**；
//   · 交换链的建立/重建/呈现全部由引擎在渲染所有者线程（KernelWorker 的
//     捕获线程）内完成 —— 故 GUI 线程与渲染线程无共享 RHI 对象。
//
// 降级语义：无窗口输出能力（非 Windows / 无 D3D11）或建链失败时，本类自动
// 隐藏窗口并发出 degraded()，整体退化为「仅离屏 + 预监」，**不是致命错误**。
// =============================================================================
#pragma once

#include <QObject>
#include <QString>

class QWindow;

namespace sm::desktop {

class OutputWindow : public QObject {
  Q_OBJECT
 public:
  explicit OutputWindow(QObject* parent = nullptr);
  ~OutputWindow() override;

  // 创建并显示输出窗口，并把窗口句柄与客户区尺寸登记到引擎。
  // preferred_* 为期望的客户区尺寸（默认 1920×1080，即 1080p60 输出视口）。
  // 返回 false = 无窗口输出能力或窗口创建失败（已降级，调用方可忽略）。
  bool open(int preferred_width = 1920, int preferred_height = 1080);

  // 关闭输出窗口（幂等）：先通知引擎停止呈现，再销毁窗口。
  void close();

  // 输出窗口是否已创建且可见（未打开 / 已关闭 / 已降级未建窗时为 false）。
  // 注：引擎侧的可用性（交换链是否被判定失败）需另查
  // platform::is_output_window_open() / last_output_window_error()。
  bool isOpen() const;

 signals:
  // 降级通知（供状态栏/日志取证；reason 为空表示恢复可用）
  void degraded(const QString& reason);

 private:
  QWindow* window_ = nullptr;  // 顶层原生窗口（DXGI 交换链宿主）
};

}  // namespace sm::desktop
