// KernelWorker / KernelHost 实现（P1-1-B）
// 参见 kernel_host.h 的线程模型与路由约定。
#include "kernel_host.h"

#include <exception>
#include <utility>

#include <QTimer>
#include <QtGlobal>

#include "core/envelope.h"
#include "core/util.h"
#include "engines/engine_registry.h"
#include "engines/media/media_engine.h"
#include "nlohmann/json.hpp"
#include "platform/media_backend.h"

namespace sm::desktop {

// ============================================================================
// KernelWorker
// ============================================================================
KernelWorker::KernelWorker(QObject* parent) : QObject(parent) {}

KernelWorker::~KernelWorker() = default;

void KernelWorker::initKernel() {
  if (kernel_) {
    emit statusChanged(QStringLiteral("{\"error\":\"kernel already running\"}"));
    return;
  }
  try {
    kernel_ = std::make_unique<sm::Kernel>();
    kernel_->init();
    kernel_->start();

    // 事件回流：引擎事件 dst="*" 广播给本 sink（type==evt 才转发）。
    kernel_->bus().register_sink("*", [this](const sm::Envelope& e) {
      onBusSinkEvent(e, /*is_reply=*/false);
    });
    // 回复回流：命令回复 dst=engine.ui.desktop 精确投递到本 sink
    // （type==rsp/err 才转发；"*" 广播事件也会到达此处，故须按 type 过滤）。
    kernel_->bus().register_sink(kKernelHostAddr, [this](const sm::Envelope& e) {
      onBusSinkEvent(e, /*is_reply=*/true);
    });

    // 心跳：每 2s tick 判定 + 引擎注册表逐个 note（与 headless 主循环同语义）。
    auto* hb_timer = new QTimer(this);
    hb_timer->setTimerType(Qt::CoarseTimer);
    hb_timer->setInterval(static_cast<int>(sm::HeartbeatMonitor::kIntervalMs));
    connect(hb_timer, &QTimer::timeout, this, &KernelWorker::onHeartbeatTick);
    hb_timer->start();

    // PGM 实时预览：~100ms 抓帧（≈10Hz），仅播放/暂停期间有效。
    auto* pgm_timer = new QTimer(this);
    pgm_timer->setTimerType(Qt::PreciseTimer);
    pgm_timer->setInterval(100);
    connect(pgm_timer, &QTimer::timeout, this, &KernelWorker::onPgmPollTick);
    pgm_timer->start();

    emit kernelReady();
    requestStatus();  // 初始状态快照
  } catch (const std::exception& e) {
    kernel_.reset();
    emit fatalError(QString::fromUtf8(e.what()));
  } catch (...) {
    kernel_.reset();
    emit fatalError(QStringLiteral("unknown error during kernel init"));
  }
}

void KernelWorker::shutdownKernel() {
  if (kernel_) {
    kernel_->stop();
    kernel_.reset();
  }
  last_pgm_jpeg_.clear();
}

void KernelWorker::postCommand(const QString& dst, const QString& op,
                               const QString& paramsJson) {
  if (!kernel_ || dst.isEmpty() || op.isEmpty()) return;

  nlohmann::json params = nlohmann::json::object();
  if (!paramsJson.isEmpty()) {
    auto parsed = nlohmann::json::parse(paramsJson.toStdString(), nullptr,
                                        /*ignore_comments=*/false);
    if (parsed.is_object()) params = std::move(parsed);
  }

  // src 固定为本桥地址；引擎以 make_reply 回复到同址，由上方 sink 回收。
  kernel_->bus().post(sm::make_cmd(kKernelHostAddr, dst.toStdString(),
                                   op.toStdString(), params));
}

void KernelWorker::requestStatus() {
  if (!kernel_) return;
  emit statusChanged(QString::fromStdString(kernel_->get_status_json()));
}

void KernelWorker::requestPlaylist() {
  if (!kernel_) return;
  emit playlistChanged(QString::fromStdString(kernel_->get_playlist_json()));
}

void KernelWorker::transportPlay(const QString& mediaId) {
  if (!kernel_) return;
  kernel_->transport_play(mediaId.toStdString());
}

void KernelWorker::transportStop() {
  if (!kernel_) return;
  kernel_->transport_stop();
}

void KernelWorker::transportPause() {
  if (!kernel_) return;
  kernel_->transport_pause();
}

void KernelWorker::transportResume() {
  if (!kernel_) return;
  kernel_->transport_resume();
}

void KernelWorker::sceneGo(const QString& sceneId, int fadeMs) {
  if (!kernel_) return;
  kernel_->scene_go(sceneId.toStdString(), fadeMs);
}

void KernelWorker::playlistGo() {
  if (!kernel_) return;
  kernel_->playlist_go();
}

void KernelWorker::playlistStart() {
  if (!kernel_) return;
  kernel_->playlist_start();
}

void KernelWorker::playlistStop() {
  if (!kernel_) return;
  kernel_->playlist_stop();
}

void KernelWorker::playlistNext() {
  if (!kernel_) return;
  kernel_->playlist_next();
}

void KernelWorker::onHeartbeatTick() {
  if (!kernel_) return;
  const std::int64_t now = sm::now_monotonic_ms();
  kernel_->heartbeat().tick(now);
  for (const auto& desc : sm::engine_registry())
    kernel_->heartbeat().note_heartbeat(desc.id, now);
  // 心跳推进后推送权威状态快照（2Hz，供状态栏/面板兜底刷新）
  requestStatus();
  // 播放列表快照同步推（完整 items[]；驱动右侧播放列表栏，与状态同频）
  requestPlaylist();
}

void KernelWorker::onPgmPollTick() {
  if (!kernel_) return;
  const sm::media::PlayState ps = kernel_->media().state();
  if (ps == sm::media::PlayState::playing || ps == sm::media::PlayState::paused) {
    const std::string jpeg = sm::platform::capture_pgm_frame_jpeg();
    if (!jpeg.empty()) {
      const QString q = QString::fromStdString(jpeg);
      if (q != last_pgm_jpeg_) {  // 静止画面与上一帧相同不重复推
        last_pgm_jpeg_ = q;
        emit frameReady(last_pgm_jpeg_);
      }
    }
  } else if (!last_pgm_jpeg_.isEmpty()) {
    // 离开播放/暂停态即复位，避免跨素材复用旧帧去重
    last_pgm_jpeg_.clear();
  }
}

void KernelWorker::onBusSinkEvent(const sm::Envelope& e, bool is_reply) {
  if (is_reply) {
    if (e.type != "rsp" && e.type != "err") return;
  } else {
    if (e.type != "evt") return;
  }
  emit busEvent(QString::fromStdString(sm::envelope_to_json(e)));
}

// ============================================================================
// KernelHost
// ============================================================================
KernelHost::KernelHost(QObject* parent) : QObject(parent) {
  // worker 无父对象：稍后 moveToThread；线程亲和由 QThread 接管
  worker_ = new KernelWorker();
  worker_->moveToThread(&thread_);

  // worker（专用线程）→ host（GUI 线程）：跨线程信号自动排队
  connect(worker_, &KernelWorker::busEvent, this, &KernelHost::busEvent);
  connect(worker_, &KernelWorker::statusChanged, this, &KernelHost::statusChanged);
  connect(worker_, &KernelWorker::playlistChanged, this,
          &KernelHost::playlistChanged);
  connect(worker_, &KernelWorker::frameReady, this, &KernelHost::frameReady);
  connect(worker_, &KernelWorker::kernelReady, this, &KernelHost::kernelReady);
  connect(worker_, &KernelWorker::fatalError, this, &KernelHost::fatalError);
}

KernelHost::~KernelHost() {
  if (thread_.isRunning()) {
    // 在 worker 线程内停止引擎并释放 Kernel，避免跨线程析构
    QMetaObject::invokeMethod(worker_, "shutdownKernel",
                              Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
  }
  delete worker_;
  worker_ = nullptr;
}

void KernelHost::start() {
  if (thread_.isRunning()) return;
  thread_.start();
  // 队列投递：Kernel 在 worker 线程内构造
  QMetaObject::invokeMethod(worker_, "initKernel", Qt::QueuedConnection);
}

void KernelHost::postCommand(const QString& dst, const QString& op,
                             const QString& paramsJson) {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "postCommand", Qt::QueuedConnection,
                            Q_ARG(QString, dst), Q_ARG(QString, op),
                            Q_ARG(QString, paramsJson));
}

void KernelHost::requestStatus() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "requestStatus", Qt::QueuedConnection);
}

void KernelHost::requestPlaylist() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "requestPlaylist", Qt::QueuedConnection);
}

void KernelHost::transportPlay(const QString& mediaId) {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "transportPlay", Qt::QueuedConnection,
                            Q_ARG(QString, mediaId));
}

void KernelHost::transportStop() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "transportStop", Qt::QueuedConnection);
}

void KernelHost::transportPause() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "transportPause", Qt::QueuedConnection);
}

void KernelHost::transportResume() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "transportResume", Qt::QueuedConnection);
}

void KernelHost::sceneGo(const QString& sceneId, int fadeMs) {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "sceneGo", Qt::QueuedConnection,
                            Q_ARG(QString, sceneId), Q_ARG(int, fadeMs));
}

void KernelHost::playlistGo() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "playlistGo", Qt::QueuedConnection);
}

void KernelHost::playlistStart() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "playlistStart", Qt::QueuedConnection);
}

void KernelHost::playlistStop() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "playlistStop", Qt::QueuedConnection);
}

void KernelHost::playlistNext() {
  if (!thread_.isRunning()) return;
  QMetaObject::invokeMethod(worker_, "playlistNext", Qt::QueuedConnection);
}

}  // namespace sm::desktop
