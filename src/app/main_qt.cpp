// sm_desktop —— Qt 桌面端程序入口（P1-1-D）
// -----------------------------------------------------------------------------
// 与 sm_kernel_smoke / sm_headless 并列的第三个可执行：仅当
// SM_BUILD_DESKTOP=ON（需 Qt 6.7+）时由顶层 CMakeLists 构建。
// 装配职责全部在 UiController（src/app/qt/ui_controller.cpp），本文件
// 只负责 QGuiApplication 生命周期与退出码。
// =============================================================================
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QStringLiteral>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

#include "app/qt/ui_controller.h"

namespace {

// 日志落盘（CI 可观测性，P1-1-D）
// -----------------------------------------------------------------------------
// 本可执行在 Windows 上以 WIN32_EXECUTABLE TRUE 构建，即 GUI 子系统、无控制台。
// Qt 在「无控制台」进程里默认把日志交给 OutputDebugString，而 stdout/stderr
// 恒为空 —— 这正是 CI 上 --smoke 退出 1（QML 主窗口装配失败）却看不到任何
// 报错的原因。这里显式安装消息处理器：设置 SM_QT_LOG=<文件路径> 时，全部
// qDebug/qInfo/qWarning/qCritical/qFatal 追加写入该文件；同时始终写 stderr，
// 使失败原因在 GUI 与 CUI 两类环境下都可取证。
// 注意：处理器在 QGuiApplication 构造之前安装，平台插件加载失败等早期消息
// 同样能被捕获。
QMutex g_log_mutex;
QString g_log_path;

void smQtMessageHandler(QtMsgType type, const QMessageLogContext& ctx,
                        const QString& msg) {
  const char* level = "info";
  switch (type) {
    case QtDebugMsg:
      level = "debug";
      break;
    case QtInfoMsg:
      level = "info";
      break;
    case QtWarningMsg:
      level = "warning";
      break;
    case QtCriticalMsg:
      level = "critical";
      break;
    case QtFatalMsg:
      level = "fatal";
      break;
  }
  const QString line =
      QStringLiteral("[%1] [%2] %3: %4\n")
          .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
               QString::fromLatin1(level),
               QString::fromLatin1(ctx.category ? ctx.category : "default"),
               msg);

  {
    QMutexLocker locker(&g_log_mutex);
    if (!g_log_path.isEmpty()) {
      QFile f(g_log_path);
      if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        f.write(line.toUtf8());
        f.flush();
      }
    }
  }

  const QByteArray raw = line.toLocal8Bit();
  std::fputs(raw.constData(), stderr);
  std::fflush(stderr);

  if (type == QtFatalMsg) std::abort();
}

}  // namespace

int main(int argc, char* argv[]) {
  // 先于 QGuiApplication 安装：确保 QML 加载期的 warning/critical 全部留痕。
  g_log_path = qEnvironmentVariable("SM_QT_LOG");
  qInstallMessageHandler(smQtMessageHandler);

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
  if (ctrl.show() != 0) {
    qCritical("sm_desktop --smoke: QML 主窗口装配失败，退出码 1");
    return 1;  // 主窗口加载失败：日志已落盘/写 stderr 后直接退出
  }
  if (smoke) {
    qInfo("sm_desktop --smoke: QML 主窗口装配成功，2.5s 后自动退出");
    QTimer::singleShot(2500, &app, &QGuiApplication::quit);
  }
  return QGuiApplication::exec();
}
