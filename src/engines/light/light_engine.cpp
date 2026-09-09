// AI 全品牌灯光引擎实现
#include "engines/light/light_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sm {
namespace light {

LightEngine::LightEngine() = default;
LightEngine::~LightEngine() { stop(); }

void LightEngine::init() {
  dmx_buffers_.clear();
}

void LightEngine::start() { running_ = true; }
void LightEngine::stop() { running_ = false; }

void LightEngine::add_position(const LightPosition& pos) {
  positions_.push_back(pos);
  // 初始化 DMX 缓冲
  if (dmx_buffers_.find(pos.dmx_universe) == dmx_buffers_.end())
    dmx_buffers_[pos.dmx_universe] = std::vector<uint8_t>(512, 0);
}

void LightEngine::remove_position(const std::string& id) {
  positions_.erase(
      std::remove_if(positions_.begin(), positions_.end(),
                      [&](const LightPosition& p) { return p.id == id; }),
      positions_.end());
}

std::vector<LightPosition> LightEngine::get_positions() const {
  return positions_;
}

std::vector<LightPosition> LightEngine::get_by_group(int group_id) const {
  std::vector<LightPosition> result;
  for (const auto& p : positions_)
    if (p.group_id == group_id) result.push_back(p);
  return result;
}

std::vector<LightPosition> LightEngine::get_by_zone(const std::string& zone) const {
  std::vector<LightPosition> result;
  for (const auto& p : positions_)
    if (p.zone == zone) result.push_back(p);
  return result;
}

void LightEngine::load_brand_preset(Brand brand) {
  // 品牌预设加载（通道映射模板）
  // 不同品牌的灯具通道顺序不同
  std::printf("[light] 加载品牌预设: %d\n", static_cast<int>(brand));
}

nlohmann::json LightEngine::get_brand_preset(Brand brand) const {
  nlohmann::json j;
  switch (brand) {
    case Brand::MA_Lighting:
      j["name"] = "MA Lighting";
      j["console"] = "grandMA3";
      j["protocol"] = "MA-Net";
      break;
    case Brand::Chamsys:
      j["name"] = "Chamsys";
      j["console"] = "MagicQ";
      j["protocol"] = "Art-Net";
      break;
    case Brand::Avolites:
      j["name"] = "Avolites";
      j["console"] = "Titan";
      j["protocol"] = "Art-Net / sACN";
      break;
    case Brand::Robe:
      j["name"] = "Robe";
      j["fixtures"] = {"Spiider", "Pointe", "Esprite", "Tetra"};
      break;
    case Brand::Martin:
      j["name"] = "Martin";
      j["fixtures"] = {"MAC Aura", "MAC Viper", "MAC Quantum"};
      break;
    default:
      j["name"] = "Generic";
      j["protocol"] = "Art-Net";
      break;
  }
  return j;
}

void LightEngine::set_ai_params(const AiLightParams& params) {
  ai_params_ = params;
}

AiLightParams LightEngine::get_ai_params() const {
  return ai_params_;
}

void LightEngine::trigger_ai_generate() {
  // AI 自动编程：基于 BPM、能量等级、情绪自动生成灯光效果
  // （节拍相位跟踪在 update() 中完成，这里只做静态编排）
  int energy = ai_params_.energy_level;

  // 根据情绪选择配色
  uint8_t r = 255, g = 128, b = 0;
  if (ai_params_.mood == "calm") {
    r = 64; g = 128; b = 255;
  } else if (ai_params_.mood == "energetic") {
    r = 255; g = 64; b = 0;
  } else if (ai_params_.mood == "intense") {
    r = 255; g = 0; b = 64;
  }

  // 遍历所有灯具，设置基础参数
  for (const auto& pos : positions_) {
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];

    // 简化：假设标准 16 通道移动头
    // ch1=pan, ch2=tilt, ch3=intensity, ch4-6=color, ch7=gobo, ch8=strobe
    if (addr + 7 < 512) {
      // 位置（随能量等级变化）
      int pan = 128 + static_cast<int>(sin(pos.x * 0.01) * 127);
      int tilt = 128 + static_cast<int>(cos(pos.y * 0.01) * 127);
      buf[addr] = pan & 0xFF;
      buf[addr + 1] = tilt & 0xFF;
      buf[addr + 2] = (energy * 25) & 0xFF;  // intensity
      buf[addr + 3] = r;
      buf[addr + 4] = g;
      buf[addr + 5] = b;
      buf[addr + 6] = (energy > 5) ? 200 : 100;  // gobo
      buf[addr + 7] = (energy > 7) ? 250 : 0;   // strobe
    }
  }

