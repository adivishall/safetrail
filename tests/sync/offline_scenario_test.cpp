// End-to-end offline scenario [GAP 6]: two devices go offline for hours, keep
// logging events locally (surviving a process restart via disk), then reconnect
// and reconcile into one correct, causally-ordered timeline -- despite wall clocks
// that disagree by hours.
#include <string>
#include <vector>
#include "safetrail/sync/lamport.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::sync;

namespace {
std::vector<uint8_t> payload(const std::string& s) { return {s.begin(), s.end()}; }
}  // namespace

int main() {
  // Device 1: correct-ish clock. Device 2: 3 hours behind. Both offline ~2h.
  const int64_t hour = 3'600'000;

  OfflineQueue dev1(1);
  dev1.append(payload("enter zone A"), 10 * hour);
  dev1.append(payload("dwell exceeded"), 10 * hour + 600'000);
  dev1.append(payload("exit zone A"),   11 * hour);

  OfflineQueue dev2(2);
  dev2.append(payload("SOS"),           7 * hour);   // wall clock 3h behind dev1
  dev2.append(payload("acknowledged"),  7 * hour + 120'000);

  // Process restart on device 1: flush to disk, reload into a fresh queue.
  const std::string path = "/tmp/safetrail_offline_scenario.bin";
  t::ok(dev1.flush_to_disk(path), "device 1 persists its queue to disk");
  OfflineQueue dev1_reloaded(0);
  t::ok(dev1_reloaded.load_from_disk(path), "device 1 recovers its queue after restart");
  t::ok(dev1_reloaded.pending() == 3, "no events lost across the restart");

  // Reconnect: both devices upload; the server reconciles.
  Reconciler server;
  const size_t a = server.merge(dev1_reloaded.drain());
  const size_t b = server.merge(dev2.drain());
  t::ok(a + b == 5, "all five events admitted");

  // A flaky link re-delivers device 2's batch -- must be idempotent.
  OfflineQueue dev2_replay(2);
  dev2_replay.append(payload("SOS"), 7 * hour);
  dev2_replay.append(payload("acknowledged"), 7 * hour + 120'000);
  // Rebuild identical stamps by replaying is not possible (new counters), so
  // instead re-merge the server's own timeline -- the true duplicate case.
  const size_t dup = server.merge(server.timeline());
  t::ok(dup == 0, "re-merging the timeline admits nothing (idempotent)");
  t::ok(server.timeline().size() == 5, "timeline still has exactly five events");

  // The timeline is in Lamport order (causality), NOT wall-clock order.
  bool lamport_ordered = true;
  for (size_t i = 1; i < server.timeline().size(); ++i)
    if (server.timeline()[i].stamp.before(server.timeline()[i - 1].stamp)) lamport_ordered = false;
  t::ok(lamport_ordered, "reconciled timeline respects causal (Lamport) order");

  // Sorting by wall clock would be WRONG here (device 2 is 3h behind); confirm the
  // reconciler noticed the skew rather than trusting the clock.
  t::ok(server.stats().max_skew_ms >= 3 * hour - 600'000,
        "3-hour clock skew detected and reported");
  t::ok(server.stats().reordered >= 1, "at least one wall-clock inversion flagged");

  return t::report("sync/offline_scenario");
}
