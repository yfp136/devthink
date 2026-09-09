// Phase 2 平台适配层：FFmpeg 解码管线 + WASAPI 音频 + D3D11 PGM 捕获
// Windows：真实实现（SM_HAS_FFMPEG / SM_HAS_WASAPI / SM_HAS_D3D11）
// macOS/Linux：stub 编译（不链接外部库）
#include "platform/media_backend.h"
#include "platform/jpeg_codec.h"  // 自包含 baseline JPEG 编码器

// P1-2（规格书 2.3 RHI）：Windows 真实媒体后端下引入渲染上下文，
// capture_pgm_frame_jpeg 改从 D3D11 离屏渲染帧读取（上传 → 合成 → 回读）。
#if defined(_WIN32) && defined(SM_HAS_D3D11)
#include "platform/rhi/rhi.h"
#endif

// Windows WASAPI 真实渲染（仅 SM_HAS_FFMPEG 分支使用；macOS/Linux stub 不编译）
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <objbase.h>  // CoInitializeEx / CoTaskMemFree（WASAPI COM 会话）
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace sm {
namespace platform {

// ============================================================================

#if !defined(SM_HAS_FFMPEG)
// ======================= macOS/Linux stub =======================

bool has_media_backend() { return false; }

nlohmann::json probe_media(const std::string& fp) {
  (void)fp;
  return nlohmann::json::object();
}
bool open_media_file(const std::string& fp, int64_t t) {
  (void)fp; (void)t;
  return true;
}
void start_media_playback(double g, int64_t f) { (void)g; (void)f; }
void stop_media_playback(int64_t f) { (void)f; }
int64_t get_media_pos_ms() { return 0; }
int64_t get_media_duration_ms(const std::string& fp) { (void)fp; return 0; }
void close_media_file() {}
std::string generate_thumbnail(const std::string& fp, int64_t d) {
  (void)fp; (void)d;
  return "";
}
std::string capture_pgm_frame_jpeg() { return ""; }
bool init_audio_output() { return false; }  // stub 无真实音频输出
void shutdown_audio_output() {}

#else
// ======================= Windows FFmpeg 真实实现 =======================

// ---- Base64 编码（用于 JPEG → base64）----
static std::string base64_encode(const uint8_t* data, size_t len) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? tbl[n & 0x3F] : '=';
  }
  return out;
}

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// ---- 内部状态 ----
struct MediaState {
  // FFmpeg 上下文
  AVFormatContext* fmt_ctx = nullptr;
  AVCodecContext* vcodec = nullptr;
  AVCodecContext* acodec = nullptr;
  SwsContext* sws = nullptr;
  SwrContext* swr = nullptr;

  // 流索引
  int video_stream = -1;
  int audio_stream = -1;

  // 视频帧转换目标
  AVFrame* rgb_frame = nullptr;
  uint8_t* rgb_buf = nullptr;
  int rgb_w = 0;
  int rgb_h = 0;

  // 音频重采样目标
  AVFrame* audio_pcm = nullptr;

  // 解码线程
  std::thread decode_thread;
  std::atomic<bool> decoding{false};
  std::atomic<bool> seek_stop{false};
  std::atomic<bool> decode_done{false};  // 解码线程已退出（EOF / 停止）

  // 当前文件是否含可播放音频（open 时确定）
  bool has_audio = false;

  // WASAPI 渲染线程（仅 Windows；有音频流时启动）
  std::thread render_thread;
  std::atomic<bool> rendering{false};      // 渲染线程运行中
  std::atomic<bool> consume_audio{false};  // 音频有消费者 → 解码线程投递队列
  std::atomic<bool> audio_drop{false};     // 渲染端不可用 → 解码丢弃音频（防阻塞）
  bool wasapi_available = false;           // init_audio_output() 探测结果
#if defined(_WIN32)
  HANDLE wake_evt = nullptr;               // 唤醒渲染线程事件等待（stop/close 时 Set）
#endif

  // 视频帧队列（PGM 捕获用）
  struct VideoFrame {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
    int64_t pts_ms = 0;
  };
  std::deque<VideoFrame> vqueue;
  std::mutex vq_mutex;
  std::condition_variable vq_cv;
  static constexpr int VQ_MAX = 5;

#if defined(_WIN32) && defined(SM_HAS_D3D11)
  // PGM 离屏渲染上下文（规格 2.3 / P1-2）：视频帧上传为纹理后经 4 层
  // 混合合成到离屏渲染目标，capture 回读该渲染帧（不再解码帧直出）。
  // 线程模型：RHI 对象仅由 capture_pgm_frame_jpeg 所在线程访问（rhi.h
  // 注释：单所有者），rhi_mutex 兜底并发；合成器不可 resize，尺寸跟随
  // 视频帧，画面尺寸变化时整体重建。
  std::shared_ptr<sm::rhi::IDevice> rhi_dev;  // 后端设备（延迟创建）
  std::shared_ptr<sm::rhi::IPgmMixer> mixer;  // 离屏合成器
  std::shared_ptr<sm::rhi::ITexture> layer_tex[sm::rhi::IPgmMixer::kMaxLayers]{};  // 上传纹理（0 = 视频帧）
  std::mutex rhi_mutex;
#endif

