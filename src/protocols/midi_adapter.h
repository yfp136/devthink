// MIDI 协议适配器
// 需求：音频 BPM/MTC 同步 + MIDI 控制消息对接
// 功能：
//   1. 接收 MIDI Time Code (MTC) → 同步时间线
//   2. 接收 MIDI Control Change (CC) → 映射到 ShowMaster 指令
//   3. 接收 MIDI Note On/Off → 触发场景/GO
//   4. 发送 MIDI 消息到外部设备
//
// 平台：Windows 用 winmm (midiInOpen/midiOutOpen)
//       macOS 用 CoreMIDI (MIDIClientCreate)
//       无 MIDI 库时为 stub（记录到日志）
#pragma once

#include "protocols/protocol_adapter.h"

#include <atomic>
#include <thread>

namespace sm {
namespace proto {

// MIDI 消息类型
enum class MidiMsgType {
  NoteOff,       // 0x80
  NoteOn,        // 0x90
  ControlChange, // 0xB0
  ProgramChange, // 0xC0
  PitchBend,     // 0xE0
  MTC,           // 0xF1 (MIDI Time Code)
  SongPosition,  // 0xF2
  Clock,         // 0xF8 (MIDI Clock / 24ppq)
  Start,         // 0xFA
  Continue,      // 0xFB
  Stop           // 0xFC
};

// MIDI CC → ShowMaster 指令映射
struct MidiMapping {
  int channel;       // MIDI channel (0-15, -1 = all)
  int controller;    // CC number (0-127)
  std::string op;    // 映射到的 ShowMaster 操作
  int min_value = 0;
  int max_value = 127;
};

class MidiAdapter : public ProtocolAdapter {
public:
  MidiAdapter();
  ~MidiAdapter() override;

  // 配置
  void set_input_device(int device_id) { input_device_id_ = device_id; }
  void set_output_device(int device_id) { output_device_id_ = device_id; }

  // 添加 CC 映射
  void add_mapping(const MidiMapping& m) { mappings_.push_back(m); }

  // 启动 MIDI 输入监听
  bool start() override;
  void stop() override;
  bool is_running() const override { return running_; }

  // 发送 MIDI 消息
  bool send(const std::string& address,
            const std::string& payload) override;

  // 发送原始 MIDI 消息（3字节）
  bool send_raw(uint8_t status, uint8_t data1, uint8_t data2);

  // 发送 MTC（MIDI Time Code）
  bool send_mtc(int hours, int minutes, int seconds, int frames);

  // 发送 MIDI Clock（24 ppqn）
  bool send_clock();

  // MTC 同步状态
  int64_t mtc_position_ms() const { return mtc_position_ms_; }
  double bpm() const { return bpm_; }

private:
  void process_midi_msg(uint32_t msg);
  void mtc_decode(uint8_t data);

  int input_device_id_ = 0;
  int output_device_id_ = 0;
  std::atomic<bool> running_{false};

  std::vector<MidiMapping> mappings_;

  // MTC 解码状态
  uint8_t mtc_values_[8] = {};
  int64_t mtc_position_ms_ = 0;
  double bpm_ = 120.0;

  // MIDI Clock 计数（用于 BPM 检测）
  int clock_count_ = 0;
  int64_t clock_last_time_ = 0;
};

} // namespace proto
} // namespace sm
