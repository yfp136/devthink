// ShowMaster Qt 桌面端 Kernel 宿主桥（P1-1-B）
// -----------------------------------------------------------------------------
// 线程模型（与规格 §11.4.1 一致，全部写入均发生在 worker 线程）：
//   GUI 线程 (QML/main_qt)  ── 队列信号 ──▶  KernelWorker（专用 QThread）
//                                                 │ 持有 sm::Kernel（M1-M5）
//                                                 │ 2s 心跳 tick / 10Hz PGM 抓帧
//                                                 ▼
//   MsgBus 总线事件 / 命令回复 / 状态 / 帧 ── 队列信号 ──▶ GUI 线程（QML 消费）
//
// 命令/回复约定（msg_bus.cpp 路由事实）：
//   * 引擎事件 make_event(...) 的 dst 固定为 "*"，广播给全部已注册 sink；
//   * 命令回复 make_reply(...) 的 dst 回填 request.src。若该地址无人注册，
//     回复会被 Kernel 的默认 sink 静默丢弃（默认 sink 只打印 unrouted cmd）。
//   因此本桥以固定源地址 engine.ui.desktop 发命令，并注册同址 sink 回收回复，
//   再注册 "*" sink 接收事件。两 sink 各自按 type 过滤，避免 "*" 广播重复。
//
// 传输层 dst 映射陷阱（已在 P1-1 调研确认）：
//   headless 将 transport.* 也映射到 engine.media，但该 sink 只认 media.*，
//   transport.* 实际由 engine.timeline sink 处理。故桥接层对常用操作直接走
//   Kernel 快捷方法（scene_go / transport_* / playlist_*，kernel.cpp），
//   不走总线，天然规避 dst 映射错误；其余指令由 QML 经 postCommand 显式
//   指定 dst（engine.media / engine.timeline / engine.scene / engine.playlist）。
#pragma once

#include <memory>
#include <string>

#include <QObject>
#include <QString>
#include <QThread>

#include "engines/kernel.h"

namespace sm::desktop {

// 本桥的总线源地址/回复地址（唯一、未与其他引擎冲突，§5.1 寻址约定）
inline constexpr const char* kKernelHostAddr = "engine.ui.desktop";

// -----------------------------------------------------------------------------
// KernelWorker：生命周期全部绑定在专用 QThread 上。
// 通过 QMetaObject::invokeMethod(..., Qt::QueuedConnection) 从 GUI 线程投递调用；
// 通过信号（跨线程自动排队）把事件/状态/帧回流到 GUI 线程。
// -----------------------------------------------------------------------------
class KernelWorker : public QObject {
  Q_OBJECT
 public:
  explicit KernelWorker(QObject* parent = nullptr);
  ~KernelWorker() override;

  bool kernel_ready() const { return kernel_ != nullptr; }

 public slots:
  // 在 worker 线程内构造 Kernel 并启动（init → start），注册总线 sink 与定时器。
  // 必须由宿主以 QueuedConnection 投递，确保 Kernel 构造于本线程。
  void initKernel();

  // 停止引擎并释放 Kernel；必须由宿主以 Blocking/Queued 投递到本线程执行。
  void shutdownKernel();

  // 向总线投递一条命令（src 固定 engine.ui.desktop）。paramsJson 非法时按空对象处理。
  void postCommand(const QString& dst, const QString& op, const QString& paramsJson);

  // 状态快照：kernel->get_status_json() → statusChanged(json)
  void requestStatus();

  // 播放列表快照：kernel->get_playlist_json() → playlistChanged(json)
  // （playlist.state 仅回 {state,current_index,item_count}；此处给出完整
  //   items[] 列表，供右侧播放列表面板渲染，不依赖 evt 名称猜测。）
  void requestPlaylist();

  // ---- 快捷操作（对应 Kernel 快捷方法，kernel.cpp：直接调用，低延迟）----
  void transportPlay(const QString& mediaId);
  void transportStop();
  void transportPause();
  void transportResume();
  void sceneGo(const QString& sceneId, int fadeMs);
  void playlistGo();
  void playlistStart();
  void playlistStop();
  void playlistNext();

 signals:
  // 引擎事件/命令回复（JSON 信封文本）。GUI 侧据此驱动各面板（W10：事件驱动，非轮询）
  void busEvent(const QString& json);
  // 权威状态快照 JSON（mode/play/media/playlist/scene_count/media_count/engines）
  void statusChanged(const QString& json);
  // 权威播放列表快照 JSON（state/current_index/items[]）
  void playlistChanged(const QString& json);
  // PGM 实时帧（JPEG base64，约 10Hz，仅播放/暂停期间）
  void frameReady(const QString& base64Jpeg);
  void kernelReady();
  void fatalError(const QString& message);

 private:
  // 2s 心跳：tick 判定 + 按引擎注册表逐个 note（语义与 headless 主循环一致）
  void onHeartbeatTick();
  // ~100ms PGM 抓帧：仅 media playing/paused，与上一帧相同则去重不推
  void onPgmPollTick();

  // 总线事件/回复转发（在线程边界安全：signal emit 由 Qt 自动排队）
  void onBusSinkEvent(const sm::Envelope& e, bool is_reply);

  std::unique_ptr<sm::Kernel> kernel_;
  QString last_pgm_jpeg_;  // 跨素材去重缓存（离开播放态后复位）
};

// -----------------------------------------------------------------------------
// KernelHost：GUI 线程门面。QML 以 context property 访问；
// 所有下行调用经队列投递到 KernelWorker，上行经信号转发。
// -----------------------------------------------------------------------------
class KernelHost : public QObject {
  Q_OBJECT
 public:
  explicit KernelHost(QObject* parent = nullptr);
  ~KernelHost() override;

  // 启动 worker 线程并在其上初始化 Kernel（幂等：重复调用仅提示）
  Q_INVOKABLE void start();

  // ---- 转发给 worker 的下行接口（QML 可直接调用）----
  Q_INVOKABLE void postCommand(const QString& dst, const QString& op,
                               const QString& paramsJson);
  Q_INVOKABLE void requestStatus();
  Q_INVOKABLE void requestPlaylist();

  Q_INVOKABLE void transportPlay(const QString& mediaId);
  Q_INVOKABLE void transportStop();
  Q_INVOKABLE void transportPause();
  Q_INVOKABLE void transportResume();
  Q_INVOKABLE void sceneGo(const QString& sceneId, int fadeMs = -1);
  Q_INVOKABLE void playlistGo();
  Q_INVOKABLE void playlistStart();
  Q_INVOKABLE void playlistStop();
  Q_INVOKABLE void playlistNext();

 signals:
  void busEvent(const QString& json);
  void statusChanged(const QString& json);
  void playlistChanged(const QString& json);
  void frameReady(const QString& base64Jpeg);
  void kernelReady();
  void fatalError(const QString& message);

 private:
  QThread thread_;
  KernelWorker* worker_ = nullptr;
};

}  // namespace sm::desktop