  // 音频缓冲队列
  struct AudioChunk {
    std::vector<uint8_t> pcm;
    int64_t pts_ms = 0;
  };
  std::deque<AudioChunk> aqueue;
  std::mutex aq_mutex;
  std::condition_variable aq_cv;
  static constexpr int AQ_MAX = 10;

  // 播放控制
  std::atomic<double> gain_db{0.0};
  std::atomic<int64_t> fade_in_ms{0};
  std::atomic<int64_t> fade_out_ms{0};
  std::atomic<bool> fading_out{false};
  std::atomic<int64_t> start_clock_ms{0};  // 播放开始时的 monotonic 时钟
  std::atomic<int64_t> trim_in_ms{0};

  // 当前播放位置
  std::atomic<int64_t> current_pos_ms{0};

  // 文件时长
  int64_t duration_ms = 0;
};

static MediaState g_state;

// ---- 解码线程主循环 ----
static void decode_loop() {
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();

  auto clock_ms = []() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  };

  // 视频帧 → RGB24 → 最新帧队列（PGM 捕获只保留最新帧）
  auto push_video = [&](AVFrame* f) -> void {
    if (!g_state.sws || !g_state.rgb_frame) return;
    sws_scale(g_state.sws, f->data, f->linesize, 0, g_state.rgb_h,
              g_state.rgb_frame->data, g_state.rgb_frame->linesize);

    MediaState::VideoFrame vf;
    int size = g_state.rgb_w * g_state.rgb_h * 3;
    vf.rgb.assign(g_state.rgb_frame->data[0],
                  g_state.rgb_frame->data[0] + size);
    vf.width = g_state.rgb_w;
    vf.height = g_state.rgb_h;
    if (f->pts != AV_NOPTS_VALUE) {
      AVRational tb =
          g_state.fmt_ctx->streams[g_state.video_stream]->time_base;
      vf.pts_ms =
          av_rescale_q(f->pts, tb, {1, 1000}) - g_state.trim_in_ms.load();
    }

    std::unique_lock<std::mutex> lk(g_state.vq_mutex);
    g_state.vq_cv.wait(lk, [] {
      return g_state.vqueue.size() < MediaState::VQ_MAX ||
             !g_state.decoding;
    });
    if (!g_state.decoding) return;
    g_state.vqueue.push_back(std::move(vf));
    if (g_state.vqueue.size() > 1) g_state.vqueue.pop_front();
  };

  // 解码音频帧 → S16/48k/stereo → 音频块队列（WASAPI 渲染线程消费）
  auto push_audio = [&](AVFrame* f) -> void {
    if (!g_state.swr || !g_state.audio_pcm) return;

    int out_samples = swr_convert(
        g_state.swr, g_state.audio_pcm->data, g_state.audio_pcm->nb_samples,
        (const uint8_t**)f->data, f->nb_samples);
    if (out_samples <= 0) return;

    int data_size =
        out_samples * g_state.audio_pcm->ch_layout.nb_channels *
        av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    if (data_size <= 0) return;

    MediaState::AudioChunk chunk;
    chunk.pcm.assign(g_state.audio_pcm->data[0],
                     g_state.audio_pcm->data[0] + data_size);
    AVRational tb =
        g_state.fmt_ctx->streams[g_state.audio_stream]->time_base;
    if (f->pts != AV_NOPTS_VALUE)
      chunk.pts_ms =
          av_rescale_q(f->pts, tb, {1, 1000}) - g_state.trim_in_ms.load();

    // 等待：渲染端已就绪（consume_audio）且队列有空位；
    // stop/close 通过置 decoding=false + notify 唤醒本等待；
    // audio_drop（渲染端不可用）→ 直接丢弃，避免解码被永久卡死
    std::unique_lock<std::mutex> lk(g_state.aq_mutex);
    g_state.aq_cv.wait(lk, [] {
      return !g_state.decoding || g_state.audio_drop ||
             (g_state.consume_audio &&
              g_state.aqueue.size() < MediaState::AQ_MAX);
    });
    if (!g_state.decoding || g_state.audio_drop) return;
    g_state.aqueue.push_back(std::move(chunk));
  };

  // 解码器排空：自然 EOF 时取尽缓存帧（stop/close 置 seek_stop 则跳过）
  auto drain_decoders = [&]() {
    if (g_state.acodec && g_state.decoding && !g_state.seek_stop) {
      avcodec_send_packet(g_state.acodec, nullptr);
      while (avcodec_receive_frame(g_state.acodec, frame) >= 0) {
        if (!g_state.decoding || g_state.seek_stop) break;
        push_audio(frame);
      }
    }
    if (g_state.vcodec && g_state.decoding && !g_state.seek_stop) {
      avcodec_send_packet(g_state.vcodec, nullptr);
      while (avcodec_receive_frame(g_state.vcodec, frame) >= 0) {
        if (!g_state.decoding || g_state.seek_stop) break;
        push_video(frame);
      }
    }
  };

  g_state.start_clock_ms = clock_ms();

  while (g_state.decoding && !g_state.seek_stop) {
    int ret = av_read_frame(g_state.fmt_ctx, pkt);
    if (ret < 0) {
      // EOF 或读错误 → 排空解码器缓存帧后退出
      drain_decoders();
      break;
    }

    if (pkt->stream_index == g_state.video_stream && g_state.vcodec) {
      ret = avcodec_send_packet(g_state.vcodec, pkt);
      if (ret >= 0) {
        while (avcodec_receive_frame(g_state.vcodec, frame) >= 0) {
          push_video(frame);
          if (!g_state.decoding || g_state.seek_stop) break;
        }
      }
    } else if (pkt->stream_index == g_state.audio_stream && g_state.acodec) {
      ret = avcodec_send_packet(g_state.acodec, pkt);
      if (ret >= 0) {
        while (avcodec_receive_frame(g_state.acodec, frame) >= 0) {
          push_audio(frame);
          if (!g_state.decoding || g_state.seek_stop) break;
        }
      }
    }

    av_packet_unref(pkt);
  }

  // 线程退出契约：置位 decode_done 并唤醒所有可能阻塞的等待方
  g_state.decode_done = true;
  g_state.vq_cv.notify_all();
  g_state.aq_cv.notify_all();

  av_frame_free(&frame);
  av_packet_free(&pkt);
}

