// MIDI 协议适配器实现
// Windows: winmm (midiInOpen / midiOutOpen / midiInStart)
// macOS: CoreMIDI (MIDIClientCreate / MIDIInputPortCreate)
// 无库: stub（打印日志，不崩溃）
#include "protocols/midi_adapter.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <midiio.h>
#pragma comment(lib, "winmm.lib")

static sm::proto::MidiAdapter* g_midi_instance = nullptr;

static void CALLBACK midi_in_callback(HMIDIIN hMidiIn, UINT wMsg,
                                       DWORD_PTR dwInstance,
                                       DWORD_PTR dwParam1,
                                       DWORD_PTR dwParam2) {
  if (wMsg == MIM_DATA && g_midi_instance) {
    // dwParam1 = MIDI 消息 (status | data1 | data2)
    uint32_t msg = static_cast<uint32_t>(dwParam1);
    // 通过静态函数调用实例方法（简化处理）
    // 实际应用中需要线程安全机制
  }
}

#elif defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

static sm::proto::MidiAdapter* g_midi_instance = nullptr;

static void midi_read_proc(const MIDIPacketList* pktlist,
                           void* refCon, void* connRefCon) {
  (void)refCon; (void)connRefCon;
  if (!g_midi_instance) return;
  const MIDIPacket* pkt = pktlist->packet;
  for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
    // 每个 packet 包含 1-3 字节 MIDI 消息
    // 这里简化处理，实际需要解析 running status
    if (pkt->length >= 3) {
      uint32_t msg = (pkt->data[0] << 16) | (pkt->data[1] << 8) | pkt->data[2];
      (void)msg;  // TODO: thread-safe dispatch to process_midi_msg
    }
    pkt = MIDIPacketNext(pkt);
  }
}

#endif

