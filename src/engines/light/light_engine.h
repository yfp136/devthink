// AI 全品牌灯光引擎
// 需求：AI 全品牌灯光（自动灯光编程、效果生成、品牌适配）
// 功能：
//   1. 灯光品牌适配（MA Lighting / Chamsys / Avolites / Robe / Martin 等）
//   2. 灯位布局管理（灯位坐标、组、区域）
//   3. AI 自动编程（跟随音乐节拍自动生成灯光效果）
//   4. 效果预设库（图案旋转、棱镜、频闪、光束定位）
//   5. DMX 输出（Art-Net / sACN）
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace light {

// 灯光品牌
enum class Brand {
  MA_Lighting,
  Chamsys,
  Avolites,
  Robe,
  Martin,
  Clay_Paky,
  Elation,
  Generic
};

// 灯具类型
enum class FixtureType {
  MovingHead_Spot,   // 图案灯
  MovingHead_Wash,   // 染色灯
  PAR,               // PAR 灯
  Strip,             // 灯条
  Laser,             // 激光
  Strobe,            // 频闪
  Blinder,           // 回转灯
  Effect             // 特效灯
};

// 灯位
struct LightPosition {
  std::string id;
  std::string name;
  FixtureType type;
  Brand brand;
  int x, y, z;               // 3D 坐标（mm）
  int dmx_start_addr;        // DMX 起始地址
  int dmx_universe;          // DMX universe
  int channel_count;        // 通道数
  int group_id;              // 分组 ID
  std::string zone;          // 区域名称
};

// 灯光效果预设
struct LightPreset {
  std::string name;
  std::string description;
  std::vector<int> fixture_ids;  // 应用的灯具
  nlohmann::json dmx_values;       // DMX 通道值
};

// AI 灯光参数
struct AiLightParams {
  double bpm = 120.0;          // 节拍
  int energy_level = 5;        // 能量等级 1-10
  std::string mood = "neutral";  // 情绪: calm / neutral / energetic / intense
  std::string color_scheme = "warm";  // 配色方案
  bool auto_generate = true;   // AI 自动生成
};

class LightEngine {
public:
  LightEngine();
  ~LightEngine();

  // 初始化
  void init();
  void start();
  void stop();

  // 灯位管理
  void add_position(const LightPosition& pos);
  void remove_position(const std::string& id);
  std::vector<LightPosition> get_positions() const;
  std::vector<LightPosition> get_by_group(int group_id) const;
  std::vector<LightPosition> get_by_zone(const std::string& zone) const;

  // 品牌预设
  void load_brand_preset(Brand brand);
  nlohmann::json get_brand_preset(Brand brand) const;

  // AI 自动编程
  void set_ai_params(const AiLightParams& params);
  AiLightParams get_ai_params() const;

  // 触发 AI 生成（基于 BPM 和情绪参数）
  void trigger_ai_generate();

  // 手动效果
  void set_color(int group_id, uint8_t r, uint8_t g, uint8_t b);
  void set_intensity(int group_id, uint8_t intensity);
  void set_position(int group_id, int pan, int tilt);
  void set_gobo(int group_id, int gobo_index);
  void set_strobe(int group_id, int rate_hz);

  // DMX 输出
  using DmxSendFn = std::function<bool(int universe,
                                        const uint8_t* data, int len)>;
  void set_dmx_send_fn(DmxSendFn fn);

  // 更新（按帧调用）
  void update(int64_t elapsed_ms, double current_bpm);

  // 获取 DMX 缓冲
  std::map<int, std::vector<uint8_t>> get_dmx_buffers() const;

  // 获取状态
  nlohmann::json status() const;

private:
  std::vector<LightPosition> positions_;
  AiLightParams ai_params_;
  DmxSendFn dmx_send_fn_;
  std::map<int, std::vector<uint8_t>> dmx_buffers_;  // universe → 512 bytes
  bool running_ = false;
  int64_t last_beat_ms_ = 0;

  void update_dmx();
  Brand detect_brand(const std::string& model_name);
};

} // namespace light
} // namespace sm
