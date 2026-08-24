// Lamport clocks + offline reconciliation.  [GAP 6]
//
// The claims that matter for the offline story:
//   - the logical clock respects causality (observe advances past what it saw)
//   - merge is idempotent: replaying a batch admits nothing new (flaky links)
//   - the reconciled timeline is deterministic regardless of batch arrival order
//   - ordering is by Lamport stamp, never by the untrusted wall clock -- so a
//     device hours off still lands in the causally correct place
//   - the offline queue survives a disk round trip
#include <string>
#include <vector>
#include "safetrail/sync/lamport.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::sync;

int main() {
  // ── Logical clock mechanics ────────────────────────────────────────────────
  {
    LamportClock c; c.device = 1;
    t::ok(c.tick() == 1 && c.tick() == 2, "tick increments");
    c.observe(10);
    t::ok(c.counter == 11, "observe jumps past the remote counter (max+1)");
    c.observe(5);
    t::ok(c.counter == 12, "observe of an older counter still advances by 1");

    LamportClock a{5, 1}, b{5, 2};
    t::ok(a.before(b) && !b.before(a), "equal counters: device id breaks the tie");
    LamportClock e{4, 9}, f{5, 0};
    t::ok(e.before(f), "lower counter is before, regardless of device");
  }

  // ── OfflineQueue append / drain / disk round trip ──────────────────────────
  {
    OfflineQueue q(7);
    q.append({1, 2, 3}, 1'000);
    q.append({4, 5},    2'000);
    t::ok(q.pending() == 2, "two events pending");

    const std::string path = "/tmp/safetrail_lamport_test.bin";
    t::ok(q.flush_to_disk(path), "flush to disk");
    OfflineQueue loaded(0);
    t::ok(loaded.load_from_disk(path), "load from disk");
    t::ok(loaded.pending() == 2, "event count survives the round trip");
    auto drained = loaded.drain();
    t::ok(drained.size() == 2 && drained[0].payload == std::vector<uint8_t>({1, 2, 3}),
          "payload bytes survive the round trip");
    t::ok(loaded.pending() == 0, "drain empties the queue");
  }

  // ── Idempotent merge ───────────────────────────────────────────────────────
  {
    std::vector<StampedEvent> batch;
    for (uint64_t i = 1; i <= 5; ++i) {
      StampedEvent e; e.stamp = {i, 1}; e.device_wall_ms = int64_t(i * 1000);
      batch.push_back(e);
    }
    Reconciler rec;
    t::ok(rec.merge(batch) == 5, "first merge admits all 5");
    t::ok(rec.merge(batch) == 0, "replay admits 0 (idempotent)");
    t::ok(rec.timeline().size() == 5, "timeline has no duplicates");
    t::ok(rec.stats().duplicates_rejected == 5, "duplicates counted");
  }

  // ── Deterministic order independent of batch arrival order ──────────────────
  {
    // Three devices, interleaved Lamport counters.
    auto mk = [](uint64_t ctr, DeviceId dev, int64_t wall) {
      StampedEvent e; e.stamp = {ctr, dev}; e.device_wall_ms = wall; return e;
    };
    std::vector<StampedEvent> A = {mk(1, 1, 100), mk(3, 1, 300), mk(5, 2, 500)};
    std::vector<StampedEvent> B = {mk(2, 2, 200), mk(4, 3, 400), mk(6, 1, 600)};

    Reconciler r1; r1.merge(A); r1.merge(B);
    Reconciler r2; r2.merge(B); r2.merge(A);   // opposite arrival order
    bool same = r1.timeline().size() == r2.timeline().size();
    for (size_t i = 0; same && i < r1.timeline().size(); ++i)
      same = r1.timeline()[i].stamp.counter == r2.timeline()[i].stamp.counter &&
             r1.timeline()[i].stamp.device  == r2.timeline()[i].stamp.device;
    t::ok(same, "timeline is identical regardless of batch order");
    // And it is sorted by Lamport stamp.
    bool sorted = true;
    for (size_t i = 1; i < r1.timeline().size(); ++i)
      if (r1.timeline()[i].stamp.before(r1.timeline()[i - 1].stamp)) sorted = false;
    t::ok(sorted, "timeline is in Lamport order");
  }

  // ── A device with a badly wrong wall clock still orders causally ────────────
  {
    auto mk = [](uint64_t ctr, DeviceId dev, int64_t wall) {
      StampedEvent e; e.stamp = {ctr, dev}; e.device_wall_ms = wall; return e;
    };
    // Event 2 (causally after event 1) has a wall clock 3 hours BEHIND event 1.
    std::vector<StampedEvent> batch = {
      mk(1, 1, 10'000'000),          // device 1, correct-ish clock
      mk(2, 2,  -800'000),           // device 2, ~3h behind
      mk(3, 1, 10'001'000),
    };
    Reconciler rec;
    rec.merge(batch);
    // Causal order preserved despite the wall clock lying.
    t::ok(rec.timeline()[0].stamp.counter == 1 &&
          rec.timeline()[1].stamp.counter == 2 &&
          rec.timeline()[2].stamp.counter == 3,
          "Lamport order holds even with a 3h-skewed device");
    t::ok(rec.stats().max_skew_ms > 10'000'000, "large clock skew is detected and reported");
    t::ok(rec.stats().reordered >= 1, "wall-clock inversion counted");
  }

  return t::report("sync/lamport");
}
