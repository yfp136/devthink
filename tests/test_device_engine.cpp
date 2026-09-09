// 硬件时序中控引擎单元测试（engine.device）
// 覆盖：台账管理 / IO 回调 / 内部模拟状态机 / 电源时序执行 /
//       延时动作队列 / EStop 全场急停 / batch 联控 / status JSON
#include <string>
#include <vector>

#include "engines/device/device_engine.h"
#include "test_common.h"

using sm::device::DeviceAction;
using sm::device::DeviceEngine;
using sm::device::DeviceInfo;
using sm::device::DeviceType;
using sm::device::PowerSequence;
using sm::device::PowerState;
using sm::device::PowerSeqStep;

namespace {

DeviceInfo make_dev(const std::string& id, const std::string& name,
                    DeviceType type, int group = 0,
                    const std::string& zone = "主舞台") {
  DeviceInfo d;
  d.id = id;
  d.name = name;
  d.type = type;
  d.group_id = group;
  d.zone = zone;
  d.model = "TestModel";
  return d;
}

// get_devices() 按 map 键序返回，这里按 id 取电源态，避免依赖插入顺序
PowerState power_of(const std::vector<DeviceInfo>& devs,
                    const std::string& id) {
  for (const auto& d : devs)
    if (d.id == id) return d.power;
  return PowerState::Unknown;
}

void test_ledger() {
  DeviceEngine eng;
  eng.add_device(make_dev("seq1", "时序器-1", DeviceType::PowerSequencer, 0));
  eng.add_device(make_dev("relay1", "继电器-1", DeviceType::Relay, 0, "控台间"));
  eng.add_device(make_dev("proj1", "投影-1", DeviceType::Projector, 1));
  SM_CHECK(eng.has_device("seq1"));
  SM_CHECK(!eng.has_device("ghost"));
  SM_CHECK_EQ(eng.get_devices().size(), std::size_t(3));
  SM_CHECK_EQ(eng.get_by_type(DeviceType::Projector).size(), std::size_t(1));
  SM_CHECK_EQ(eng.get_by_group(0).size(), std::size_t(2));
  SM_CHECK_EQ(eng.get_by_zone("控台间").size(), std::size_t(1));
  eng.remove_device("relay1");
  SM_CHECK(!eng.has_device("relay1"));
  SM_CHECK_EQ(eng.get_devices().size(), std::size_t(2));
}

void test_io_callback() {
  DeviceEngine eng;
  eng.add_device(make_dev("relay1", "继电器-1", DeviceType::Relay));
  eng.init();
  eng.start();

  int calls = 0;
  DeviceAction last_action = DeviceAction::Query;
  std::string last_param;
  eng.set_io_fn("relay1",
                [&](const DeviceInfo&, DeviceAction a, const std::string& p) {
                  ++calls;
                  last_action = a;
                  last_param = p;
                  return true;
                });
  SM_CHECK(eng.exec_action("relay1", DeviceAction::PowerOn, "", 0));
  SM_CHECK_EQ(calls, 1);
  SM_CHECK(last_action == DeviceAction::PowerOn);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);

  // IO 返回失败 → 设备置 Fault
  eng.set_io_fn("relay1",
                [](const DeviceInfo&, DeviceAction, const std::string&) {
                  return false;
                });
  SM_CHECK(!eng.exec_action("relay1", DeviceAction::PowerOff, "", 0));
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::Fault);
}

void test_simulated_states() {
  DeviceEngine eng;
  eng.add_device(make_dev("proj1", "投影-1", DeviceType::Projector));
  eng.init();
  eng.start();

  eng.exec_action("proj1", DeviceAction::PowerOn, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);
  eng.exec_action("proj1", DeviceAction::Standby, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::Standby);
  eng.exec_action("proj1", DeviceAction::PowerOff, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::Off);
  eng.exec_action("proj1", DeviceAction::Reboot, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);

  // 未绑定 IO 且设备不存在 → false
  SM_CHECK(!eng.exec_action("ghost", DeviceAction::PowerOn, "", 0));
}

