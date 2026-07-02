// olv_test.hpp — minimal single-header test framework.
//
// Intentionally tiny (~120 lines) so the repository carries zero third-party
// test code (air-gap friendly; trade-off documented in docs/PLAN.md §10).
// No fixtures, no subcases: a flat list of OLV_TEST cases with CHECK macros.
//
// Usage:
//   #include "olv_test.hpp"
//   OLV_TEST(my_case) { OLV_CHECK_EQ(1 + 1, 2); }
//   OLV_TEST_MAIN()
//
// Each failed check prints file:line and the expression/values, marks the
// test failed, and execution continues. The process exits non-zero if any
// test failed. `--list` prints test names without running them.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace olv::test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}

inline bool& currentFailed() {
  static bool failed = false;
  return failed;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <typename A, typename B>
inline void reportFailure(const char* file, int line, const char* what, const A& a, const B& b) {
  std::ostringstream os;
  os << a << " vs " << b;
  std::fprintf(stderr, "    FAIL %s:%d  %s  (%s)\n", file, line, what, os.str().c_str());
  currentFailed() = true;
}

inline void reportFailure(const char* file, int line, const char* what) {
  std::fprintf(stderr, "    FAIL %s:%d  %s\n", file, line, what);
  currentFailed() = true;
}

inline int runAll(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
    for (const Case& c : registry()) std::printf("%s\n", c.name);
    return 0;
  }
  int failed = 0;
  for (const Case& c : registry()) {
    currentFailed() = false;
    std::printf("[ RUN  ] %s\n", c.name);
    c.fn();
    if (currentFailed()) {
      std::printf("[ FAIL ] %s\n", c.name);
      ++failed;
    } else {
      std::printf("[  OK  ] %s\n", c.name);
    }
  }
  std::printf("%zu test(s), %d failure(s)\n", registry().size(), failed);
  return failed == 0 ? 0 : 1;
}

}  // namespace olv::test

#define OLV_TEST(name)                                                           \
  static void olv_test_fn_##name();                                              \
  static ::olv::test::Registrar olv_test_reg_##name(#name, &olv_test_fn_##name); \
  static void olv_test_fn_##name()

#define OLV_CHECK(cond)                                                 \
  do {                                                                  \
    if (!(cond)) ::olv::test::reportFailure(__FILE__, __LINE__, #cond); \
  } while (0)

#define OLV_CHECK_EQ(a, b)                                                          \
  do {                                                                              \
    const auto olv_va = (a);                                                        \
    const auto olv_vb = (b);                                                        \
    if (!(olv_va == olv_vb))                                                        \
      ::olv::test::reportFailure(__FILE__, __LINE__, #a " == " #b, olv_va, olv_vb); \
  } while (0)

#define OLV_CHECK_NEAR(a, b, eps)                                                   \
  do {                                                                              \
    const double olv_va = static_cast<double>(a);                                   \
    const double olv_vb = static_cast<double>(b);                                   \
    if (!(std::fabs(olv_va - olv_vb) <= (eps)))                                     \
      ::olv::test::reportFailure(__FILE__, __LINE__, #a " ~= " #b, olv_va, olv_vb); \
  } while (0)

#define OLV_TEST_MAIN()                     \
  int main(int argc, char** argv) {         \
    return ::olv::test::runAll(argc, argv); \
  }
