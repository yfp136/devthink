// OutputWindow 实现（P1-2 收尾）—— 设计说明与职责边界见 output_window.h。
// -----------------------------------------------------------------------------
// 本文件是「P1-2 输出窗口」在桌面端的**唯一调用方接线**：
//   Qt 顶层窗口（QWindow）── 句柄 + 客户区尺寸 ──▶ platform::open_output_window
//   窗口尺寸变化        ──▶ platform::resize_output_window
//   关闭 / 析构         ──▶ platform::close_output_window
// 交换链的建立/重建/呈现全在引擎渲染所有者线程内完成（media_backend.h 契约），
// 故本文件不出现任何 D3D/DXGI 类型 —— GUI 线程与渲染线程无共享 RHI 对象。
//
// 失败一律降级（非致命）：无窗口输出能力、句柄/尺寸非法、建链失败三种情况都
// 只是让输出视口不可用，预监（frameReady JPEG）与离屏合成不受影响。
// =============================================================================
#include "app/qt/output_window.h"

#include <QDebug>   // qWarning()/qInfo() 的 operator<< 需要 QDebug 完整类型（MSVC error C2027）
#include <QString>
#include <QWindow>
#include <QtGlobal>

#include <string>

#include "platform/media_backend.h"

namespace sm::desktop {

namespace {

// QWindow::winId() → 引擎需要的原生窗口句柄（Windows = HWND）。
// 非 Windows 平台恒为 nullptr：此时 has_output_window_capability() 亦为 false，
// 调用方已在更早的能力自检处返回，本函数只是兜底。
void* native_handle_of(QWindow* window) {
#if defined(_WIN32)
  return window != nullptr ? reinterpret_cast<void*>(window->winId()) : nullptr;
#else
  (void)window;
  return nullptr;
#endif
}

// 把引擎侧原因转成可读文本；reason 为空时给出泛化说明（避免空日志）。
QString degrade_reason(const char* fallback) {
  const std::string err = sm::platform::last_output_window_error();
  if (!err.empty()) return QString::fromStdString(err);
  return QString::fromLatin1(fallback);
}

}  // namespace

OutputWindow::OutputWindow(QObject* parent) : QObject(parent) {}

OutputWindow::~OutputWindow() { close(); }

bool OutputWindow::open(int preferred_width, int preferred_height) {
  if (window_ != nullptr) return true;  // 幂等：已打开则不再建窗

  // 1) 能力自检。无窗口输出能力（非 Windows / 未编 D3D11）直接降级，
  //    连窗口都不创建 —— headless、CI 冒烟、stub 构建均属预期场景。
  if (!sm::platform::has_output_window_capability()) {
    const QString reason =
        degrade_reason("无本地窗口输出能力（当前平台不支持 D3D11 输出）");
    qWarning().noquote() << "OutputWindow: 输出视口不可用，降级为「仅离屏 + 预监」："
                         << reason;
    emit degraded(reason);
    return false;
  }

  // 2) 建立宿主窗口：独立顶层窗口（可拖到第二显示器），客户区尺寸即输出
  //    分辨率。必须是独立窗口 —— Present 会覆盖整个客户区，挂在六区 QML
  //    主窗口上会把整片界面刷掉（见 output_window.h 的设计说明）。
  QWindow* win = new QWindow();
  win->setFlags(Qt::Window);
  win->setTitle(QStringLiteral("ShowMaster 输出视口 (Program)"));
  win->resize(preferred_width > 0 ? preferred_width : 1920,
              preferred_height > 0 ? preferred_height : 1080);
  win->show();

  // 3) 登记句柄 + 客户区尺寸。winId() 在此强制原生窗口创建，取得真实 HWND；
  //    实际交换链由渲染线程在首次捕获时按需建立（延迟建链）。
  void* hwnd = native_handle_of(win);
  if (hwnd == nullptr ||
      !sm::platform::open_output_window(hwnd, win->width(), win->height())) {
    const QString reason = degrade_reason("输出窗口登记被拒（句柄或尺寸非法）");
    qWarning().noquote() << "OutputWindow: 输出视口降级（仅离屏 + 预监）："
                         << reason;
    win->hide();
    delete win;
    emit degraded(reason);
    return false;
  }

  window_ = win;

  // 4) 尺寸跟随：resize/maximize 后把新客户区尺寸投递给引擎（异步登记，
  //    下一次捕获时 ResizeBuffers）。失败按 rhi.h 契约保持旧尺寸。
  connect(win, &QWindow::widthChanged, this, [this](int) {
    if (window_ != nullptr) {
      sm::platform::resize_output_window(window_->width(), window_->height());
    }
  });
  connect(win, &QWindow::heightChanged, this, [this](int) {
    if (window_ != nullptr) {
      sm::platform::resize_output_window(window_->width(), window_->height());
    }
  });

  qInfo().noquote() << "OutputWindow: 输出视口已登记" << win->width() << "x"
                    << win->height() << "（交换链由渲染线程按需建立）";
  emit degraded(QString());  // 空串 = 恢复可用
  return true;
}

void OutputWindow::close() {
  if (window_ == nullptr) return;  // 幂等

  // 顺序不可颠倒：先解除引擎登记（渲染线程在下一次捕获时停止呈现并释放
  // 交换链），再销毁宿主窗口 —— 避免窗口先死后仍有 Present 投递。
  sm::platform::close_output_window();
  window_->hide();
  delete window_;
  window_ = nullptr;

  qInfo("OutputWindow: 输出视口已关闭（后续仅离屏 + 预监）");
}

bool OutputWindow::isOpen() const {
  return window_ != nullptr && window_->isVisible();
}

}  // namespace sm::desktop