// ---- WASAPI 渲染线程主循环（事件驱动共享模式）----
// 消费 g_state.aqueue 中的 S16/48k/stereo 音频块。wake_evt 由
// start_media_playback 创建、由 stop/close 触发唤醒；本线程只使用不关闭。
// 退出方式：① 初始化失败 → audio_drop=true（解码丢弃音频，纯视频照播）；
// ② 自然播完（decode_done 且队列空，连续两个静音周期）→ 自行收尾；
// ③ stop/close 置 rendering=false 并经 wake_evt 唤醒 → 收尾退出。
static void render_loop() {
  constexpr DWORD kRate = 48000;  // 采样率（与解码端 swr 输出一致）
  constexpr WORD kCh = 2;         // 声道数（stereo）
  constexpr WORD kBits = 16;      // 位深（S16）
  const UINT32 kFrameBytes = kCh * (kBits / 8);  // 4 字节/帧

  auto clock_ms = []() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  };

  // ---- 线程内 COM 初始化 ----
  bool co_uninit = false;
  {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE：线程已以其它模式初始化 → 沿用现有 COM，无需/不可
    // 配对 CoUninitialize；其余失败则渲染端不可用，直接进入丢弃兜底。
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      g_state.audio_drop = true;
      g_state.consume_audio = false;
      g_state.rendering = false;
      return;
    }
    co_uninit = SUCCEEDED(hr);  // S_OK / S_FALSE 需配对 CoUninitialize
  }

  // ---- 设备与客户端握手 ----
  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioClient* client = nullptr;
  IAudioRenderClient* render = nullptr;
  WAVEFORMATEX* mix = nullptr;  // GetMixFormat 返回，CoTaskMemFree 释放
  bool started = false;
  UINT32 buf_frames = 0;

  auto cleanup = [&]() {
    if (render) render->Release();
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (mix) CoTaskMemFree(mix);
    if (co_uninit) CoUninitialize();
  };

  do {
    WAVEFORMATEX pcm16 = {};
    pcm16.wFormatTag = WAVE_FORMAT_PCM;
    pcm16.nChannels = kCh;
    pcm16.nSamplesPerSec = kRate;
    pcm16.wBitsPerSample = kBits;
    pcm16.nBlockAlign = static_cast<WORD>(kCh * (kBits / 8));
    pcm16.nAvgBytesPerSec = kRate * pcm16.nBlockAlign;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
      break;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
      break;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&client))))
      break;

    // 首选 16bit/48k/stereo PCM；共享模式若拒绝，回退到等价的混音格式
    // （混音格式必须仍是 16bit PCM/48k/2ch，字节布局才与解码输出一致，
    //  否则本实现不做重采样，直接视为不支持 → audio_drop）。
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    0, 0, &pcm16, nullptr);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
      if (FAILED(client->GetMixFormat(&mix)) || !mix) break;
      bool equiv = mix->nChannels == kCh && mix->nSamplesPerSec == kRate &&
                   mix->wBitsPerSample == kBits;
      if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* ext =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
        if (!IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
          equiv = false;
      } else if (mix->wFormatTag != WAVE_FORMAT_PCM) {
        equiv = false;
      }
      if (!equiv) break;
      hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                              0, 0, mix, nullptr);
    }
    if (FAILED(hr)) break;
    if (FAILED(client->GetBufferSize(&buf_frames)) || buf_frames == 0) break;
    if (FAILED(client->SetEventHandle(g_state.wake_evt))) break;
    if (FAILED(client->GetService(IID_PPV_ARGS(&render)))) break;
    if (FAILED(client->Start())) break;
    started = true;
  } while (false);

  if (!started) {
    // 渲染端不可用 → 解码丢弃音频（防阻塞），视频照常播放
    g_state.audio_drop = true;
    g_state.consume_audio = false;
    g_state.rendering = false;
    cleanup();
    return;
  }

  // ---- 主循环：事件触发 → 填充可写缓冲 ----
  int64_t render_start = clock_ms();  // 淡入计时起点
  bool was_fading_out = false;
  int64_t fade_out_start = 0;
  bool had_error = false;
  int idle_ticks = 0;

  while (g_state.rendering.load()) {
    DWORD wait = WaitForSingleObject(g_state.wake_evt, INFINITE);
    if (wait == WAIT_FAILED) { had_error = true; break; }  // 句柄异常
    if (!g_state.rendering.load()) break;

    UINT32 pad = 0;
    if (FAILED(client->GetCurrentPadding(&pad))) { had_error = true; break; }
    UINT32 avail = buf_frames - pad;
    if (avail == 0) continue;

    // 播放位置：跟随队首音频块时间戳
    {
      std::lock_guard<std::mutex> lk(g_state.aq_mutex);
      if (!g_state.aqueue.empty())
        g_state.current_pos_ms = g_state.aqueue.front().pts_ms;
    }

    // 增益与淡入/淡出包络（每事件周期一个值，≈10ms 步进，听感平滑）
    double env = 1.0;
    int64_t fi = g_state.fade_in_ms.load();
    if (fi > 0) {
      double t = double(clock_ms() - render_start) / double(fi);
      env = std::min(1.0, std::max(0.0, t));
    }
    if (g_state.fading_out.load()) {
      if (!was_fading_out) {
        was_fading_out = true;
        fade_out_start = clock_ms();
      }
      int64_t fo = g_state.fade_out_ms.load();
      double t = fo > 0 ? double(clock_ms() - fade_out_start) / double(fo) : 1.0;
      env *= std::max(0.0, 1.0 - t);
    } else {
      was_fading_out = false;
    }
    double lin = std::pow(10.0, g_state.gain_db.load() / 20.0);

    // 自然播完判定：解码线程已退出且队列为空
    bool drained = false;
    {
      std::lock_guard<std::mutex> lk(g_state.aq_mutex);
      drained = g_state.decode_done.load() && g_state.aqueue.empty();
    }

    BYTE* data = nullptr;
    if (FAILED(render->GetBuffer(avail, &data))) { had_error = true; break; }

    if (drained) {
      // 静音补帧让引擎把缓冲播完；连续两次为空 → 自然结束
      memset(data, 0, size_t(avail) * kFrameBytes);
      render->ReleaseBuffer(avail, 0);
      if (++idle_ticks >= 2) break;
      continue;
    }
    idle_ticks = 0;

    // 从队列取块写入（剩余写回队头；不足补静音）
    int16_t* dst = reinterpret_cast<int16_t*>(data);
    UINT32 written = 0;
    while (written < avail) {
      MediaState::AudioChunk chunk;
      bool got = false;
      {
        std::lock_guard<std::mutex> lk(g_state.aq_mutex);
        if (!g_state.aqueue.empty()) {
          chunk = std::move(g_state.aqueue.front());
          g_state.aqueue.pop_front();
          got = true;
        }
      }
      if (!got) break;

      size_t n_smps = chunk.pcm.size() / sizeof(int16_t);
      UINT32 need = avail - written;
      size_t n = std::min<size_t>(n_smps, need);
      const int16_t* src = reinterpret_cast<const int16_t*>(chunk.pcm.data());
      for (size_t i = 0; i < n; ++i) {
        double s = double(src[i]) * lin * env;
        s = std::max(-32768.0, std::min(32767.0, s));
        dst[written + i] = static_cast<int16_t>(s);
      }
      written += static_cast<UINT32>(n);

      if (n < n_smps) {
        // 未消费完的部分写回队头，保持块序与队首时间戳
        MediaState::AudioChunk rest;
        rest.pts_ms = chunk.pts_ms;
        rest.pcm.assign(chunk.pcm.begin() + (n * sizeof(int16_t)),
                        chunk.pcm.end());
        std::lock_guard<std::mutex> lk(g_state.aq_mutex);
        g_state.aqueue.push_front(std::move(rest));
        break;
      }
    }
    // 数据不足部分补静音
    for (UINT32 i = written; i < avail; ++i) dst[i] = 0;
    render->ReleaseBuffer(avail, 0);

    // stop 淡出完成 → 收尾退出（fade_out_ms<=0 表示立即淡出为 0）
    if (g_state.fading_out.load()) {
      int64_t fo = g_state.fade_out_ms.load();
      if (fo <= 0 || (clock_ms() - fade_out_start) >= fo) break;
    }
  }

  // ---- 收尾 ----
  client->Stop();
  g_state.consume_audio = false;
  if (had_error) g_state.audio_drop = true;  // 中途失效 → 解码丢弃防阻塞
  g_state.rendering = false;
  cleanup();
}

