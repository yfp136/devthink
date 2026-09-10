// ShowMaster Qt 桌面端 —— UI 装配控制器（P1-1-D）
// -----------------------------------------------------------------------------
// 职责：Qt GUI 侧唯一装配层，把 kernel_host 桥接层与 QML 界面粘合：
//   1) 以 context property "kernelBridge" 注入 KernelHost（Main.qml 契约，
//      面板发送一律经 kernelBridge.postCommand，不直接依赖 C++ 类型）；
//   2) 加载主窗口 qrc:/qml/Main.qml（qml.qrc 前缀 /qml，目录结构保真，
//      使 Main.qml 的 import "panels" 与面板 import "../UiStyle.js"
//      （.pragma library 样式单例）均按资源目录解析）；
//   3) 装配本地输出视口（P1-2 收尾）：独立顶层窗口承载 DXGI 交换链，把
//      句柄 + 客户区尺寸登记给引擎，由渲染线程每帧 present(PGM 合成结果)；
//      失败自动降级为「仅离屏 + 预监」，不是致命错误（见 output_window.h）；
//   4) 全部就绪后启动内核 worker（顺序保证：QML 的 Component.onCompleted
//      已在加载期完成信号连接，kernelReady / statusChanged 上行不会漏接；
//      输出登记先于 worker 启动，故首帧起即可直出到输出视口）。
//
// 线程模型（与 kernel_host.h 一致）：本类与 QML 同在 GUI 线程；Kernel 与
// MsgBus 运行在 KernelHost 内部专用线程，经队列信号收发，此处不接触内核。
// 输出视口同理：Qt 侧只投递 HWND/尺寸，D3D/DXGI 对象全部由引擎在渲染线程持有。
// =============================================================================
#pragma once

#include <QQmlApplicationEngine>

#include "kernel_host.h"
#include "output_window.h"

namespace sm::desktop {

class UiController : public QObject {
  Q_OBJECT
 public:
  explicit UiController(QObject* parent = nullptr);
  ~UiController() override;

  // 装配并显示主窗口。成功返回 0；QML 加载失败返回 1（由入口决定退出码）。
  int show();

 private:
  // 声明顺序即析构逆序（自下而上）：
  //   out_window_（最后声明）→ 先执行 close()：在 worker 仍在运行时解除引擎的
  //     输出登记，使渲染线程在停机前最后一次捕获即可释放交换链；若仍残留，
  //     亦由引擎静态期回收（DXGI 交换链不依赖窗口存续）。
  //   engine_ → 销毁 QML 根对象，切断其对 host_ 的引用（QML 不引用 out_window_）。
  //   host_（最先声明）→ 最后执行 worker 停机（BlockingQueued），避免跨线程析构 Kernel。
  KernelHost host_;
  QQmlApplicationEngine engine_;
  OutputWindow out_window_;
};

}  // namespace sm::desktop