namespace sm {
namespace proto {

MidiAdapter::MidiAdapter() = default;
MidiAdapter::~MidiAdapter() { stop(); }

bool MidiAdapter::start() {
#if defined(_WIN32)
  MIDIINCAPSA caps;
  int num = midiInGetNumDevs();
  if (num == 0) {
    std::printf("[midi] 无可用 MIDI 输入设备\n");
    running_ = true;  // stub 模式
    return true;
  }

  if (input_device_id_ >= num) input_device_id_ = 0;

  midiInGetDevCapsA(input_device_id_, &caps, sizeof(caps));
  std::printf("[midi] 输入设备: %s\n", caps.szPname);

  HMIDIIN handle;
  MMRESULT rv = midiInOpen(&handle, input_device_id_,
                            reinterpret_cast<DWORD_PTR>(midi_in_callback),
                            0, CALLBACK_FUNCTION);
  if (rv != MMSYSERR_NOERROR) {
    std::fprintf(stderr, "[midi] midiInOpen 失败: %d\n", rv);
    running_ = true;
    return true;  // stub 模式
  }
  g_midi_instance = this;
  midiInStart(handle);
  running_ = true;
  std::printf("[midi] 输入监听已启动（设备 %d）\n", input_device_id_);
  return true;

#elif defined(__APPLE__)
  MIDIClientRef client;
  OSStatus rc = MIDIClientCreate(CFSTR("ShowMaster"), nullptr, nullptr, &client);
  if (rc != noErr) {
    std::fprintf(stderr, "[midi] MIDIClientCreate 失败: %d\n", rc);
    running_ = true;
    return true;
  }

  MIDIPortRef inPort;
  rc = MIDIInputPortCreate(client, CFSTR("Input"), midi_read_proc,
                           nullptr, &inPort);
  if (rc != noErr) {
    std::fprintf(stderr, "[midi] MIDIInputPortCreate 失败: %d\n", rc);
    running_ = true;
    return true;
  }

  int numSources = MIDIGetNumberOfSources();
  if (numSources > 0) {
    MIDIEndpointRef src = MIDIGetSource(0);
    rc = MIDIPortConnectSource(inPort, src, nullptr);
    std::printf("[midi] 连接到第 1 个源设备 (共 %d 个)\n", numSources);
  } else {
    std::printf("[midi] 无可用 MIDI 源设备\n");
  }

  g_midi_instance = this;
  running_ = true;
  std::printf("[midi] 输入监听已启动\n");
  return true;

#else
  // stub
  running_ = true;
  std::printf("[midi] stub 模式（无 MIDI 库）\n");
  return true;
#endif
}

void MidiAdapter::stop() {
  running_ = false;
  g_midi_instance = nullptr;
}

void MidiAdapter::process_midi_msg(uint32_t msg) {
  uint8_t status = msg & 0xFF;
  uint8_t data1 = (msg >> 8) & 0xFF;
  uint8_t data2 = (msg >> 16) & 0xFF;

  uint8_t msg_type = status & 0xF0;
  uint8_t channel = status & 0x0F;

  ProtocolMessage pm;
  pm.type = ProtocolType::MIDI;
  pm.source = "midi:" + std::to_string(channel);
  pm.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

  switch (msg_type) {
    case 0x80:  // Note Off
      pm.address = "note_off";
      pm.payload = std::to_string(data1);  // note number
      break;
    case 0x90:  // Note On
      pm.address = "note_on";
      pm.payload = std::to_string(data1) + "," + std::to_string(data2);
      break;
    case 0xB0:  // Control Change
      pm.address = "cc";
      pm.payload = std::to_string(data1) + "," + std::to_string(data2);
      // 检查映射
      for (const auto& m : mappings_) {
        if ((m.channel < 0 || m.channel == channel) &&
            m.controller == data1) {
          pm.address = m.op;
          int range = m.max_value - m.min_value;
          if (range > 0)
            pm.payload = std::to_string(
                static_cast<double>(data2 - m.min_value) / range);
        }
      }
      break;
    case 0xC0:  // Program Change
      pm.address = "program_change";
      pm.payload = std::to_string(data1);
      break;
    case 0xE0:  // Pitch Bend
      pm.address = "pitch_bend";
      pm.payload = std::to_string((data2 << 7) | data1);
      break;
    case 0xF1:  // MTC
      mtc_decode(data1);
      return;  // MTC 不触发回调
    case 0xF8:  // MIDI Clock
      clock_count_++;
      {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (clock_last_time_ > 0 && clock_count_ >= 24) {
          double elapsed_s = (now - clock_last_time_) / 1000.0;
          if (elapsed_s > 0) {
            bpm_ = 60.0 / elapsed_s;
            clock_count_ = 0;
            clock_last_time_ = now;
          }
        } else if (clock_last_time_ == 0) {
          clock_last_time_ = now;
        }
      }
      return;
    case 0xFA:  // Start
      pm.address = "midi_start";
      pm.payload = "";
      break;
    case 0xFB:  // Continue
      pm.address = "midi_continue";
      pm.payload = "";
      break;
    case 0xFC:  // Stop
      pm.address = "midi_stop";
      pm.payload = "";
      break;
    default:
      return;
  }

  if (callback_)
    callback_(pm);
}

void MidiAdapter::mtc_decode(uint8_t data) {
  // MTC 以 8 个片段传送一帧时间码
  // data 的低 4 位是值，高 4 位标识片段类型 (0-7)
  uint8_t piece = (data >> 4) & 0x07;
  uint8_t value = data & 0x0F;

  mtc_values_[piece] = value;

  // 当收到 piece 7（帧个位）时，表示完整一帧
  if (piece == 7) {
    int frames = mtc_values_[7] | ((mtc_values_[6] & 0x01) << 4);
    int seconds = mtc_values_[5] | ((mtc_values_[4] & 0x03) << 4);
    int minutes = mtc_values_[3] | ((mtc_values_[2] & 0x03) << 4);
    int hours = mtc_values_[1] | ((mtc_values_[0] & 0x01) << 4);

    // 转换为毫秒（假设 30 fps）
    mtc_position_ms_ = (hours * 3600 + minutes * 60 + seconds) * 1000
                      + (frames * 1000 / 30);
  }
}

bool MidiAdapter::send(const std::string& address,
                       const std::string& payload) {
  // address 格式: "cc" / "note_on" / "note_off" / "program_change"
  // payload: "channel,controller,value" 或 "channel,note,velocity"
  // 简化：直接发送原始 MIDI
  if (address == "raw") {
    // payload = "status,data1,data2"
    int s, d1, d2;
    if (std::sscanf(payload.c_str(), "%d,%d,%d", &s, &d1, &d2) == 3)
      return send_raw(static_cast<uint8_t>(s),
                      static_cast<uint8_t>(d1),
                      static_cast<uint8_t>(d2));
  }
  return false;
}

bool MidiAdapter::send_raw(uint8_t status, uint8_t data1, uint8_t data2) {
#if defined(_WIN32)
  HMIDIOUT handle;
  if (midiOutOpen(&handle, output_device_id_, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    return false;
  DWORD msg = (status) | (data1 << 8) | (data2 << 16);
  MMRESULT rv = midiOutShortMsg(handle, msg);
  midiOutClose(handle);
  return rv == MMSYSERR_NOERROR;
#elif defined(__APPLE__)
  // CoreMIDI 发送需要目标 endpoint，简化处理
  std::printf("[midi] send_raw(0x%02X, 0x%02X, 0x%02X) [stub]\n",
              status, data1, data2);
  return true;
#else
  std::printf("[midi] send_raw(0x%02X, 0x%02X, 0x%02X) [stub]\n",
              status, data1, data2);
  return true;
#endif
}

bool MidiAdapter::send_mtc(int hours, int minutes, int seconds, int frames) {
  // 发送 8 个 MTC 片段
  // 帧: frames & 0x0F | (0 << 4)
  // 秒: (seconds & 0x0F) | (0 << 4)
  // 秒十位: (seconds / 10) | (1 << 4)
  // 分: (minutes & 0x0F) | (2 << 4)
  // 分十位: (minutes / 10) | (3 << 4)
  // 时: (hours & 0x0F) | (4 << 4)
  // 时十位: (hours / 10) | (5 << 4)
  // 帧类型: (0) | (6 << 4)  // 0=24fps, 1=25fps, 2=30fps drop, 3=30fps
  // 帧: ((frames >> 4) & 0x01) | (7 << 4)

  uint8_t pieces[8] = {
    uint8_t((frames & 0x0F) | (0 << 4)),
    uint8_t((seconds & 0x0F) | (1 << 4)),
    uint8_t((seconds / 10 & 0x03) | (2 << 4)),
    uint8_t((minutes & 0x0F) | (3 << 4)),
    uint8_t((minutes / 10 & 0x03) | (4 << 4)),
    uint8_t((hours & 0x0F) | (5 << 4)),
    uint8_t((hours / 10 & 0x01) | (6 << 4)),
    uint8_t(((frames >> 4) & 0x01) | (7 << 4))
  };

  for (int i = 0; i < 8; ++i)
    send_raw(0xF1, pieces[i], 0);
  return true;
}

bool MidiAdapter::send_clock() {
  return send_raw(0xF8, 0, 0);
}

} // namespace proto
} // namespace sm
