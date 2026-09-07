// 极简单元测试骨架（无第三方依赖；断言失败计数并在 main 出口返回非 0）
#pragma once

#include <cstdio>
#include <string>

namespace smtest {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report(bool ok, const char* file, int line, const std::string& expr) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::fprintf(stderr, "  [FAIL] %s:%d  %s\n", file, line, expr.c_str());
  }
}

template <typename A, typename B>
void report_eq(const A& a, const B& b, const char* file, int line, const char* sa,
               const char* sb) {
  ++g_checks;
  if (!(a == b)) {
    ++g_failures;
    std::fprintf(stderr, "  [FAIL] %s:%d  %s == %s 不成立\n", file, line, sa, sb);
  }
}

inline int finish(const char* name) {
  if (g_failures == 0) {
    std::printf("PASS %s（%d 项检查）\n", name, g_checks);
    return 0;
  }
  std::printf("FAIL %s（失败 %d/%d）\n", name, g_failures, g_checks);
  return 1;
}

}  // namespace smtest

#define SM_CHECK(cond) smtest::report(!!(cond), __FILE__, __LINE__, #cond)
#define SM_CHECK_MSG(cond, msg) smtest::report(!!(cond), __FILE__, __LINE__, (msg))
#define SM_CHECK_EQ(a, b) smtest::report_eq((a), (b), __FILE__, __LINE__, #a, #b)
