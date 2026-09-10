// UiController 实现（P1-1-D），参见 ui_controller.h 装配说明。
#include "app/qt/ui_controller.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QUrl>

namespace sm::desktop {

UiController::UiController(QObject* parent) : QObject(parent) {}

UiController::~UiController() = default;

int UiController::show() {
  // 0) 装配前置自检：把「qrc 资源是否已链接进二进制」与「QML 模块搜索路径」
  //    写入日志。--smoke 退出 1 时，这两项足以区分两类根因：
  //    present=false → qml.qrc 未被 AUTORCC 链接（构建期问题）；
  //    present=true 且 import paths 缺少安装目录 → 运行期 QML 模块未部署
  //    （windeployqt 问题），随后加载器会打印 "module ... is not installed"。
  qInfo().noquote() << "UiController: qrc:/qml/Main.qml present ="
                    << QFile::exists(QStringLiteral(":/qml/Main.qml"));
  qInfo().noquote() << "UiController: QML import paths ="
                    << engine_.importPathList().join(QStringLiteral(";"));

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

  // 3) 装配本地输出视口（P1-2 收尾 / 规格第 2 章「1080p60 输出视口」）
  //    ---------------------------------------------------------------------
  //    引擎侧（media_backend.cpp）已把「一次合成 → 窗口直出 + 预监回读」接好，
  //    此处只负责把 Qt 顶层窗口的句柄与客户区尺寸登记进去：
  //      QWindow::winId() ──▶ platform::open_output_window(hwnd, w, h)
  //    交换链由渲染线程（KernelWorker 的捕获线程）按需建立并每帧 present，
  //    GUI 线程不触碰任何 D3D/DXGI 对象。
  //    先连接降级信号再 open()，确保建窗/登记失败的首次降级也被记录。
  //    SM_NO_OUTPUT_WINDOW 可显式关闭输出视口（离屏 + 预监仍完全可用）。
  connect(&out_window_, &OutputWindow::degraded, this,
          [](const QString& reason) {
            if (reason.isEmpty()) {
              qInfo("UiController: 输出视口可用（渲染线程将按需建立交换链）");
            } else {
              qWarning().noquote()
                  << "UiController: 输出视口降级（仅离屏 + 预监）：" << reason;
            }
          });
  if (qEnvironmentVariableIsSet("SM_NO_OUTPUT_WINDOW")) {
    qInfo("UiController: SM_NO_OUTPUT_WINDOW 已设置，跳过输出视口装配");
  } else {
    // 失败非致命：open() 内部已告警并发降级信号，返回 false 无需处理。
    out_window_.open();
  }

  // 4) QML 事件循环已就绪、输出视口已登记，此时启动内核：kernelReady/
  //    statusChanged 等上行信号已在 Main.qml Component.onCompleted 中连接，
  //    不会因时序漏接。
  host_.start();
  return 0;
}

}  // namespace sm::desktop
