// 纯逻辑内核工具：时钟 / 时间戳 / UUID / SHA-256
// 规格 §5.1：信封时间用宿主启动后毫秒单调时钟（monotonic_ms）+ 系统时间 ts。
// SHA-256 为工程文件与素材的统一哈希（§2.1/§8.3），此处内嵌标准实现，
// 无第三方依赖即可在本机(内核里程碑)与 Windows 全平台自测。
#pragma once

#include <cstdint>
#include <string>

namespace sm {

// 宿主启动后的毫秒单调时钟（std::chrono::steady_clock，永不回拨）
std::int64_t now_monotonic_ms();

// 当前系统时间 ISO8601（含时区偏移），如 2026-09-07T10:00:00.000+08:00
std::string iso8601_now();

// 生成 32 位小写十六进制 UUID（去横线），用于各 *_id 主键
std::string uuid_hex32();

// ---- SHA-256（自实现，输出小写十六进制）----
// 标准测试向量：
//   sha256_hex("")    == e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
//   sha256_hex("abc") == ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
std::string sha256_hex(const std::string& data);

// 计算文件 SHA-256（§8.3 ref/pack 模式哈希依据）；失败返回 false 并置 err
bool sha256_file_hex(const std::string& path, std::string& hex_out, std::string& err);

// 包内素材命名：取哈希前 16 位 + 原文件名（§8.1 media/<sha256前16位>_<原文件名>）
std::string pack_media_name(const std::string& sha256_hex_full, const std::string& original_name);

}  // namespace sm
