// 最小 ZIP 容器读写（.showproj 明文 zip 层，规格 §8）
//
// 设计取舍（为什么不用 minizip-ng / zlib）：
//   规格第 8 章要求工程文件是「明文 zip」——即包内内容可被任意 zip 工具直接
//   查看与解包。故本实现只写 STORE 条目（压缩方法 0，不压缩）：
//     * 已压过的媒体（mp4/jpg/png/...）再 deflate 收益极低；
//     * manifest.json 体量本就很小；
//     * 零第三方依赖 ⇒ 内核里程碑在 macOS/Windows 均可本机自测，
//       CI 也无需新增 vcpkg 包或 Qt 之外的构建前置。
//
// 完整性策略：写入时逐条计算 CRC-32，读取时对 EOCD / 中央目录 / 本地头 /
// 尺寸 / CRC-32 逐层核对，任一不符即失败——不做「尽力而为」的部分成功解析。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

// 包内一个条目（STORE：data 即原始字节）
struct ZipEntry {
  std::string name;  // 包内相对路径，统一以 '/' 分隔
  std::string data;  // 内容
};

// 读出的条目信息：内容 + 记录在包内的 CRC-32（便于调用方二次核对）
struct ZipReadEntry {
  std::string name;
  std::string data;
  std::uint32_t crc32 = 0;
};

// CRC-32（IEEE 802.3，多项式 0xEDB88320 反序），与 zip 规范一致
std::uint32_t crc32_bytes(const void* data, std::size_t len);

// ---- 容器层 IO 原语（zip 写入与打包共用）----
bool read_file_bytes(const std::string& path, std::string& out, std::string& err);
bool write_file_bytes(const std::string& path, const std::string& data, std::string& err);

// 写出 zip（全部 STORE，文件头时间戳固定为 1980-01-01 以保证输出确定）；
// 失败返回 false 并置 err。超过 4 GiB 需要 zip64，本版本明确报错而非静默截断。
bool zip_write_file(const std::string& path, const std::vector<ZipEntry>& entries,
                    std::string& err);

// 读入 zip 并做完整性校验；失败返回 false 并置 err（err 为中文原因）
bool zip_read_file(const std::string& path, std::vector<ZipReadEntry>& out, std::string& err);

// 从内存读取（便于用例直接构造损坏包，无需落盘）
bool zip_read_bytes(const std::string& bytes, std::vector<ZipReadEntry>& out,
                    std::string& err);

}  // namespace sm