void test_sequence_execution() {
  DeviceEngine eng;
  eng.add_device(make_dev("seq1", "时序器-1", DeviceType::PowerSequencer));
  eng.add_device(make_dev("proj1", "投影-1", DeviceType::Projector));
  eng.add_device(make_dev("wall1", "拼接-1", DeviceType::VideoWall));
  eng.init();
  eng.start();

  PowerSequence boot;
  boot.name = "boot_all";
  boot.steps = {
      PowerSeqStep{"seq1", DeviceAction::PowerOn, "", 100},
      PowerSeqStep{"proj1", DeviceAction::PowerOn, "", 100},
      PowerSeqStep{"wall1", DeviceAction::PowerOn, "", 0},
  };
  eng.define_sequence(boot);

  SM_CHECK(eng.trigger_sequence("boot_all"));
  SM_CHECK(eng.sequence_state().running);

  // 推进模拟时钟：第 1 步（seq1 on, 延时 100）
  eng.update(1);
  SM_CHECK_EQ(power_of(eng.get_devices(), "seq1"), PowerState::On);
  SM_CHECK_EQ(power_of(eng.get_devices(), "proj1"), PowerState::Off);

  // 100ms 后进入第 2 步；再过 1ms 执行 proj1
  eng.update(101);
  eng.update(102);
  SM_CHECK_EQ(power_of(eng.get_devices(), "proj1"), PowerState::On);
  SM_CHECK_EQ(power_of(eng.get_devices(), "wall1"), PowerState::Off);

  // 推进第 3 步（wall1 on）：update(203) 完成步骤切换，下一步 tick 执行
  eng.update(203);
  eng.update(204);
  SM_CHECK_EQ(power_of(eng.get_devices(), "wall1"), PowerState::On);

  // 全部步骤完成 → 序列结束
  eng.update(206);
  SM_CHECK(!eng.sequence_state().running);

  // 未知序列拒绝触发
  SM_CHECK(!eng.trigger_sequence("no_such_seq"));
}

void test_delayed_action_and_batch() {
  DeviceEngine eng;
  eng.add_device(make_dev("a", "A", DeviceType::Relay));
  eng.add_device(make_dev("b", "B", DeviceType::Relay));
  eng.add_device(make_dev("c", "C", DeviceType::Relay));
  eng.init();
  eng.start();

  // 延时动作：200ms 后断电
  eng.exec_action("a", DeviceAction::PowerOn, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);
  eng.exec_action("a", DeviceAction::PowerOff, "", 200);
  eng.update(100);  // 未到期
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);
  eng.update(200);  // 到期
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::Off);

  // 联控：三台设备按 100ms 步间隔依次上电
  eng.update(300);
  SM_CHECK_EQ(eng.batch_action(DeviceAction::PowerOn, -1, 100), 3);
  eng.update(301);  // 执行 due<=301 的动作（300 起步：300/400/500 → 仅第 1 台）
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);
  SM_CHECK_EQ(eng.get_devices().at(1).power, PowerState::Off);
  eng.update(401);
  SM_CHECK_EQ(eng.get_devices().at(1).power, PowerState::On);
  eng.update(502);
  SM_CHECK_EQ(eng.get_devices().at(2).power, PowerState::On);
}

void test_estop() {
  DeviceEngine eng;
  eng.add_device(make_dev("seq1", "时序器-1", DeviceType::PowerSequencer));
  eng.add_device(make_dev("proj1", "投影-1", DeviceType::Projector));
  eng.init();
  eng.start();

  PowerSequence boot;
  boot.name = "boot";
  boot.steps = {PowerSeqStep{"proj1", DeviceAction::PowerOn, "", 5000}};
  eng.define_sequence(boot);
  eng.trigger_sequence("boot");

  eng.exec_action("seq1", DeviceAction::PowerOn, "", 0);
  eng.exec_action("proj1", DeviceAction::PowerOn, "", 0);
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::On);
  SM_CHECK_EQ(eng.get_devices().at(1).power, PowerState::On);

  // 急停：全场断电 + 中止序列
  SM_CHECK(eng.exec_action("seq1", DeviceAction::EStop, "", 0));
  SM_CHECK_EQ(eng.get_devices().at(0).power, PowerState::Off);
  SM_CHECK_EQ(eng.get_devices().at(1).power, PowerState::Off);
  SM_CHECK(!eng.sequence_state().running);
}

void test_status_json() {
  DeviceEngine eng;
  eng.add_device(make_dev("ups1", "UPS-1", DeviceType::Ups));
  eng.init();
  eng.start();
  eng.exec_action("ups1", DeviceAction::PowerOn, "", 0);
  eng.update(2100);  // 触发一次周期遥测

  const nlohmann::json j = eng.status();
  SM_CHECK(j.contains("devices"));
  SM_CHECK_EQ(j["devices"].size(), std::size_t(1));
  const auto& d = j["devices"][0];
  SM_CHECK_EQ(d["id"].get<std::string>(), std::string("ups1"));
  SM_CHECK_EQ(d["power"].get<std::string>(), std::string("on"));
  SM_CHECK(d.contains("voltage"));
  SM_CHECK_EQ(j["sequence"]["running"].get<bool>(), false);
}

}  // namespace

int main() {
  test_ledger();
  test_io_callback();
  test_simulated_states();
  test_sequence_execution();
  test_delayed_action_and_batch();
  test_estop();
  test_status_json();
  return smtest::finish("test_device_engine");
}