// ---- 平台能力查询 ----
bool has_media_backend() { return true; }

nlohmann::json probe_media(const std::string& file_path) {
  nlohmann::json result;
  AVFormatContext* fmt_ctx = nullptr;

  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    return result;

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    avformat_close_input(&fmt_ctx);
    return result;
  }

  result["duration_ms"] = fmt_ctx->duration / 1000;

  int video_stream = -1, audio_stream = -1;
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    const AVCodecParameters* par = fmt_ctx->streams[i]->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && video_stream < 0) {
      video_stream = int(i);
      result["width"] = par->width;
      result["height"] = par->height;
      AVRational fr = fmt_ctx->streams[i]->avg_frame_rate;
      result["fps"] = fr.den > 0 ? double(fr.num) / fr.den : 0.0;
      result["codec"] = avcodec_get_name(par->codec_id);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream < 0) {
      audio_stream = int(i);
      result["audio_sr"] = par->sample_rate;
      result["audio_ch"] = par->ch_layout.nb_channels;
    }
  }

  avformat_close_input(&fmt_ctx);
  return result;
}

bool open_media_file(const std::string& file_path, int64_t trim_in_ms) {
  // 关闭之前的文件
  close_media_file();

  g_state.trim_in_ms = trim_in_ms;
  g_state.has_audio = false;
  g_state.decode_done = false;

  if (avformat_open_input(&g_state.fmt_ctx, file_path.c_str(),
                          nullptr, nullptr) < 0)
    return false;

  if (avformat_find_stream_info(g_state.fmt_ctx, nullptr) < 0) {
    avformat_close_input(&g_state.fmt_ctx);
    return false;
  }

  g_state.duration_ms = g_state.fmt_ctx->duration / 1000;

  // 查找视频流
  g_state.video_stream = av_find_best_stream(
      g_state.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (g_state.video_stream >= 0) {
    const AVCodec* vcodec = avcodec_find_decoder(
        g_state.fmt_ctx->streams[g_state.video_stream]->codecpar->codec_id);
    if (vcodec) {
      g_state.vcodec = avcodec_alloc_context3(vcodec);
      avcodec_parameters_to_context(g_state.vcodec,
          g_state.fmt_ctx->streams[g_state.video_stream]->codecpar);
      avcodec_open2(g_state.vcodec, vcodec, nullptr);

      // 缩放到 720p（PGM 捕获用）
      int src_w = g_state.fmt_ctx->streams[g_state.video_stream]->codecpar->width;
      int src_h = g_state.fmt_ctx->streams[g_state.video_stream]->codecpar->height;
      g_state.rgb_w = src_w > 1280 ? 1280 : src_w;
      g_state.rgb_h = (g_state.rgb_w * src_h + src_w / 2) / src_w;

      g_state.sws = sws_getContext(src_w, src_h, AV_PIX_FMT_YUV420P,
                                    g_state.rgb_w, g_state.rgb_h,
                                    AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                    nullptr, nullptr, nullptr);
      g_state.rgb_frame = av_frame_alloc();
      g_state.rgb_buf = (uint8_t*)av_malloc(
          av_image_get_buffer_size(AV_PIX_FMT_RGB24, g_state.rgb_w,
                                    g_state.rgb_h, 1));
      av_image_fill_arrays(g_state.rgb_frame->data, g_state.rgb_frame->linesize,
                           g_state.rgb_buf, AV_PIX_FMT_RGB24,
                           g_state.rgb_w, g_state.rgb_h, 1);
    }
  }

  // 查找音频流
  g_state.audio_stream = av_find_best_stream(
      g_state.fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (g_state.audio_stream >= 0) {
    const AVCodec* acodec = avcodec_find_decoder(
        g_state.fmt_ctx->streams[g_state.audio_stream]->codecpar->codec_id);
    if (acodec) {
      g_state.acodec = avcodec_alloc_context3(acodec);
      avcodec_parameters_to_context(g_state.acodec,
          g_state.fmt_ctx->streams[g_state.audio_stream]->codecpar);
      avcodec_open2(g_state.acodec, acodec, nullptr);

      // 重采样为 S16, 48kHz, stereo（FFmpeg ≥6 AVChannelLayout API；
      // FFmpeg 8 已移除旧 bitmask channel layout / AVCodecContext.channels）
      AVChannelLayout in_layout;
      av_channel_layout_default(&in_layout, 2);
      if (g_state.acodec->ch_layout.nb_channels > 0) {
        in_layout = g_state.acodec->ch_layout;
        if (in_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
          int n = in_layout.nb_channels;
          av_channel_layout_uninit(&in_layout);
          av_channel_layout_default(&in_layout, n);
        }
      }
      AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
      if (swr_alloc_set_opts2(&g_state.swr, &out_layout, AV_SAMPLE_FMT_S16,
                              48000, &in_layout, g_state.acodec->sample_fmt,
                              g_state.acodec->sample_rate, 0, nullptr) < 0 ||
          !g_state.swr) {
        if (g_state.swr) {
          swr_free(&g_state.swr);
          g_state.swr = nullptr;
        }
      } else {
        swr_init(g_state.swr);
      }

      g_state.audio_pcm = av_frame_alloc();
      if (g_state.audio_pcm) {
        g_state.audio_pcm->format = AV_SAMPLE_FMT_S16;
        g_state.audio_pcm->ch_layout = out_layout;
        g_state.audio_pcm->nb_samples = 4096;
        if (av_frame_get_buffer(g_state.audio_pcm, 0) < 0) {
          av_frame_free(&g_state.audio_pcm);
          if (g_state.swr) {
            swr_free(&g_state.swr);
            g_state.swr = nullptr;
          }
        }
      }

      // 音频真正可用：解码器 + 重采样器 + PCM 缓冲全部就绪
      g_state.has_audio =
          (g_state.acodec && g_state.swr && g_state.audio_pcm);
    }
  }

  return true;
}

void start_media_playback(double gain_db, int64_t fade_in_ms) {
  g_state.gain_db = gain_db;
  g_state.fade_in_ms = fade_in_ms;
  g_state.fading_out = false;
  g_state.seek_stop = false;

  // 音频渲染路径决策：设备可用且有音频流 → 创建事件并启用消费；
  // 设备不可用 / 事件创建失败 → audio_drop（解码丢弃音频，纯视频照播）
  g_state.audio_drop = false;
  g_state.consume_audio = false;
#if defined(_WIN32)
  if (g_state.has_audio) {
    if (g_state.wasapi_available) {
      g_state.wake_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (!g_state.wake_evt) g_state.audio_drop = true;
    } else {
      g_state.audio_drop = true;
    }
  }
#else
  if (g_state.has_audio) g_state.audio_drop = true;
#endif

  g_state.decoding = true;
  g_state.decode_thread = std::thread(decode_loop);

  // 渲染线程：仅当事件有效时启动；中途初始化失败由 render_loop 自行收尾
  if (g_state.has_audio && g_state.wake_evt) {
    g_state.rendering = true;
    g_state.consume_audio = true;
    g_state.render_thread = std::thread(render_loop);
  }
}

void stop_media_playback(int64_t fade_out_ms) {
  g_state.fading_out = true;
  g_state.fade_out_ms = fade_out_ms;

  g_state.decoding = false;
  g_state.seek_stop = true;
  g_state.vq_cv.notify_all();
  g_state.aq_cv.notify_all();

  if (g_state.decode_thread.joinable())
    g_state.decode_thread.join();

  // 停渲染：唤醒事件 → 渲染线程按淡出窗口自行退出 → join 回收
#if defined(_WIN32)
  if (g_state.wake_evt) SetEvent(g_state.wake_evt);
#endif
  if (g_state.render_thread.joinable())
    g_state.render_thread.join();
#if defined(_WIN32)
  if (g_state.wake_evt) {
    CloseHandle(g_state.wake_evt);
    g_state.wake_evt = nullptr;
  }
#endif
  g_state.consume_audio = false;
}

int64_t get_media_pos_ms() {
  // 基于音频时钟（如果有音频流）
  if (!g_state.aqueue.empty()) {
    return g_state.aqueue.front().pts_ms;
  }
  // 回退到系统时钟
  if (g_state.start_clock_ms > 0) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now - g_state.start_clock_ms.load()) + g_state.trim_in_ms.load();
  }
  return 0;
}

