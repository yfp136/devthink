// 硬件时序中控引擎（engine.device / DeviceEngine.dll）
// 需求：整包 7x24 常驻的工业现场硬件管控核心
// 功能：
//   1. 设备资产台账（电源时序器 / 继电器 / 投影 / 拼接处理器 / UPS / 传感器）
//   2. 电源时序控制（开机/关机序列，步间延时，防浪涌冲击）
//   3. 动作命令（上电 / 断电 / 重启 / 待机 / 信号切换 / 紧急制动）
//   4. 设备状态监测（在线 / 电源态 / 温度 / 电压，周期轮询）
//   5. 触发联动（场景 / 定时 / 远程指令触发设备序列）
//   6. 硬件 IO 抽象：真实设备走注册的 IO 回调，未注册时内部模拟
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace device {

// 设备类型
enum class DeviceType {
  PowerSequencer,   // 电源时序器（8/12 路）
  Relay,            // 继电器模块
  Projector,        // 投影机
  VideoWall,        // 拼接处理器
  Ups,              // UPS 电源
  Sensor,           // 环境传感器（温湿度/电流）
  Generic           // 通用 IP/串口设备
};

// 设备电源状态
enum class PowerState {
  Off,              // 断电
  Standby,          // 待机（软关机）
  On,               // 运行
  Fault,            // 故障/告警
  Unknown           // 未知（离线）
};

// 设备动作
enum class DeviceAction {
  PowerOn,          // 上电
  PowerOff,         // 断电
  Reboot,           // 重启
  Standby,          // 待机
  EStop,            // 紧急制动（全场断电）
  SetInput,         // 切换输入信号（带 param）
  Mute,             // 静音/屏蔽
  Unmute,           // 取消静音
  Query,            // 查询状态
  Custom            // 自定义指令（param 透传）
};

// 设备台账条目
struct DeviceInfo {
  std::string id;
  std::string name;
  DeviceType type;
  std::string model;            // 型号，用于模拟行为特征
  std::string ip;               // 网络设备地址
  int channel;                  // 串口/继电器通道（0 = N/A）
  int group_id;                 // 分组 ID
  std::string zone;             // 区域（如 "主舞台" / "控台间"）

  // 运行时状态（引擎维护）
  PowerState power = PowerState::Off;
  bool online = false;
  float temperature = 25.0f;    // °C
  float voltage = 0.0f;         // V（UPS/电源通道）
  float load_percent = 0.0f;    // 负载 %
  int64_t last_cmd_at_ms = 0;   // 最后命令时刻（模拟时钟）
  std::string last_error;
};

// 时序步骤：执行动作后等待 delay_ms 再进入下一步
struct PowerSeqStep {
  std::string device_id;
  DeviceAction action;
  std::string param;            // 动作参数（如 SetInput 的信号源）
  int64_t delay_ms = 500;       // 执行完成后延时
};

// 电源时序序列（开机/关机/演出模式一键联动）
struct PowerSequence {
  std::string name;
  std::string description;
  std::vector<PowerSeqStep> steps;
};

// 序列执行进度（status() 暴露给 Web/总线）
struct SeqRunState {
  std::string name;
  bool running = false;
  size_t step_index = 0;        // 当前步骤
  int64_t step_remaining_ms = 0; // 当前步骤剩余延时
  int64_t started_at_ms = 0;
};

// 硬件写入回调：由传输层（Art-Net / TCP / 串口 / 继电器厂商库）注册。
// 返回 false 表示硬件未响应，引擎记录 Fault 并上报。
using IoFn = std::function<bool(const DeviceInfo& dev,
                                DeviceAction action,
                                const std::string& param)>;

// 状态变化事件回调（供总线转发 event.device.*，可为空）
using EventFn = std::function<void(const nlohmann::json& event)>;

class DeviceEngine {
public:
  DeviceEngine();
  ~DeviceEngine();

  // 生命周期
  void init();
  void start();
  void stop();

  // ---- 设备台账 ----
  void add_device(const DeviceInfo& dev);
  void remove_device(const std::string& id);
  bool has_device(const std::string& id) const;
  std::vector<DeviceInfo> get_devices() const;
  std::vector<DeviceInfo> get_by_type(DeviceType type) const;
  std::vector<DeviceInfo> get_by_group(int group_id) const;
  std::vector<DeviceInfo> get_by_zone(const std::string& zone) const;

  // ---- 硬件 IO 绑定（按设备 id；未绑定时走内部模拟）----
  void set_io_fn(const std::string& device_id, IoFn fn);
  void set_event_fn(EventFn fn);

  // ---- 电源时序 ----
  void define_sequence(const PowerSequence& seq);
  bool trigger_sequence(const std::string& name);   // 立即启动
  void abort_sequence();                            // 急停当前序列
  SeqRunState sequence_state() const;

  // ---- 动作执行 ----
  // 立即执行；delay_ms > 0 时排入延时队列（由 update() 推进）
  bool exec_action(const std::string& device_id,
                   DeviceAction action,
                   const std::string& param = "",
                   int64_t delay_ms = 0);

  // 一键联控：对全部/某组设备按序执行同一动作（自动加入步骤间隔）
  int batch_action(DeviceAction action, int group_id = -1,
                   int64_t step_gap_ms = 300);

  // ---- 模拟时钟推进（宿主每帧/每 tick 调用）----
  void update(int64_t elapsed_ms);

  // ---- 状态 ----
  nlohmann::json status() const;

private:
  std::map<std::string, DeviceInfo> devices_;
  std::map<std::string, IoFn> io_fns_;
  std::map<std::string, PowerSequence> sequences_;
  EventFn event_fn_;

  // 延时动作队列：到期时刻(ms) → 描述
  struct PendingAction {
    int64_t due_ms;
    std::string device_id;
    DeviceAction action;
    std::string param;
  };
  std::vector<PendingAction> pending_actions_;

  SeqRunState run_;
  int64_t clock_ms_ = 0;         // 模拟时钟（update 传入的累计值）
  int64_t last_poll_ms_ = 0;     // 轮询计时
  bool running_ = false;

  DeviceInfo* find_device(const std::string& id);
  bool apply_action(DeviceInfo& dev, DeviceAction action,
                    const std::string& param, bool via_hw);
  void emit_change(const DeviceInfo& dev, const std::string& what);
  void poll_devices();           // 周期状态巡检（模拟遥测漂移）
};

}  // namespace device
}  // namespace sm
