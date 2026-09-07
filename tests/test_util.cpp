// 工具函数单元测试（规格 §5.1 时间/时钟、§8.3 哈希与素材命名）
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "core/util.h"
#include "test_common.h"

namespace {

void test_sha256_vectors() {
  // 标准测试向量（FIPS 180-4）
  SM_CHECK_EQ(sm::sha256_hex(""),
              std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  SM_CHECK_EQ(sm::sha256_hex("abc"),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void test_sha256_file() {
  const std::string dir = (std::filesystem::temp_directory_path() / "sm_test_util").string();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + "/abc.txt";
  {
    std::ofstream out(path, std::ios::binary);
    out << "abc";
  }
  std::string hex, err;
  SM_CHECK(sm::sha256_file_hex(path, hex, err));
  SM_CHECK_EQ(hex, sm::sha256_hex("abc"));

  std::string dummy;
  SM_CHECK(!sm::sha256_file_hex(dir + "/not_exist.bin", dummy, err));
  SM_CHECK(!err.empty());

  std::filesystem::remove(path, ec);
  std::filesystem::remove_all(dir, ec);
}

void test_uuid() {
  const std::string a = sm::uuid_hex32();
  const std::string b = sm::uuid_hex32();
  SM_CHECK_EQ(a.size(), std::size_t(32));
  SM_CHECK(a != b);
  const std::string allowed = "0123456789abcdef";
  for (char c : a) SM_CHECK(allowed.find(c) != std::string::npos);

  std::set<std::string> seen;
  for (int i = 0; i < 200; ++i) seen.insert(sm::uuid_hex32());
  SM_CHECK_EQ(seen.size(), std::size_t(200));  // 无碰撞
}

void test_clock_and_iso8601() {
  const std::int64_t t1 = sm::now_monotonic_ms();
  const std::int64_t t2 = sm::now_monotonic_ms();
  SM_CHECK(t1 >= 0 && t2 >= t1);  // 单调不回拨

  const std::string iso = sm::iso8601_now();
  SM_CHECK(iso.size() > 15);
  SM_CHECK(iso.find('T') != std::string::npos);
  // 含时区偏移，形如 +08:00 / -05:00 等
  SM_CHECK(iso.find('+') != std::string::npos || iso.find('-') != std::string::npos);
}

void test_pack_media_name() {
  const std::string full(64, 'a');
  SM_CHECK_EQ(sm::pack_media_name(full, "clip.mp4"),
              std::string("aaaaaaaaaaaaaaaa_clip.mp4"));  // 取哈希前 16 位
  const std::string hash = sm::sha256_hex("abc");
  SM_CHECK(sm::pack_media_name(hash, "orig.mov").compare(0, 16, hash.substr(0, 16)) == 0);
}

}  // namespace

int main() {
  test_sha256_vectors();
  test_sha256_file();
  test_uuid();
  test_clock_and_iso8601();
  test_pack_media_name();
  return smtest::finish("test_util");
}