int64_t get_media_duration_ms(const std::string& file_path) {
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    return 0;
  int64_t dur = fmt_ctx->duration / 1000;
  avformat_close_input(&fmt_ctx);
  return dur;
}

void close_media_file() {
  // 先停渲染端（若存在）：置位 + 唤醒事件 + join，随后回收事件；
  // 渲染线程退出后 aqueue 才无并发消费者，可安全清空
  g_state.rendering = false;
  g_state.consume_audio = false;
#if defined(_WIN32)
  if (g_state.wake_evt) SetEvent(g_state.wake_evt);
#endif
  if (g_state.render_thread.joinable())
    g_state.render_thread.join();
#if defined(_WIN32)
  if (g_state.wake_evt) {
    CloseHandle(g_state.wake_evt);
    g_state.wake_evt = nullptr;
  }
#endif
  g_state.audio_drop = false;

  g_state.decoding = false;
  g_state.seek_stop = true;
  g_state.vq_cv.notify_all();
  g_state.aq_cv.notify_all();

  if (g_state.decode_thread.joinable())
    g_state.decode_thread.join();

  if (g_state.sws) { sws_freeContext(g_state.sws); g_state.sws = nullptr; }
  if (g_state.swr) { swr_free(&g_state.swr); g_state.swr = nullptr; }
  if (g_state.rgb_frame) { av_frame_free(&g_state.rgb_frame); }
  if (g_state.rgb_buf) { av_free(g_state.rgb_buf); g_state.rgb_buf = nullptr; }
  if (g_state.audio_pcm) { av_frame_free(&g_state.audio_pcm); }
  if (g_state.vcodec) { avcodec_free_context(&g_state.vcodec); }
  if (g_state.acodec) { avcodec_free_context(&g_state.acodec); }
  if (g_state.fmt_ctx) { avformat_close_input(&g_state.fmt_ctx); }

  g_state.vqueue.clear();
  g_state.aqueue.clear();
  g_state.start_clock_ms = 0;
  g_state.current_pos_ms = 0;
}

