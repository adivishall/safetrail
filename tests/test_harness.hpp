#pragma once
// Minimal test harness. No gtest -- one more dependency we do not need.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

namespace t {
inline int failures = 0, checks = 0;
inline void ok(bool cond, const std::string& what) {
  ++checks;
  if (!cond) { ++failures; printf("  \033[31mFAIL\033[0m %s\n", what.c_str()); }
}
inline void near(double a, double b, double tol, const std::string& what) {
  ok(std::fabs(a - b) <= tol, what + "  (" + std::to_string(a) + " vs " +
                                  std::to_string(b) + ")");
}
inline int report(const char* name) {
  printf("%s %-42s %d checks, %d failed\n",
         failures ? "\033[31m[FAIL]\033[0m" : "\033[32m[ ok ]\033[0m", name, checks, failures);
  return failures ? 1 : 0;
}
}  // namespace t
