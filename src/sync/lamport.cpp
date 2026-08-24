#include "safetrail/sync/lamport.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace safetrail::sync {

// ── OfflineQueue ──────────────────────────────────────────────────────────────
OfflineQueue::OfflineQueue(DeviceId self) { clock_.device = self; }

uint64_t OfflineQueue::append(std::vector<uint8_t> payload, int64_t wall_ms) {
  const uint64_t c = clock_.tick();            // local event advances the logical clock
  StampedEvent e;
  e.stamp = LamportClock{c, clock_.device};
  e.device_wall_ms = wall_ms;
  e.payload = std::move(payload);
  queue_.push_back(std::move(e));
  return c;
}

size_t OfflineQueue::pending() const { return queue_.size(); }

std::vector<StampedEvent> OfflineQueue::drain() {
  std::vector<StampedEvent> out = std::move(queue_);
  queue_.clear();
  return out;
}

// Binary on-disk format (little-endian host assumed, same trade the geohash blob
// makes): [device u32][count u32] then count x
//   [counter u64][device u32][wall i64][len u32][len bytes]
bool OfflineQueue::flush_to_disk(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto w = [&](const void* p, size_t n) { return std::fwrite(p, 1, n, f) == n; };
  bool ok = true;
  const uint32_t dev = clock_.device, count = uint32_t(queue_.size());
  ok = ok && w(&dev, sizeof(dev)) && w(&count, sizeof(count));
  for (const auto& e : queue_) {
    const uint32_t len = uint32_t(e.payload.size());
    ok = ok && w(&e.stamp.counter, sizeof(e.stamp.counter)) &&
               w(&e.stamp.device, sizeof(e.stamp.device)) &&
               w(&e.device_wall_ms, sizeof(e.device_wall_ms)) &&
               w(&len, sizeof(len)) &&
               (len == 0 || w(e.payload.data(), len));
  }
  std::fclose(f);
  return ok;
}

bool OfflineQueue::load_from_disk(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  auto r = [&](void* p, size_t n) { return std::fread(p, 1, n, f) == n; };
  uint32_t dev = 0, count = 0;
  bool ok = r(&dev, sizeof(dev)) && r(&count, sizeof(count));
  if (ok) {
    clock_.device = dev;
    queue_.clear();
    for (uint32_t i = 0; i < count && ok; ++i) {
      StampedEvent e;
      uint32_t len = 0;
      ok = r(&e.stamp.counter, sizeof(e.stamp.counter)) &&
           r(&e.stamp.device, sizeof(e.stamp.device)) &&
           r(&e.device_wall_ms, sizeof(e.device_wall_ms)) &&
           r(&len, sizeof(len));
      if (ok && len) { e.payload.resize(len); ok = r(e.payload.data(), len); }
      if (ok) {
        if (e.stamp.counter > clock_.counter) clock_.counter = e.stamp.counter;
        queue_.push_back(std::move(e));
      }
    }
  }
  std::fclose(f);
  return ok;
}

// ── Reconciler ────────────────────────────────────────────────────────────────
size_t Reconciler::merge(const std::vector<StampedEvent>& batch) {
  size_t admitted = 0;
  for (const auto& e : batch) {
    // Idempotency: (device, counter) uniquely identifies an event. A replayed
    // batch -- which flaky links guarantee -- is rejected event for event.
    bool dup = false;
    for (const auto& t : timeline_)
      if (t.stamp.counter == e.stamp.counter && t.stamp.device == e.stamp.device) { dup = true; break; }
    if (dup) { ++stats_.duplicates_rejected; continue; }

    clock_.observe(e.stamp.counter);   // server logical clock absorbs the remote counter
    timeline_.push_back(e);
    ++admitted;
    ++stats_.merged;
  }

  // Deterministic total order: Lamport (counter, then device). stable_sort so a
  // re-merge of the same set is byte-identical regardless of batch arrival order.
  std::stable_sort(timeline_.begin(), timeline_.end(),
                   [](const StampedEvent& a, const StampedEvent& b) { return a.stamp.before(b.stamp); });

  // Skew analysis. Walk the causal order; the wall clock *should* be monotone. A
  // device whose clock is behind shows up as a backwards jump -- that is exactly
  // the untrusted-wall-clock problem the Lamport order exists to survive.
  stats_.reordered = 0;
  stats_.max_skew_ms = 0;
  int64_t running_max_wall = INT64_MIN;
  for (auto& e : timeline_) {
    if (running_max_wall != INT64_MIN && e.device_wall_ms < running_max_wall) {
      ++stats_.reordered;                                  // causal-next but wall-earlier
      const int64_t skew = running_max_wall - e.device_wall_ms;
      e.clock_skew_ms = -skew;                             // negative = device is behind
      if (skew > stats_.max_skew_ms) stats_.max_skew_ms = skew;
    } else {
      e.clock_skew_ms = 0;
    }
    running_max_wall = std::max(running_max_wall, e.device_wall_ms);
  }
  return admitted;
}

}  // namespace safetrail::sync