// ---- 缩略图生成 ----
std::string generate_thumbnail(const std::string& file_path, int64_t duration_ms) {
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    return "";

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    avformat_close_input(&fmt_ctx);
    return "";
  }

  int vs = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vs < 0) {
    avformat_close_input(&fmt_ctx);
    return "";
  }

  const AVCodec* codec = avcodec_find_decoder(
      fmt_ctx->streams[vs]->codecpar->codec_id);
  if (!codec) {
    avformat_close_input(&fmt_ctx);
    return "";
  }

  AVCodecContext* cctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(cctx, fmt_ctx->streams[vs]->codecpar);
  if (avcodec_open2(cctx, codec, nullptr) < 0) {
    avcodec_free_context(&cctx);
    avformat_close_input(&fmt_ctx);
    return "";
  }

  // seek 到 1 秒
  int64_t seek_target = 1000;  // ms
  if (duration_ms > 0 && duration_ms < 2000)
    seek_target = duration_ms / 2;
  int64_t ts = seek_target * 1000;  // AV_TIME_BASE 微秒
  av_seek_frame(fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);

  AVFrame* frame = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();

  // 读取直到获得一帧
  bool got_frame = false;
  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == vs) {
      if (avcodec_send_packet(cctx, pkt) >= 0) {
        while (avcodec_receive_frame(cctx, frame) >= 0) {
          got_frame = true;
          break;
        }
      }
    }
    av_packet_unref(pkt);
    if (got_frame) break;
  }

  std::string result;
  if (got_frame) {
    // 缩放到 320px 宽
    int src_w = cctx->width;
    int src_h = cctx->height;
    int dst_w = 320;
    int dst_h = (dst_w * src_h + src_w / 2) / src_w;

    SwsContext* sws = sws_getContext(src_w, src_h, AV_PIX_FMT_YUV420P,
                                     dst_w, dst_h, AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    AVFrame* rgb = av_frame_alloc();
    uint8_t* buf = (uint8_t*)av_malloc(
        av_image_get_buffer_size(AV_PIX_FMT_RGB24, dst_w, dst_h, 1));
    av_image_fill_arrays(rgb->data, rgb->linesize, buf,
                         AV_PIX_FMT_RGB24, dst_w, dst_h, 1);

    sws_scale(sws, frame->data, frame->linesize, 0, src_h,
              rgb->data, rgb->linesize);

    // 自包含 baseline JPEG 编码（YCbCr 4:2:0，质量≈85）→ base64
    // rgb 由 av_image_fill_arrays(align=1) 分配，行连续无填充，可用默认行距
    std::vector<uint8_t> jpg =
        encode_jpeg_rgb24(rgb->data[0], dst_w, dst_h);
    if (!jpg.empty()) result = base64_encode(jpg.data(), jpg.size());

    av_free(buf);
    av_frame_free(&rgb);
    sws_freeContext(sws);
  }

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&cctx);
  avformat_close_input(&fmt_ctx);
  return result;
}

