// Hashed timing wheel, against a brute-force "which timers are due" oracle.
//
// Deadlines are tick-aligned and in the future, so the wheel's quantised firing
// coincides exactly with the natural semantics "fires once now >= deadline" --
// which is what the oracle checks.
#include <algorithm>
#include <string>
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

  // ── Many timers on the SAME expiry ─────────────────────────────────────────
  //
  // They land in one slot and must all fire on that tick, none early, none held
  // over. The in-place compaction in advance() is what this exercises: a bug
  // there loses whichever entries the surviving prefix overwrote.
  {
    ds::TimerWheel<int> w(1000, 16);
    for (int i = 0; i < 500; ++i) w.schedule(i, 40 * tick);   // all identical
    w.schedule(999, 39 * tick);                                // one tick earlier
    t::ok(w.advance(39 * tick).size() == 1, "the earlier timer fires alone");
    t::ok(w.pending() == 500, "the other 500 are still pending");
    auto f = w.advance(40 * tick);
    t::ok(f.size() == 500, "all 500 share one expiry and fire together");
    std::sort(f.begin(), f.end());
    bool all = true;
    for (int i = 0; i < 500; ++i) if (f[size_t(i)] != i) all = false;
    t::ok(all, "and they are exactly the 500 scheduled, none duplicated or lost");
    t::ok(w.pending() == 0, "nothing left");
  }

  // ── Cancellation ───────────────────────────────────────────────────────────
  {
    ds::TimerWheel<int> w(1000, 8);
    for (int i = 0; i < 10; ++i) w.schedule(i, (i + 5) * tick);
    t::ok(w.pending() == 10, "10 armed");

    t::ok(w.cancel(3, 8 * tick), "cancelling an armed timer succeeds");
    t::ok(w.pending() == 9, "and pending() drops -- it is a live count, not a total");
    t::ok(!w.cancel(3, 8 * tick), "cancelling it twice reports false");
    t::ok(!w.cancel(3, 99 * tick), "cancelling with the wrong deadline reports false");
    t::ok(!w.cancel(77, 8 * tick), "cancelling an id that was never scheduled reports false");
    t::ok(w.pending() == 9, "and none of those changed the count");

    auto fired = w.advance(100 * tick);
    t::ok(fired.size() == 9, "nine fire");
    bool saw_cancelled = false;
    for (int id : fired) if (id == 3) saw_cancelled = true;
    t::ok(!saw_cancelled, "and the cancelled one is not among them");
    t::ok(w.pending() == 0, "wheel drained");
    t::ok(!w.cancel(4, 9 * tick), "cancelling a timer that has already fired reports false");
  }

  // Cancelling from a slot with several entries must remove the right one --
  // advance() compacts in place and cancel() swaps with the back, so both touch
  // slot ordering and a mix of them is where an off-by-one would show.
  {
    ds::TimerWheel<int> w(1000, 4);        // 4 slots, deadlines many revolutions out
    for (int i = 0; i < 60; ++i) w.schedule(i, (i + 1) * tick);
    for (int i = 0; i < 60; i += 3) t::ok(w.cancel(i, (i + 1) * tick),
                                          "cancelled " + std::to_string(i));
    t::ok(w.pending() == 40, "40 of 60 remain");
    auto fired = w.advance(200 * tick);
    t::ok(fired.size() == 40, "exactly those 40 fire");
    std::sort(fired.begin(), fired.end());
    bool right = true;
    size_t k = 0;
    for (int i = 0; i < 60; ++i) if (i % 3 != 0) { if (fired[k++] != i) right = false; }
    t::ok(right, "and they are exactly the ones not cancelled");
  }

  // ── advance() over a large jump ────────────────────────────────────────────
  //
  // Pins the documented semantics rather than a complexity claim: a jump of D
  // ticks steps D times and every timer inside the span fires, in tick order,
  // regardless of how many revolutions the wheel makes.
  {
    ds::TimerWheel<int> w(1000, 8);           // 8 slots
    for (int i = 1; i <= 300; ++i) w.schedule(i, int64_t(i) * tick);
    auto fired = w.advance(300 * tick);       // ~37 revolutions in one call
    t::ok(fired.size() == 300, "one 300-tick jump fires all 300 timers");
    bool ordered = true;
    for (size_t i = 1; i < fired.size(); ++i) if (fired[i - 1] > fired[i]) ordered = false;
    t::ok(ordered, "and yields them in tick order across every revolution");
    t::ok(w.pending() == 0, "with none held over");
    t::ok(w.now() == 300 * tick, "the wheel's clock landed on the target tick");
  }

  return t::report("ds/timer_wheel");
}
