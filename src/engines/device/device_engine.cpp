// 硬件时序中控引擎实现
#include "engines/device/device_engine.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace sm {
namespace device {

namespace {

const char* power_state_name(PowerState s) {
  switch (s) {
    case PowerState::Off:     return "off";
    case PowerState::Standby: return "standby";
    case PowerState::On:      return "on";
    case PowerState::Fault:   return "fault";
    default:                  return "unknown";
  }
}

const char* action_name(DeviceAction a) {
  switch (a) {
    case DeviceAction::PowerOn:   return "power_on";
    case DeviceAction::PowerOff:  return "power_off";
    case DeviceAction::Reboot:    return "reboot";
    case DeviceAction::Standby:   return "standby";
    case DeviceAction::EStop:     return "estop";
    case DeviceAction::SetInput:  return "set_input";
    case DeviceAction::Mute:      return "mute";
    case DeviceAction::Unmute:    return "unmute";
    case DeviceAction::Query:     return "query";
    default:                      return "custom";
  }
}

}  // namespace

DeviceEngine::DeviceEngine() = default;
DeviceEngine::~DeviceEngine() { stop(); }

void DeviceEngine::init() {
  // 模拟现场：设备上电后默认在线（真实 IO 轮询会刷新该状态）
  for (auto& [id, dev] : devices_) {
    (void)id;
    dev.online = true;
  }
}

void DeviceEngine::start() {
  running_ = true;
  last_poll_ms_ = clock_ms_;
  std::printf("[device] 硬件中控引擎启动，设备 %zu 台\n", devices_.size());
}

void DeviceEngine::stop() {
  running_ = false;
  pending_actions_.clear();
  run_.running = false;
}

// ---- 设备台账 ----

void DeviceEngine::add_device(const DeviceInfo& dev) {
  devices_[dev.id] = dev;
}

void DeviceEngine::remove_device(const std::string& id) {
  devices_.erase(id);
  io_fns_.erase(id);
}

bool DeviceEngine::has_device(const std::string& id) const {
  return devices_.count(id) > 0;
}

std::vector<DeviceInfo> DeviceEngine::get_devices() const {
  std::vector<DeviceInfo> out;
  out.reserve(devices_.size());
  for (const auto& [id, dev] : devices_) out.push_back(dev);
  return out;
}

std::vector<DeviceInfo> DeviceEngine::get_by_type(DeviceType type) const {
  std::vector<DeviceInfo> out;
  for (const auto& [id, dev] : devices_)
    if (dev.type == type) out.push_back(dev);
  return out;
}

std::vector<DeviceInfo> DeviceEngine::get_by_group(int group_id) const {
  std::vector<DeviceInfo> out;
  for (const auto& [id, dev] : devices_)
    if (dev.group_id == group_id) out.push_back(dev);
  return out;
}

std::vector<DeviceInfo> DeviceEngine::get_by_zone(const std::string& zone) const {
  std::vector<DeviceInfo> out;
  for (const auto& [id, dev] : devices_)
    if (dev.zone == zone) out.push_back(dev);
  return out;
}

// ---- IO 绑定 ----

void DeviceEngine::set_io_fn(const std::string& device_id, IoFn fn) {
  if (fn)
    io_fns_[device_id] = std::move(fn);
  else
    io_fns_.erase(device_id);
}

void DeviceEngine::set_event_fn(EventFn fn) { event_fn_ = std::move(fn); }

// ---- 电源时序 ----

void DeviceEngine::define_sequence(const PowerSequence& seq) {
  sequences_[seq.name] = seq;
}

bool DeviceEngine::trigger_sequence(const std::string& name) {
  auto it = sequences_.find(name);
  if (it == sequences_.end()) return false;
  if (it->second.steps.empty()) return false;
  run_.name = name;
  run_.running = true;
  run_.step_index = 0;
  run_.step_remaining_ms = 0;   // 立即执行第一步
  run_.started_at_ms = clock_ms_;
  std::printf("[device] 时序序列 '%s' 启动（%zu 步）\n",
              name.c_str(), it->second.steps.size());
  return true;
}

void DeviceEngine::abort_sequence() {
  if (!run_.running) return;
  run_.running = false;
  std::printf("[device] 时序序列 '%s' 被中止\n", run_.name.c_str());
}

SeqRunState DeviceEngine::sequence_state() const { return run_; }

// ---- 动作执行 ----

bool DeviceEngine::exec_action(const std::string& device_id,
                               DeviceAction action,
                               const std::string& param,
                               int64_t delay_ms) {
  if (delay_ms > 0) {
    pending_actions_.push_back({clock_ms_ + delay_ms, device_id,
                                action, param});
    return true;  // 已排入队列
  }
  DeviceInfo* dev = find_device(device_id);
  if (!dev) return false;
  return apply_action(*dev, action, param, true);
}

int DeviceEngine::batch_action(DeviceAction action, int group_id,
                               int64_t step_gap_ms) {
  std::vector<DeviceInfo*> targets;
  for (auto& [id, dev] : devices_) {
    (void)id;
    if (group_id < 0 || dev.group_id == group_id) targets.push_back(&dev);
  }
  // 按设备加入顺序排列（map 保证有序 → 时序确定）
  int64_t due = clock_ms_;
  for (auto* dev : targets) {
    pending_actions_.push_back({due, dev->id, action, ""});
    due += step_gap_ms;
  }
  return static_cast<int>(targets.size());
}

// ---- 模拟时钟推进 ----

void DeviceEngine::update(int64_t elapsed_ms) {
  if (!running_) return;
  const int64_t delta = elapsed_ms - clock_ms_;
  clock_ms_ = elapsed_ms;
  if (delta <= 0) return;

  // 1) 延时动作队列
  for (size_t i = 0; i < pending_actions_.size();) {
    if (pending_actions_[i].due_ms <= clock_ms_) {
      PendingAction pa = pending_actions_[i];
      pending_actions_.erase(pending_actions_.begin() +
                             static_cast<std::ptrdiff_t>(i));
      if (DeviceInfo* dev = find_device(pa.device_id))
        apply_action(*dev, pa.action, pa.param, true);
      // 未找到设备：静默丢弃
    } else {
      ++i;
    }
  }

  // 2) 执行中的电源时序
  //    状态机：执行 step_index → 装载其延时 → 倒计时 → 步进下一索引 →
  //    顶部判定结束；delay=0 的步骤执行后立即步进，无需额外 tick
  if (run_.running) {
    const auto sit = sequences_.find(run_.name);
    const size_t total = (sit == sequences_.end())
                             ? 0
                             : sit->second.steps.size();
    if (sit == sequences_.end() || run_.step_index >= total) {
      if (run_.running) {
        run_.running = false;
        if (sit != sequences_.end())
          std::printf("[device] 时序序列 '%s' 完成\n", run_.name.c_str());
      }
    } else if (run_.step_remaining_ms <= 0) {
      // 当前步骤就绪：执行并装载本步骤的延时
      const PowerSeqStep& step = sit->second.steps[run_.step_index];
      if (DeviceInfo* dev = find_device(step.device_id))
        apply_action(*dev, step.action, step.param, true);
      run_.step_remaining_ms = step.delay_ms;
      if (run_.step_remaining_ms <= 0) {
        ++run_.step_index;  // 无需延时：立即步进（下一轮判定结束）
        run_.step_remaining_ms = 0;
      }
    } else {
      // 倒计时当前步骤延时
      run_.step_remaining_ms -= delta;
      if (run_.step_remaining_ms <= 0) {
        ++run_.step_index;
        run_.step_remaining_ms = 0;
      }
    }
  }

  // 3) 周期遥测巡检（每 2 秒模拟一次现场数据漂移）
  if (clock_ms_ - last_poll_ms_ >= 2000) {
    last_poll_ms_ = clock_ms_;
    poll_devices();
  }
}

// ---- 内部 ----

DeviceInfo* DeviceEngine::find_device(const std::string& id) {
  auto it = devices_.find(id);
  return it == devices_.end() ? nullptr : &it->second;
}

// 应用动作：优先走硬件 IO 回调；未绑定 IO 时按设备类型内部模拟。
// via_hw 用于区分“宿主命令”与“模拟回放”，决定是否再次触发事件上报。
bool DeviceEngine::apply_action(DeviceInfo& dev, DeviceAction action,
                                const std::string& param, bool via_hw) {
  const bool online = dev.online;
  const std::string dev_id = dev.id;  // 拷贝，防回调中重入失效

  auto io = io_fns_.find(dev_id);
  if (io != io_fns_.end()) {
    const bool ok = io->second(dev, action, param);
    if (!ok) {
      dev.power = PowerState::Fault;
      dev.last_error = "io no response: " + std::string(action_name(action));
      emit_change(dev, "fault");
      return false;
    }
  }

  // 内部状态机（离线设备拒绝除 PowerOn/Query 外的动作）
  const bool powered = (action != DeviceAction::PowerOff &&
                        action != DeviceAction::Standby &&
                        action != DeviceAction::EStop);
  if (!online && !powered && action != DeviceAction::Query) {
    dev.last_error = "device offline";
    return false;
  }

  dev.last_cmd_at_ms = clock_ms_;
  switch (action) {
    case DeviceAction::PowerOn:
      dev.power = PowerState::On;
      dev.last_error.clear();
      break;
    case DeviceAction::PowerOff:
      dev.power = PowerState::Off;
      break;
    case DeviceAction::Standby:
      dev.power = PowerState::Standby;
      break;
    case DeviceAction::Reboot:
      // 模拟：断电瞬间后自动恢复上电（真实设备由 IO 轮询确认）
      dev.power = PowerState::On;
      break;
    case DeviceAction::EStop:
      dev.power = PowerState::Off;
      abort_sequence();
      break;
    case DeviceAction::SetInput:
      dev.last_error = "input:" + param;  // 记录当前输入源（模拟）
      break;
    case DeviceAction::Mute:
    case DeviceAction::Unmute:
    case DeviceAction::Query:
    case DeviceAction::Custom:
      break;
  }

  if (action == DeviceAction::EStop) {
    // 全场急停：除本设备外其余全部断电（防止浪涌逐路关）
    for (auto& [id, other] : devices_) {
      (void)id;
      if (other.id != dev_id) other.power = PowerState::Off;
    }
    emit_change(dev, "estop");
    return true;
  }

  if (via_hw) emit_change(dev, action_name(action));
  return true;
}

void DeviceEngine::emit_change(const DeviceInfo& dev,
                               const std::string& what) {
  if (!event_fn_) return;
  nlohmann::json ev = {
      {"device", dev.id},
      {"what", what},
      {"power", power_state_name(dev.power)},
      {"online", dev.online},
  };
  event_fn_(ev);
}

void DeviceEngine::poll_devices() {
  for (auto& [id, dev] : devices_) {
    (void)id;
    if (!dev.online) continue;
    // 遥测模拟：温度热漂移 ±0.3°C；电源类设备电压 220~230V、负载缓变
    float drift = static_cast<float>((std::rand() % 60) - 30) / 100.0f;
    dev.temperature += drift;
    if (dev.temperature < 15.0f) dev.temperature = 15.0f;
    if (dev.temperature > 95.0f) dev.temperature = 95.0f;

    switch (dev.type) {
      case DeviceType::PowerSequencer:
      case DeviceType::Ups:
        dev.voltage = 220.0f + static_cast<float>(std::rand() % 101) / 10.0f;
        if (dev.power == PowerState::On)
          dev.load_percent = 15.0f +
              static_cast<float>(std::rand() % 6000) / 100.0f;
        break;
      case DeviceType::Projector:
        if (dev.power == PowerState::On) dev.temperature += 8.0f;
        break;
      case DeviceType::Sensor:
        dev.load_percent = static_cast<float>(std::rand() % 100);
        break;
      default:
        break;
    }
  }
}

nlohmann::json DeviceEngine::status() const {
  nlohmann::json j;
  j["running"] = running_;
  j["devices"] = nlohmann::json::array();
  for (const auto& [id, dev] : devices_) {
    nlohmann::json dj = {
        {"id", dev.id},
        {"name", dev.name},
        {"type", static_cast<int>(dev.type)},
        {"group", dev.group_id},
        {"zone", dev.zone},
        {"online", dev.online},
        {"power", power_state_name(dev.power)},
        {"temperature", dev.temperature},
    };
    if (dev.type == DeviceType::PowerSequencer ||
        dev.type == DeviceType::Ups) {
      dj["voltage"] = dev.voltage;
      dj["load_percent"] = dev.load_percent;
    }
    if (!dev.last_error.empty()) dj["last_error"] = dev.last_error;
    j["devices"].push_back(std::move(dj));
  }
  nlohmann::json sj;
  sj["name"] = run_.name;
  sj["running"] = run_.running;
  sj["step_index"] = run_.step_index;
  sj["started_at_ms"] = run_.started_at_ms;
  j["sequence"] = std::move(sj);
  j["clock_ms"] = clock_ms_;
  j["pending_actions"] = pending_actions_.size();
  return j;
}

}  // namespace device
}  // namespace sm