// ---- PGM 渲染上下文（P1-2）----
// 延迟创建 D3D11 设备 + 合成器 + 视频上传纹理。合成器不可 resize，
// 画面尺寸变化（或首次 / 上次创建失败）时整体重建。
// 返回 false = 无渲染上下文，调用方按 media_backend.h 语义返回空串。
#if defined(_WIN32) && defined(SM_HAS_D3D11)
static bool ensure_pgm_render_context(int video_w, int video_h) {
  if (video_w <= 0 || video_h <= 0) return false;

  if (!g_state.rhi_dev) {
    g_state.rhi_dev = sm::rhi::create_device();
    if (!g_state.rhi_dev) return false;
    std::fprintf(stderr, "[pgm] RHI backend: %s\n",
                 g_state.rhi_dev->backend_name());
  }

  const std::uint32_t w = static_cast<std::uint32_t>(video_w);
  const std::uint32_t h = static_cast<std::uint32_t>(video_h);
  if (!g_state.mixer || !g_state.layer_tex[0] ||
      g_state.mixer->width() != w || g_state.mixer->height() != h) {
    for (auto& t : g_state.layer_tex) t = nullptr;
    g_state.mixer = g_state.rhi_dev->create_pgm_mixer(w, h);
    if (!g_state.mixer) return false;

    sm::rhi::Texture2DDesc desc{};
    desc.format = sm::rhi::PixelFormat::Rgba8;
    desc.width = w;
    desc.height = h;
    desc.render_target = false;  // 普通上传纹理（视频帧）
    g_state.layer_tex[0] = g_state.rhi_dev->create_texture(desc);
    if (!g_state.layer_tex[0]) {
      g_state.mixer = nullptr;  // 允许下次调用重试
      return false;
    }
  }
  return true;
}
#endif

