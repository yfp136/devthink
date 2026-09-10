// UiController 实现（P1-1-D），参见 ui_controller.h 装配说明。
#include "app/qt/ui_controller.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

namespace sm::desktop {

UiController::UiController(QObject* parent) : QObject(parent) {}

UiController::~UiController() = default;

int UiController::show() {
  // 1) 注入内核桥上下文属性。QML 侧经 kernelBridge 直接调用（下行）
  //    与信号连接（上行），面板缺失注入时仅告警不崩溃（独立预览态）。
  engine_.rootContext()->setContextProperty(QStringLiteral("kernelBridge"),
                                            &host_);

  // 2) 加载主窗口。qml.qrc 以前缀 /qml 保真登记目录结构：
  //    qrc:/qml/Main.qml 顶层骨架；qrc:/qml/panels/ 六个面板经
  //    Main.qml 的 import "panels" 解析；qrc:/qml/UiStyle.js 为
  //    .pragma library 样式单例，面板以 import "../UiStyle.js" 引用。
  engine_.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine_.rootObjects().isEmpty()) {
    qWarning("UiController: 主窗口加载失败（qrc:/qml/Main.qml）");
    return 1;
  }

  // 3) QML 事件循环已就绪后启动内核：kernelReady/statusChanged 等上行
  //    信号已在 Main.qml Component.onCompleted 中连接，不会因时序漏接。
  host_.start();
  return 0;
}

}  // namespace sm::desktop
