// Alert triage (binary heap) and escalation (timer wheel + hash table).
#include <algorithm>
#include <vector>
#include "safetrail/alert/escalation.hpp"
#include "safetrail/alert/triage.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::alert;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  int below(int n) { return int(next() % uint64_t(n)); }
};

Alert mk(AlertId id, uint8_t sev, Timestamp raised, AlertKind kind = AlertKind::ZoneBreach) {
  Alert a; a.id = id; a.severity = sev; a.raised_ms = raised; a.kind = kind; return a;
}
}  // namespace

int main() {
  // ── The documented triage case: age + isolation beat raw severity ──────────
  {
    const Timestamp now = 5 * 60'000;   // 5 minutes in
    // sev-3, opened 4 min ago, nearest responder 20 km away.
    Alert aged = mk(1, 3, now - 4 * 60'000);
    aged.priority = triage_score(aged, now, 20'000.0);
    // sev-4, fresh, responder 200 m away.
    Alert fresh = mk(2, 4, now);
    fresh.priority = triage_score(fresh, now, 200.0);

    t::ok(aged.priority > fresh.priority, "aged+isolated sev-3 outranks fresh sev-4 next to help");

    TriageQueue q;
    q.push(fresh);
    q.push(aged);
    t::ok(q.top().id == 1, "heap surfaces the higher-priority alert first");
  }

  // ── SOS always floats to the top ───────────────────────────────────────────
  {
    const Timestamp now = 10 * 60'000;
    Alert sos = mk(9, 1, now, AlertKind::SosTriggered);          // lowest severity...
    sos.priority = triage_score(sos, now, 0.0);
    Alert big = mk(8, 5, now - 9 * 60'000);                      // ...vs an old sev-5
    big.priority = triage_score(big, now, 30'000.0);
    TriageQueue q; q.push(big); q.push(sos);
    t::ok(q.top().id == 9, "SOS outranks everything");
  }

  // ── Heap pops in non-increasing priority order (vs a sorted oracle) ────────
  {
    Rng rng(0x71A6E);
    TriageQueue q;
    std::vector<double> pr;
    for (int i = 0; i < 500; ++i) {
      Alert a = mk(AlertId(i), uint8_t(1 + rng.below(5)), int64_t(rng.below(600000)));
      a.priority = triage_score(a, 600000, double(rng.below(40000)));
      pr.push_back(a.priority);
      q.push(a);
    }
    std::sort(pr.rbegin(), pr.rend());   // descending
    bool ordered = true;
    for (size_t i = 0; i < pr.size(); ++i) {
      if (std::fabs(q.pop().priority - pr[i]) > 1e-9) ordered = false;
    }
    t::ok(ordered, "triage pops in non-increasing priority order");
  }

  // ── Escalation: unacknowledged deadlines fire; acknowledged ones do not ────
  {
    // 5-minute timeout, 1-second ticks.
    EscalationTracker esc(5 * 60'000, 1000, 64, /*start=*/0);
    esc.arm(1, 0);          // deadline 5:00
    esc.arm(2, 0);          // deadline 5:00, will be acknowledged
    esc.arm(3, 60'000);     // deadline 6:00

    esc.acknowledge(2);

    auto e1 = esc.advance(4 * 60'000);   // 4:00 -- nothing due yet
    t::ok(e1.empty(), "nothing escalates before the deadline");

    auto e2 = esc.advance(5 * 60'000);   // 5:00 -- alerts 1 and 2 due; 2 acknowledged
    t::ok(e2.size() == 1 && e2[0] == 1, "only the unacknowledged alert escalates");

    auto e3 = esc.advance(7 * 60'000);   // 7:00 -- alert 3 due
    t::ok(e3.size() == 1 && e3[0] == 3, "later deadline escalates at its time");

    auto e4 = esc.advance(9 * 60'000);   // nothing left
    t::ok(e4.empty(), "each alert escalates at most once");
  }

  // ── Acknowledgement removes the timer, it does not just ignore it ──────────
  //
  // The observable difference between eager and lazy cancellation: tracked()
  // counts LIVE deadlines. Under the old lazy scheme an acknowledged alert stayed
  // in the wheel until its deadline passed, so a busy shift's wheel grew with
  // total alerts raised rather than with the ones anybody was still waiting on.
  {
    EscalationTracker esc(5 * 60'000, 1000, 64, 0);
    for (AlertId i = 1; i <= 100; ++i) esc.arm(i, 0);
    t::ok(esc.tracked() == 100, "100 live deadlines");
    t::ok(esc.known() == 100, "and 100 alerts known");

    for (AlertId i = 1; i <= 90; ++i) esc.acknowledge(i);
    t::ok(esc.tracked() == 10, "acknowledging 90 leaves 10 live deadlines");
    t::ok(esc.known() == 100, "while all 100 are still known -- the two differ on purpose");

    auto fired = esc.advance(5 * 60'000);
    t::ok(fired.size() == 10, "exactly the 10 unacknowledged escalate");
    t::ok(esc.tracked() == 0, "and the wheel is empty afterwards");

    // Acknowledging after escalation must not resurrect or double-count anything.
    esc.acknowledge(95);
    t::ok(esc.tracked() == 0, "acknowledging an already-escalated alert is a no-op");
    t::ok(esc.advance(60 * 60'000).empty(), "and nothing fires an hour later");
  }

  // Re-arming an already-tracked alert must not create a second timer -- the
  // wheel itself does not deduplicate, so this is the tracker's invariant.
  {
    EscalationTracker esc(60'000, 1000, 32, 0);
    esc.arm(7, 0);
    esc.arm(7, 30'000);
    esc.arm(7, 45'000);
    t::ok(esc.tracked() == 1, "re-arming does not stack timers");
    auto f = esc.advance(10 * 60'000);
    t::ok(f.size() == 1 && f[0] == 7, "and the alert escalates exactly once");
  }

  // ── Escalation vs a brute-force oracle over random arm/ack/advance ─────────
  {
    Rng rng(0xE5CA1);
    for (int trial = 0; trial < 30; ++trial) {
      EscalationTracker esc(30'000, 1000, size_t(8 + rng.below(40)), 0);
      const int n = 1 + rng.below(200);
      std::vector<int64_t> deadline((size_t)n);
      std::vector<char> acked((size_t)n, 0), escalated_oracle((size_t)n, 0), escalated_real((size_t)n, 0);
      for (int i = 0; i < n; ++i) {
        const int64_t raised = int64_t(rng.below(60)) * 1000;
        deadline[size_t(i)] = raised + 30'000;
        esc.arm(AlertId(i), raised);
      }
      // Acknowledge a random subset up front.
      for (int i = 0; i < n; ++i)
        if (rng.below(3) == 0) { esc.acknowledge(AlertId(i)); acked[size_t(i)] = 1; }

      int64_t now = 0;
      while (now < 120'000) {
        now += int64_t(1 + rng.below(15)) * 1000;
        for (AlertId id : esc.advance(now)) escalated_real[size_t(id)] = 1;
        for (int i = 0; i < n; ++i)
          if (!acked[size_t(i)] && deadline[size_t(i)] <= now) escalated_oracle[size_t(i)] = 1;
      }
      bool match = true;
      for (int i = 0; i < n; ++i) if (escalated_real[size_t(i)] != escalated_oracle[size_t(i)]) match = false;
      t::ok(match, "escalation == oracle (trial " + std::to_string(trial) + ")");
    }
  }

  return t::report("alert/triage_escalation");
}