// ---- D3D11 PGM 帧捕获（规格 2.3 / P1-2）----
// 有渲染上下文：最新解码帧 → 上传纹理 → 4 层混合合成 → 回读渲染帧 →
// JPEG base64。无渲染上下文（stub / 设备不可用 / 无帧）：返回空串，
// 对齐 media_backend.h"如果无 D3D11 渲染上下文，返回空字符串"。
std::string capture_pgm_frame_jpeg() {
#if defined(_WIN32) && defined(SM_HAS_D3D11)
  // 1) 队列锁内快速拷贝最新帧（编码/渲染不在锁内做，避免阻塞解码线程）
  MediaState::VideoFrame vf;
  {
    std::lock_guard<std::mutex> lk(g_state.vq_mutex);
    if (g_state.vqueue.empty()) return "";
    vf = g_state.vqueue.back();
  }
  if (vf.rgb.empty()) return "";

  // 2) 渲染上下文（RHI 单所有者线程：capture 调用方）
  std::lock_guard<std::mutex> rlk(g_state.rhi_mutex);
  if (!ensure_pgm_render_context(vf.width, vf.height)) return "";

  const std::uint32_t w = static_cast<std::uint32_t>(vf.width);
  const std::uint32_t h = static_cast<std::uint32_t>(vf.height);

  // 3) RGB24 → RGBA8（alpha=255）→ 上传 → 绑定 layer0（Replace 直通）
  std::vector<std::uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  const std::uint8_t* src = vf.rgb.data();
  std::uint8_t* dst = rgba.data();
  for (std::uint32_t y = 0; y < h; ++y) {
    const std::uint8_t* row = src + static_cast<size_t>(y) * w * 3;
    std::uint8_t* drow = dst + static_cast<size_t>(y) * w * 4;
    for (std::uint32_t x = 0; x < w; ++x) {
      drow[x * 4 + 0] = row[x * 3 + 0];
      drow[x * 4 + 1] = row[x * 3 + 1];
      drow[x * 4 + 2] = row[x * 3 + 2];
      drow[x * 4 + 3] = 255;
    }
  }
  if (!g_state.layer_tex[0]->upload(rgba.data(), w * 4)) return "";

  sm::rhi::BlendConfig cfg;
  cfg.enable = true;
  cfg.op = sm::rhi::BlendOp::Replace;  // 视频为底层：直通覆盖
  cfg.opacity = 1.0f;
  cfg.mask_key = 0;
  g_state.mixer->set_layer(0, g_state.layer_tex[0].get(), cfg, true);
  // 其余层当前无源（字幕/logo 属后续任务），显式解绑防残留
  for (int i = 1; i < sm::rhi::IPgmMixer::kMaxLayers; ++i)
    g_state.mixer->set_layer(i, nullptr, sm::rhi::BlendConfig{}, true);

  // 4) 合成（不透明黑底）→ 回读渲染帧
  std::vector<std::uint8_t> rendered;
  if (!g_state.mixer->compose_and_readback(0x000000FFu, rendered)) return "";
  if (rendered.size() < static_cast<size_t>(w) * h * 4) return "";

  // 5) RGBA8 → RGB24 → JPEG → base64
  std::vector<std::uint8_t> rgb24(static_cast<size_t>(w) * h * 3);
  const std::uint8_t* rs = rendered.data();
  std::uint8_t* rd = rgb24.data();
  for (std::uint32_t y = 0; y < h; ++y) {
    const std::uint8_t* row = rs + static_cast<size_t>(y) * w * 4;
    std::uint8_t* drow = rd + static_cast<size_t>(y) * w * 3;
    for (std::uint32_t x = 0; x < w; ++x) {
      drow[x * 3 + 0] = row[x * 4 + 0];
      drow[x * 3 + 1] = row[x * 4 + 1];
      drow[x * 3 + 2] = row[x * 4 + 2];
    }
  }
  std::vector<std::uint8_t> jpg =
      encode_jpeg_rgb24(rgb24.data(), vf.width, vf.height);
  if (jpg.empty()) return "";
  return base64_encode(jpg.data(), jpg.size());
#else
  // 无 D3D11 渲染上下文（macOS/Linux stub / Windows 未启用 D3D11）→ 空串
  return "";
#endif
}

// ---- WASAPI 音频初始化 ----
bool init_audio_output() {
  // 真实探测默认渲染端点；结果写入 wasapi_available 供 start 决策
#if defined(_WIN32)
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool co_uninit =
      (SUCCEEDED(hr) && hr != static_cast<HRESULT>(RPC_E_CHANGED_MODE));
  if (FAILED(hr) && hr != static_cast<HRESULT>(RPC_E_CHANGED_MODE)) {
    g_state.wasapi_available = false;
    return false;
  }

  bool ok = false;
  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                        IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr) && enumerator)
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  ok = (SUCCEEDED(hr) && device != nullptr);

  if (device) device->Release();
  if (enumerator) enumerator->Release();
  if (co_uninit) CoUninitialize();
  g_state.wasapi_available = ok;
  return ok;
#else
  return false;
#endif
}

void shutdown_audio_output() {
  // 兜底：渲染线程与事件通常在 stop/close 已回收；若进程退出前仍有
  // 残留渲染线程（例如未走 close 的异常路径），在此强制收尾
#if defined(_WIN32)
  g_state.rendering = false;
  g_state.consume_audio = false;
  if (g_state.wake_evt) SetEvent(g_state.wake_evt);
  if (g_state.render_thread.joinable())
    g_state.render_thread.join();
  if (g_state.wake_evt) {
    CloseHandle(g_state.wake_evt);
    g_state.wake_evt = nullptr;
  }
#endif
}

#endif // SM_HAS_FFMPEG

} // namespace platform
} // namespace sm
