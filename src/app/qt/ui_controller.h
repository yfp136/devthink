// ShowMaster Qt 桌面端 —— UI 装配控制器（P1-1-D）
// -----------------------------------------------------------------------------
// 职责：Qt GUI 侧唯一装配层，把 kernel_host 桥接层与 QML 界面粘合：
//   1) 以 context property "kernelBridge" 注入 KernelHost（Main.qml 契约，
//      面板发送一律经 kernelBridge.postCommand，不直接依赖 C++ 类型）；
//   2) 加载主窗口 qrc:/qml/Main.qml（qml.qrc 前缀 /qml，目录结构保真，
//      使 Main.qml 的 import "panels" 与面板 import "../UiStyle.js"
//      （.pragma library 样式单例）均按资源目录解析）；
//   3) 加载完成后启动内核 worker（顺序保证：QML 的 Component.onCompleted
//      已在加载期完成信号连接，kernelReady / statusChanged 上行不会漏接）。
//
// 线程模型（与 kernel_host.h 一致）：本类与 QML 同在 GUI 线程；Kernel 与
// MsgBus 运行在 KernelHost 内部专用线程，经队列信号收发，此处不接触内核。
// =============================================================================
#pragma once

#include <QQmlApplicationEngine>

#include "kernel_host.h"

namespace sm::desktop {

class UiController : public QObject {
  Q_OBJECT
 public:
  explicit UiController(QObject* parent = nullptr);
  ~UiController() override;

  // 装配并显示主窗口。成功返回 0；QML 加载失败返回 1（由入口决定退出码）。
  int show();

 private:
  // 声明顺序即析构逆序：engine_（后声明）先销毁，切断 QML 对 host_ 的引用后，
  // host_ 再执行 worker 停机（BlockingQueued），避免跨线程析构 Kernel。
  KernelHost host_;
  QQmlApplicationEngine engine_;
};

}  // namespace sm::desktop