  std::printf("[light] AI 生成: BPM=%.1f, 能量=%d, 情绪=%s\n",
              ai_params_.bpm, energy, ai_params_.mood.c_str());
}

void LightEngine::set_color(int group_id, uint8_t r, uint8_t g, uint8_t b) {
  for (const auto& pos : positions_) {
    if (pos.group_id != group_id) continue;
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];
    if (addr + 5 < 512) {
      buf[addr + 3] = r;
      buf[addr + 4] = g;
      buf[addr + 5] = b;
    }
  }
}

void LightEngine::set_intensity(int group_id, uint8_t intensity) {
  for (const auto& pos : positions_) {
    if (pos.group_id != group_id) continue;
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];
    if (addr + 2 < 512)
      buf[addr + 2] = intensity;
  }
}

void LightEngine::set_position(int group_id, int pan, int tilt) {
  for (const auto& pos : positions_) {
    if (pos.group_id != group_id) continue;
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];
    if (addr + 1 < 512) {
      buf[addr] = pan & 0xFF;
      buf[addr + 1] = tilt & 0xFF;
    }
  }
}

void LightEngine::set_gobo(int group_id, int gobo_index) {
  for (const auto& pos : positions_) {
    if (pos.group_id != group_id) continue;
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];
    if (addr + 6 < 512)
      buf[addr + 6] = gobo_index & 0xFF;
  }
}

void LightEngine::set_strobe(int group_id, int rate_hz) {
  for (const auto& pos : positions_) {
    if (pos.group_id != group_id) continue;
    int addr = pos.dmx_start_addr - 1;
    auto& buf = dmx_buffers_[pos.dmx_universe];
    if (addr + 7 < 512) {
      // 映射 Hz 到 DMX 值（0-255）
      buf[addr + 7] = std::min(255, rate_hz * 10) & 0xFF;
    }
  }
}

void LightEngine::set_dmx_send_fn(DmxSendFn fn) {
  dmx_send_fn_ = std::move(fn);
}

void LightEngine::update(int64_t elapsed_ms, double current_bpm) {
  if (!running_) return;

  if (ai_params_.auto_generate) {
    // 检测节拍
    double beat_interval = 60000.0 / current_bpm;
    if (elapsed_ms - last_beat_ms_ >= static_cast<int64_t>(beat_interval)) {
      last_beat_ms_ = elapsed_ms;
      // 节拍触发：轻微变化效果
      // 根据能量等级决定动作幅度
      for (const auto& pos : positions_) {
        int addr = pos.dmx_start_addr - 1;
        auto& buf = dmx_buffers_[pos.dmx_universe];
        if (addr + 7 < 512) {
          // 节拍闪光
          int flash = (ai_params_.energy_level > 5) ? 50 : 20;
          buf[addr + 2] = std::min(255, buf[addr + 2] + flash);
        }
      }
    }
  }

  update_dmx();
}

void LightEngine::update_dmx() {
  if (!dmx_send_fn_) return;
  for (const auto& [universe, buf] : dmx_buffers_)
    dmx_send_fn_(universe, buf.data(), 512);
}

std::map<int, std::vector<uint8_t>> LightEngine::get_dmx_buffers() const {
  return dmx_buffers_;
}

nlohmann::json LightEngine::status() const {
  nlohmann::json j;
  j["running"] = running_;
  j["positions"] = positions_.size();
  j["universes"] = dmx_buffers_.size();
  j["ai"]["bpm"] = ai_params_.bpm;
  j["ai"]["energy"] = ai_params_.energy_level;
  j["ai"]["mood"] = ai_params_.mood;
  j["ai"]["auto"] = ai_params_.auto_generate;
  return j;
}

Brand LightEngine::detect_brand(const std::string& model_name) {
  if (model_name.find("MAC") != std::string::npos) return Brand::Martin;
  if (model_name.find("Spiider") != std::string::npos ||
      model_name.find("Pointe") != std::string::npos) return Brand::Robe;
  if (model_name.find("MAC Aura") != std::string::npos) return Brand::Martin;
  return Brand::Generic;
}

} // namespace light
} // namespace sm
