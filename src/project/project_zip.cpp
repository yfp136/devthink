#include "project/project_zip.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace sm {

namespace {

// ---- 常量（PKWARE APPNOTE）----
constexpr std::uint32_t kSigLocal = 0x04034b50u;
constexpr std::uint32_t kSigCentral = 0x02014b50u;
constexpr std::uint32_t kSigEocd = 0x06054b50u;
constexpr std::uint16_t kVersionNeeded = 20;    // 2.0：仅 STORE/deflate
constexpr std::uint16_t kFlagUtf8 = 0x0800;     // bit 11：文件名为 UTF-8
constexpr std::uint16_t kMethodStore = 0;
constexpr std::uint16_t kDosTime = 0;           // 固定 00:00:00
constexpr std::uint16_t kDosDate = 0x0021;      // 固定 1980-01-01
constexpr std::uint32_t kMax32 = 0xFFFFFFFFu;   // zip64 分界

// ---- 小端读写 ----
void put16(std::string& out, std::uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put32(std::string& out, std::uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
  out.push_back(static_cast<char>((v >> 16) & 0xFF));
  out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::uint16_t get16(const std::string& s, std::size_t off) {
  return static_cast<std::uint16_t>(static_cast<unsigned char>(s[off]) |
                                    (static_cast<unsigned char>(s[off + 1]) << 8));
}

std::uint32_t get32(const std::string& s, std::size_t off) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}

}  // namespace

bool read_file_bytes(const std::string& path, std::string& out, std::string& err) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    err = "无法打开文件: " + path;
    return false;
  }
  out.clear();
  std::array<char, 64 * 1024> buf{};
  for (;;) {
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    if (got > 0) out.append(buf.data(), got);
    if (got < buf.size()) {
      if (std::ferror(f) != 0) {
        std::fclose(f);
        err = "读取文件失败: " + path;
        return false;
      }
      break;  // EOF
    }
  }
  std::fclose(f);
  return true;
}

bool write_file_bytes(const std::string& path, const std::string& data, std::string& err) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    err = "无法创建文件: " + path;
    return false;
  }
  if (!data.empty() && std::fwrite(data.data(), 1, data.size(), f) != data.size()) {
    std::fclose(f);
    err = "写入文件失败: " + path;
    return false;
  }
  if (std::fclose(f) != 0) {
    err = "关闭文件失败（可能磁盘已满）: " + path;
    return false;
  }
  return true;
}

std::uint32_t crc32_bytes(const void* data, std::size_t len) {
  static std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
    return t;
  }();

  std::uint32_t crc = 0xFFFFFFFFu;
  const auto* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < len; ++i) {
    crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

bool zip_write_file(const std::string& path, const std::vector<ZipEntry>& entries,
                    std::string& err) {
  std::string out;
  std::string central;

  // 各条目本地头偏移（写中央目录时需要）
  std::vector<std::uint32_t> offsets;
  offsets.reserve(entries.size());

  for (const auto& e : entries) {
    if (e.name.empty()) {
      err = "zip 条目名为空";
      return false;
    }
    if (e.name.size() > 0xFFFFu || e.data.size() > kMax32) {
      err = "条目超出 zip32 限制（>4GiB 或名称过长），本版本不支持 zip64: " + e.name;
      return false;
    }
    if (out.size() > kMax32) {
      // 中央目录偏移必须能在 32 位内表示
      err = "包体超出 zip32 限制（>4GiB），本版本不支持 zip64";
      return false;
    }

    const std::uint32_t crc = crc32_bytes(e.data.data(), e.data.size());
    const std::uint32_t size = static_cast<std::uint32_t>(e.data.size());
    const std::uint32_t off = static_cast<std::uint32_t>(out.size());
    offsets.push_back(off);

    const std::uint16_t name_len = static_cast<std::uint16_t>(e.name.size());
    put32(out, kSigLocal);
    put16(out, kVersionNeeded);
    put16(out, kFlagUtf8);
    put16(out, kMethodStore);
    put16(out, kDosTime);
    put16(out, kDosDate);
    put32(out, crc);
    put32(out, size);  // compressed size == uncompressed size（STORE）
    put32(out, size);
    put16(out, name_len);
    put16(out, 0);  // extra len
    out.append(e.name);
    out.append(e.data);
  }

  if (out.size() > kMax32) {
    err = "包体超出 zip32 限制（>4GiB），本版本不支持 zip64";
    return false;
  }
  const std::uint32_t cd_offset = static_cast<std::uint32_t>(out.size());

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    const std::uint32_t crc = crc32_bytes(e.data.data(), e.data.size());
    const std::uint32_t size = static_cast<std::uint32_t>(e.data.size());
    const std::uint16_t name_len = static_cast<std::uint16_t>(e.name.size());
    put32(central, kSigCentral);
    put16(central, kVersionNeeded);  // version made by
    put16(central, kVersionNeeded);  // version needed
    put16(central, kFlagUtf8);
    put16(central, kMethodStore);
    put16(central, kDosTime);
    put16(central, kDosDate);
    put32(central, crc);
    put32(central, size);
    put32(central, size);
    put16(central, name_len);
    put16(central, 0);  // extra len
    put16(central, 0);  // comment len
    put16(central, 0);  // disk number start
    put16(central, 0);  // internal attrs
    put32(central, 0);  // external attrs
    put32(central, offsets[i]);
    central.append(e.name);
  }

  if (entries.size() > 0xFFFFu || central.size() > kMax32) {
    err = "条目数或中央目录超出 zip32 限制，本版本不支持 zip64";
    return false;
  }
  const std::uint32_t cd_size = static_cast<std::uint32_t>(central.size());
  const std::uint16_t count = static_cast<std::uint16_t>(entries.size());

  out.append(central);
  put32(out, kSigEocd);
  put16(out, 0);  // disk number
  put16(out, 0);  // disk with central dir
  put16(out, count);
  put16(out, count);
  put32(out, cd_size);
  put32(out, cd_offset);
  put16(out, 0);  // comment len

  return write_file_bytes(path, out, err);
}

