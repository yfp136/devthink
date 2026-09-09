// AI 全品牌灯光引擎单元测试（engine.light）
// 覆盖：灯位台账 / 分组区域查询 / DMX 512 缓冲按 universe 分配 /
//       手动效果写通道与分组隔离 / AI 自动编程（情绪配色+能量） /
//       节拍跟随闪断 / 停止后不再刷新 / 品牌预设查询 / status JSON
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engines/light/light_engine.h"
#include "test_common.h"

using sm::light::AiLightParams;
using sm::light::Brand;
using sm::light::FixtureType;
using sm::light::LightEngine;
using sm::light::LightPosition;

namespace {

LightPosition make_pos(const std::string& id, int universe, int addr,
                       int group = 0, const std::string& zone = "主舞台",
                       int x = 0, int y = 0) {
  LightPosition p;
  p.id = id;
  p.name = id;
  p.type = FixtureType::MovingHead_Spot;
  p.brand = Brand::Generic;
  p.x = x;
  p.y = y;
  p.z = 0;
  p.dmx_start_addr = addr;
  p.dmx_universe = universe;
  p.channel_count = 16;
  p.group_id = group;
  p.zone = zone;
  return p;
}

uint8_t dmx_at(const std::map<int, std::vector<uint8_t>>& bufs,
               int universe, int idx) {
  const auto it = bufs.find(universe);
  if (it == bufs.end() || idx < 0 || idx >= static_cast<int>(it->second.size()))
    return 0;
  return it->second[static_cast<size_t>(idx)];
}

void test_ledger_and_universe_bufs() {
  LightEngine eng;
  eng.init();
  eng.add_position(make_pos("L1", 1, 1, 0, "主舞台", 0, 0));
  eng.add_position(make_pos("L2", 1, 17, 1, "副舞台", 100, 50));
  eng.add_position(make_pos("L3", 2, 1, 1, "主舞台", 200, 0));
  eng.add_position(make_pos("L4", 2, 1, 1, "副舞台", 0, 200));

  SM_CHECK_EQ(eng.get_positions().size(), std::size_t(4));
  SM_CHECK_EQ(eng.get_by_group(1).size(), std::size_t(3));
  SM_CHECK_EQ(eng.get_by_group(9).size(), std::size_t(0));
  SM_CHECK_EQ(eng.get_by_zone("主舞台").size(), std::size_t(2));
  SM_CHECK_EQ(eng.get_by_zone("不存在").size(), std::size_t(0));

  // 每个出现过的 universe 都自动建立 512 字节缓冲
  const auto bufs = eng.get_dmx_buffers();
  SM_CHECK_EQ(bufs.size(), std::size_t(2));
  SM_CHECK(bufs.count(1) == 1);
  SM_CHECK(bufs.count(2) == 1);
  SM_CHECK_EQ(bufs.at(1).size(), std::size_t(512));
  SM_CHECK_EQ(bufs.at(2).size(), std::size_t(512));

  eng.remove_position("L2");
  SM_CHECK_EQ(eng.get_positions().size(), std::size_t(3));
  SM_CHECK_EQ(eng.get_by_group(1).size(), std::size_t(2));
}

void test_manual_dmx_writes_and_group_isolation() {
  LightEngine eng;
  eng.init();
  // M1/M2 组 7，O1 组 8（隔离对照）；M1 addr=1→idx0，M2 addr=33→idx32
  eng.add_position(make_pos("M1", 1, 1, 7));
  eng.add_position(make_pos("M2", 1, 33, 7));
  eng.add_position(make_pos("O1", 1, 1, 8));

  eng.set_color(7, 10, 20, 30);
  auto b1 = eng.get_dmx_buffers();
  SM_CHECK_EQ(dmx_at(b1, 1, 3), 10);   // M1 ch4=R
  SM_CHECK_EQ(dmx_at(b1, 1, 4), 20);   // M1 ch5=G
  SM_CHECK_EQ(dmx_at(b1, 1, 5), 30);   // M1 ch6=B
  SM_CHECK_EQ(dmx_at(b1, 1, 35), 10);  // M2 ch4=R（addr 33 → idx32+3）
  SM_CHECK_EQ(dmx_at(b1, 1, 3 + 0), 10);
  SM_CHECK_EQ(dmx_at(b1, 1, 3), 10);

  eng.set_intensity(7, 200);
  eng.set_position(7, 0x1FF, 400);  // pan=511, tilt=400
  eng.set_gobo(7, 12);
  eng.set_strobe(7, 25);  // 25Hz → 250
  const auto b2 = eng.get_dmx_buffers();
  SM_CHECK_EQ(dmx_at(b2, 1, 2), 200);  // M1 intensity
  SM_CHECK_EQ(dmx_at(b2, 1, 0), 0xFF); // M1 pan & 0xFF
  SM_CHECK_EQ(dmx_at(b2, 1, 1), 400 & 0xFF);
  SM_CHECK_EQ(dmx_at(b2, 1, 6), 12);   // gobo
  SM_CHECK_EQ(dmx_at(b2, 1, 7), 250);  // strobe
  SM_CHECK_EQ(dmx_at(b2, 1, 34), 200); // M2 intensity（idx32+2）
  SM_CHECK_EQ(dmx_at(b2, 1, 3), 10);   // 颜色通道不受位置写入影响

  // 组 8 隔离：只写 O1，不影响组 7
  eng.set_color(8, 100, 100, 100);
  eng.set_intensity(8, 99);
  const auto b3 = eng.get_dmx_buffers();
  SM_CHECK_EQ(dmx_at(b3, 1, 3), 100);  // O1 颜色被改
  SM_CHECK_EQ(dmx_at(b3, 1, 2), 99);   // O1 intensity
  SM_CHECK_EQ(dmx_at(b3, 1, 35), 10);  // M2 颜色保持组 7 写入值
}

void test_ai_generate_mood_and_energy() {
  LightEngine eng;
  eng.init();
  eng.add_position(make_pos("A1", 1, 1, 0, "主舞台", 0, 0));

  // calm / 能量5 → 蓝青配色、强度 125、gobo 100、strobe 0
  AiLightParams calm;
  calm.mood = "calm";
  calm.energy_level = 5;
  calm.bpm = 120.0;
  eng.set_ai_params(calm);
  eng.trigger_ai_generate();
  const auto b1 = eng.get_dmx_buffers();
  SM_CHECK_EQ(dmx_at(b1, 1, 0), 128);                 // pan = 128
  SM_CHECK_EQ(dmx_at(b1, 1, 1), 128 + 127);           // tilt = 128+cos(0)*127
  SM_CHECK_EQ(dmx_at(b1, 1, 2), 125);                 // intensity 5*25
  SM_CHECK_EQ(dmx_at(b1, 1, 3), 64);                  // R(calm)
  SM_CHECK_EQ(dmx_at(b1, 1, 4), 128);                 // G(calm)
  SM_CHECK_EQ(dmx_at(b1, 1, 5), 255);                 // B(calm)
  SM_CHECK_EQ(dmx_at(b1, 1, 6), 100);                 // gobo (energy<=5)
  SM_CHECK_EQ(dmx_at(b1, 1, 7), 0);                   // strobe (energy<=7)

  // intense / 能量 9 → 红粉配色、强度 225、gobo 200、strobe 250
  LightEngine eng2;
  eng2.init();
  eng2.add_position(make_pos("A2", 1, 1, 0, "主舞台", 100, 100));
  AiLightParams hot;
  hot.mood = "intense";
  hot.energy_level = 9;
  eng2.set_ai_params(hot);
  eng2.trigger_ai_generate();
  const auto b2 = eng2.get_dmx_buffers();
  SM_CHECK_EQ(dmx_at(b2, 1, 2), 225);
  SM_CHECK_EQ(dmx_at(b2, 1, 3), 255);
  SM_CHECK_EQ(dmx_at(b2, 1, 4), 0);
  SM_CHECK_EQ(dmx_at(b2, 1, 5), 64);
  SM_CHECK_EQ(dmx_at(b2, 1, 6), 200);
  SM_CHECK_EQ(dmx_at(b2, 1, 7), 250);
}

void test_beat_follow_flash() {
  LightEngine eng;
  eng.init();
  eng.add_position(make_pos("B1", 1, 1, 0, "主舞台", 0, 0));
  eng.start();

  AiLightParams p;
  p.bpm = 120.0;  // 500ms/拍
  p.energy_level = 6;
  p.mood = "energetic";
  eng.set_ai_params(p);
  eng.trigger_ai_generate();  // intensity = 6*25 = 150

  // t=0 尚未到拍
  eng.update(0, 120.0);
  SM_CHECK_EQ(dmx_at(eng.get_dmx_buffers(), 1, 2), 150);

  // t=499 仍未到拍（< 500）
  eng.update(499, 120.0);
  SM_CHECK_EQ(dmx_at(eng.get_dmx_buffers(), 1, 2), 150);

  // t=500 命中拍 → 闪断 +50（energy>5）
  eng.update(500, 120.0);
  SM_CHECK_EQ(dmx_at(eng.get_dmx_buffers(), 1, 2), 200);

  // t=600 未到下一拍
  eng.update(600, 120.0);
  SM_CHECK_EQ(dmx_at(eng.get_dmx_buffers(), 1, 2), 200);

  // t=1000 第二拍 → +50 → 250（min 255）
  eng.update(1000, 120.0);
  SM_CHECK_EQ(dmx_at(eng.get_dmx_buffers(), 1, 2), 250);
}

void test_stop_no_update_and_dmx_send() {
  LightEngine eng;
  eng.init();
  eng.add_position(make_pos("D1", 1, 1));
  eng.start();

  int sent = 0;
  int sent_len = 0;
  eng.set_dmx_send_fn([&](int, const uint8_t*, int len) {
    ++sent;
    sent_len = len;
    return true;
  });

  eng.update(0, 120.0);
  SM_CHECK_EQ(sent, 1);
  SM_CHECK_EQ(sent_len, 512);

  eng.stop();
  eng.update(100, 120.0);
  SM_CHECK_EQ(sent, 1);  // stop 后不再发送
}

void test_brand_presets() {
  LightEngine eng;
  const nlohmann::json ma = eng.get_brand_preset(Brand::MA_Lighting);
  SM_CHECK_EQ(ma["console"].get<std::string>(), std::string("grandMA3"));
  const nlohmann::json robe = eng.get_brand_preset(Brand::Robe);
  SM_CHECK(robe["fixtures"].is_array());
  SM_CHECK_EQ(robe["fixtures"].size(), std::size_t(4));
  const nlohmann::json generic = eng.get_brand_preset(Brand::Generic);
  SM_CHECK_EQ(generic["name"].get<std::string>(), std::string("Generic"));

  // load_brand_preset 冒烟（应不崩溃）
  eng.load_brand_preset(Brand::Martin);
}

void test_status_json() {
  LightEngine eng;
  eng.init();
  eng.add_position(make_pos("S1", 1, 1));
  eng.add_position(make_pos("S2", 2, 1));
  eng.start();
  AiLightParams p;
  p.bpm = 130.0;
  p.energy_level = 3;
  p.mood = "calm";
  eng.set_ai_params(p);

  const nlohmann::json j = eng.status();
  SM_CHECK_EQ(j["positions"].get<int>(), 2);
  SM_CHECK_EQ(j["universes"].get<int>(), 2);
  SM_CHECK_EQ(j["ai"]["bpm"].get<double>(), 130.0);
  SM_CHECK_EQ(j["ai"]["energy"].get<int>(), 3);
  SM_CHECK_EQ(j["ai"]["mood"].get<std::string>(), std::string("calm"));
  SM_CHECK_EQ(j["ai"]["auto"].get<bool>(), true);
  SM_CHECK_EQ(j["running"].get<bool>(), true);
}

}  // namespace

int main() {
  test_ledger_and_universe_bufs();
  test_manual_dmx_writes_and_group_isolation();
  test_ai_generate_mood_and_energy();
  test_beat_follow_flash();
  test_stop_no_update_and_dmx_send();
  test_brand_presets();
  test_status_json();
  return smtest::finish("test_light_engine");
}
