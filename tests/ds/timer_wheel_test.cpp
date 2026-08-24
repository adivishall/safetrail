// Hashed timing wheel, against a brute-force "which timers are due" oracle.
//
// Deadlines are tick-aligned and in the future, so the wheel's quantised firing
// coincides exactly with the natural semantics "fires once now >= deadline" --
// which is what the oracle checks.
#include <algorithm>
#include <vector>
#include "safetrail/ds/timer_wheel.hpp"
#include "../test_harness.hpp"

using namespace safetrail;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  int below(int n) { return int(next() % uint64_t(n)); }
};
}  // namespace

int main() {
  const int64_t tick = 1000;
  Rng rng(0x71E7);

  for (int trial = 0; trial < 40; ++trial) {
    const size_t slots = size_t(4 + rng.below(60));    // deliberately smaller than the horizon
    ds::TimerWheel<int> wheel(tick, slots, /*start=*/0);

    // Schedule N timers at tick-aligned future deadlines, some many revolutions out.
    const int n = 1 + rng.below(400);
    std::vector<int64_t> deadline((size_t)n);
    std::vector<char> fired_oracle(size_t(n), 0), fired_wheel(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
      const int64_t k = 1 + rng.below(300);            // 1..300 ticks ahead
      deadline[size_t(i)] = k * tick;
      wheel.schedule(i, deadline[size_t(i)]);
    }
    t::ok(wheel.pending() == size_t(n), "all timers pending after scheduling");

    // Advance in random tick-aligned jumps to well past the far horizon.
    int64_t now = 0;
    while (now < 320 * tick) {
      now += int64_t(1 + rng.below(20)) * tick;
      auto fired = wheel.advance(now);
      for (int id : fired) {
        if (fired_wheel[size_t(id)]) t::ok(false, "a timer fired twice");
        fired_wheel[size_t(id)] = 1;
      }
      // Oracle: everything with deadline <= now must have fired by now.
      for (int i = 0; i < n; ++i)
        if (deadline[size_t(i)] <= now) fired_oracle[size_t(i)] = 1;

      // Invariant at every step: wheel's fired set == oracle's due set.
      bool match = true;
      for (int i = 0; i < n; ++i) if (fired_wheel[size_t(i)] != fired_oracle[size_t(i)]) match = false;
      if (!match) { t::ok(false, "fired set diverged from oracle at now=" + std::to_string(now)); break; }
    }

    // Everything has fired exactly once and nothing is left pending.
    t::ok(wheel.pending() == 0, "no timers left pending after full sweep");
    bool all = true;
    for (int i = 0; i < n; ++i) if (!fired_wheel[size_t(i)]) all = false;
    t::ok(all, "every timer fired exactly once");
  }

  // ── Overdue deadline fires on the next tick ─────────────────────────────────
  {
    ds::TimerWheel<int> w(1000, 8, /*start=*/10'000);
    w.schedule(1, 5'000);                              // already in the past
    auto f = w.advance(11'000);                        // one tick forward
    t::ok(f.size() == 1 && f[0] == 1, "overdue timer fires on the next tick");
  }

  return t::report("ds/timer_wheel");
}
