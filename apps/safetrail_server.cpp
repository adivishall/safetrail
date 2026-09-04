// safetrail_server -- NOT IMPLEMENTED, and deliberately so.
//
// This project has no server. The engine is a C++ program that runs a scenario
// and writes one self-contained HTML replay (apps/safetrail_headless.cpp); the
// "deployment" is that static file. A live server was on an early roadmap and
// was dropped, because a network service is not what a data-structures course
// project is being graded on and pretending otherwise would misrepresent the
// architecture. See docs/ARCHITECTURE.md.
//
// The file survives only so that a stale reference to the target fails loudly
// here rather than mysteriously in a build system.
#include <cstdio>

int main(int, char**) {
  std::fprintf(stderr,
               "safetrail has no server: run build/safetrail_headless "
               "(see docs/ARCHITECTURE.md)\n");
  return 1;
}