bool zip_read_bytes(const std::string& bytes, std::vector<ZipReadEntry>& out,
                    std::string& err) {
  out.clear();

  // ---- 1. 定位 EOCD（从尾部向前搜，注释最长 65535 字节）----
  if (bytes.size() < 22) {
    err = "包体过小，不是有效 zip";
    return false;
  }
  const std::size_t max_back = 22 + 0xFFFFu;
  const std::size_t search_from = bytes.size() > max_back ? bytes.size() - max_back : 0;
  std::size_t eocd = std::string::npos;
  for (std::size_t i = bytes.size() - 22 + 1; i-- > search_from;) {
    if (get32(bytes, i) == kSigEocd) {
      eocd = i;
      break;
    }
  }
  if (eocd == std::string::npos) {
    err = "未找到 zip 结束记录（EOCD），包可能被截断或损坏";
    return false;
  }
  if (eocd + 22 > bytes.size()) {
    err = "EOCD 越界，包已损坏";
    return false;
  }

  const std::uint16_t count = get16(bytes, eocd + 10);
  const std::uint32_t cd_size = get32(bytes, eocd + 12);
  const std::uint32_t cd_offset = get32(bytes, eocd + 16);

  if (static_cast<std::size_t>(cd_offset) + cd_size > eocd) {
    err = "中央目录范围越界，包已损坏";
    return false;
  }

  // ---- 2. 逐条解析中央目录 ----
  struct CdItem {
    std::string name;
    std::uint32_t crc = 0;
    std::uint32_t size = 0;
    std::uint32_t lfh_off = 0;
    std::uint16_t method = 0;
  };
  std::vector<CdItem> items;
  items.reserve(count);

  std::size_t p = cd_offset;
  for (std::uint16_t i = 0; i < count; ++i) {
    if (p + 46 > bytes.size() || get32(bytes, p) != kSigCentral) {
      err = "中央目录条目签名无效，包已损坏";
      return false;
    }
    CdItem it;
    it.method = get16(bytes, p + 10);
    it.crc = get32(bytes, p + 16);
    it.size = get32(bytes, p + 24);
    const std::uint16_t name_len = get16(bytes, p + 28);
    const std::uint16_t extra_len = get16(bytes, p + 30);
    const std::uint16_t comment_len = get16(bytes, p + 32);
    it.lfh_off = get32(bytes, p + 42);

    if (p + 46 + name_len > bytes.size()) {
      err = "中央目录条目名称越界，包已损坏";
      return false;
    }
    it.name = bytes.substr(p + 46, name_len);
    p += 46u + name_len + extra_len + comment_len;

    if (it.method != kMethodStore) {
      err = "包内条目使用了不支持的压缩方法（仅支持 0/STORE）: " + it.name;
      return false;
    }
    items.push_back(std::move(it));
  }

  // ---- 3. 逐条读本地头 + 数据并核对 CRC ----
  for (const auto& it : items) {
    const std::size_t lfh = it.lfh_off;
    if (lfh + 30 > bytes.size() || get32(bytes, lfh) != kSigLocal) {
      err = "本地文件头签名无效，包已损坏: " + it.name;
      return false;
    }
    const std::uint16_t l_method = get16(bytes, lfh + 8);
    const std::uint32_t l_crc = get32(bytes, lfh + 14);
    const std::uint32_t l_size = get32(bytes, lfh + 22);
    const std::uint16_t l_name_len = get16(bytes, lfh + 26);
    const std::uint16_t l_extra_len = get16(bytes, lfh + 28);

    if (l_method != kMethodStore) {
      err = "本地头压缩方法不受支持（仅支持 0/STORE）: " + it.name;
      return false;
    }
    if (l_crc != it.crc || l_size != it.size) {
      err = "中央目录与本地头信息不一致，包已损坏: " + it.name;
      return false;
    }
    const std::size_t data_off = lfh + 30u + l_name_len + l_extra_len;
    if (data_off + l_size > bytes.size()) {
      err = "条目数据越界，包已损坏: " + it.name;
      return false;
    }

    ZipReadEntry e;
    e.name = it.name;
    e.data = bytes.substr(data_off, l_size);
    e.crc32 = it.crc;
    if (crc32_bytes(e.data.data(), e.data.size()) != it.crc) {
      err = "条目 CRC-32 校验失败（内容损坏）: " + it.name;
      return false;
    }
    out.push_back(std::move(e));
  }

  return true;
}

bool zip_read_file(const std::string& path, std::vector<ZipReadEntry>& out,
                   std::string& err) {
  std::string bytes;
  if (!read_file_bytes(path, bytes, err)) return false;
  return zip_read_bytes(bytes, out, err);
}

}  // namespace sm
