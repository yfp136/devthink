// sm_desktop —— Qt 桌面端程序入口（P1-1-D）
// -----------------------------------------------------------------------------
// 与 sm_kernel_smoke / sm_headless 并列的第三个可执行：仅当
// SM_BUILD_DESKTOP=ON（需 Qt 6.7+）时由顶层 CMakeLists 构建。
// 装配职责全部在 UiController（src/app/qt/ui_controller.cpp），本文件
// 只负责 QGuiApplication 生命周期与退出码。
// =============================================================================
#include <QGuiApplication>
#include <QStringLiteral>
#include <QTimer>

#include "app/qt/ui_controller.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("ShowMaster"));
  app.setOrganizationName(QStringLiteral("ShowMaster"));
  app.setApplicationVersion(QStringLiteral("0.1.0"));

  // --smoke：自检/CI 冒烟模式（出口项 E3 的自动化断言）。语义分层：
  //   · ctrl.show() != 0 —— QML 主窗口装配失败（qrc:/qml/Main.qml 解析、
  //     panels/UiStyle.js 导入、kernelBridge 注入任一失败导致无根对象），
  //     直接返回 1，是 CI 可断言的失败信号；
  //   · show() == 0 —— 再进入事件循环 2.5s，让 Component.onCompleted 与
  //     首轮 kernelReady/statusChanged 上行信号真实执行一次后正常退出，
  //     退出码 0 表示装配 + 首轮事件处理冒烟通过；
  //   · 事件循环期内崩溃 —— 进程异常退出码非 0，同样被 CI 捕获。
  // 交互式启动不带 --smoke，走常规 exec() 事件循环直到窗口关闭。
  const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));

  sm::desktop::UiController ctrl;
  if (ctrl.show() != 0) return 1;  // 主窗口加载失败：打印后直接退出
  if (smoke) {
    qInfo("sm_desktop --smoke: QML 主窗口装配成功，2.5s 后自动退出");
    QTimer::singleShot(2500, &app, &QGuiApplication::quit);
  }
  return QGuiApplication::exec();
}
