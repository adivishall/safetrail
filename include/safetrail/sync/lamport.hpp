#pragma once
//
// Logical clocks for offline event reconciliation.  [GAP 6]
//
// The offline story: ship a serialised spatial index to the device, evaluate
// zones locally with no server, queue events, reconcile on reconnect.
//
// Reconciliation is the hard part, and it is a genuine distributed-systems
// problem hiding inside a geofencing project. Events arrive from several devices
// that were disconnected for hours. Their wall clocks disagree — phone clocks
// drift, timezones get set wrong, users change them manually. Ordering incident
// events by device wall clock produces a timeline that is sometimes simply false,
// and this log is meant to be evidence.
//
// Lamport timestamps give a consistent partial order that respects causality
// without trusting any clock:
//
//   local event         →  counter++
//   send / merge        →  counter = max(local, received) + 1
//   compare             →  (counter, device_id) lexicographic  ← total order,
//                          deterministic, tie-broken by device
//
// Wall clock is retained alongside, for humans, clearly labelled as untrusted.
//
#include <cstdint>
#include <string>
#include <vector>
#include "safetrail/types.hpp"

namespace safetrail::sync {

using safetrail::DeviceId;

struct LamportClock {
  uint64_t counter = 0;
  DeviceId device  = 0;

  uint64_t tick() { return ++counter; }
  void observe(uint64_t remote) { counter = (remote > counter ? remote : counter) + 1; }

  // Total order: counter first, device id breaks ties. Determinism matters — two
  // merges of the same event set must produce byte-identical ordering.
  bool before(const LamportClock& o) const {
    return counter != o.counter ? counter < o.counter : device < o.device;
  }
};

// An event as it crosses the offline boundary.
struct StampedEvent {
  LamportClock stamp;
  int64_t      device_wall_ms;   // UNTRUSTED — display only, never sort on this
  std::vector<uint8_t> payload;  // serialised fence::Event

  // Filled during merge: how far the device clock was from server time. Worth
  // surfacing — in testing you will find devices hours off, which is precisely
  // why the Lamport ordering exists.
  int64_t clock_skew_ms = 0;
};

// Append-only local queue. Survives process restart; the device may be offline
// for days.
class OfflineQueue {
 public:
  explicit OfflineQueue(DeviceId self);

  uint64_t append(std::vector<uint8_t> payload, int64_t wall_ms);
  size_t   pending() const;
  bool     flush_to_disk(const std::string& path) const;
  bool     load_from_disk(const std::string& path);
  std::vector<StampedEvent> drain();

 private:
  LamportClock clock_;
  std::vector<StampedEvent> queue_;
};

// Server side. Merges batches from many devices into one ordered timeline.
class Reconciler {
 public:
  // Idempotent: replaying the same batch must not duplicate events, because
  // flaky links mean it will happen. Returns count actually admitted.
  size_t merge(const std::vector<StampedEvent>& batch);

  // The reconciled timeline, in Lamport order.
  const std::vector<StampedEvent>& timeline() const { return timeline_; }

  struct Stats {
    uint64_t merged = 0, duplicates_rejected = 0, reordered = 0;
    int64_t  max_skew_ms = 0;
  };
  Stats stats() const { return stats_; }

 private:
  LamportClock clock_;
  std::vector<StampedEvent> timeline_;
  Stats stats_{};
};

}  // namespace safetrail::sync
