#include "safetrail/sync/lamport.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "safetrail/util/bytes.hpp"

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

// ── On-disk format ────────────────────────────────────────────────────────────
//
// Explicitly little-endian via util/bytes.hpp -- the old version wrote native
// integers with fwrite(&v, sizeof v) while its comment claimed a fixed layout.
// This file is the one thing in the project that is DEFINED to cross machines
// (it is the offline device's outbox), so host-endian was the wrong default in
// exactly the place it mattered most.
//
//   ["SQ1\0" magic u32][device u32][count u32]
//   count x { counter u64, device u32, wall i64, len u32, len bytes }
//
// Loading parses into a temporary queue and swaps it in only on success. A device
// that lost power mid-flush produces a truncated file, and the important property
// is that reading it leaves the in-memory queue untouched rather than
// half-replaced -- these are unsynced events, so a partial load is data loss.
namespace {
constexpr uint32_t kQueueMagic = 0x00315153;      // "SQ1\0"
// A single event payload is a serialised fence::Event, tens of bytes. The cap is
// a sanity bound on a corrupt length field, not a design limit; it exists so a
// garbage length is a parse error instead of an allocation the size of the field.
constexpr uint32_t kMaxPayloadBytes = 1u << 20;
}  // namespace

bool OfflineQueue::flush_to_disk(const std::string& path) const {
  std::vector<uint8_t> buf;
  util::put_u32(buf, kQueueMagic);
  util::put_u32(buf, uint32_t(clock_.device));
  util::put_u32(buf, uint32_t(queue_.size()));
  for (const auto& e : queue_) {
    util::put_u64(buf, e.stamp.counter);
    util::put_u32(buf, uint32_t(e.stamp.device));
    util::put_i64(buf, e.device_wall_ms);
    util::put_u32(buf, uint32_t(e.payload.size()));
    buf.insert(buf.end(), e.payload.begin(), e.payload.end());
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  if (!buf.empty())
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
  return bool(f);
}

bool OfflineQueue::load_from_disk(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string blob = ss.str();
  util::Reader r(reinterpret_cast<const uint8_t*>(blob.data()), blob.size());

  uint32_t magic = 0, dev = 0, count = 0;
  if (!r.u32(&magic) || magic != kQueueMagic) return false;
  if (!r.u32(&dev) || !r.u32(&count)) return false;

  std::vector<StampedEvent> loaded;
  uint64_t high_counter = 0;
  for (uint32_t i = 0; i < count; ++i) {
    StampedEvent e;
    uint32_t sdev = 0, len = 0;
    if (!r.u64(&e.stamp.counter) || !r.u32(&sdev) || !r.i64(&e.device_wall_ms) ||
        !r.u32(&len))
      return false;
    e.stamp.device = DeviceId(sdev);
    if (len > kMaxPayloadBytes) return false;      // refuse before allocating
    if (len && !r.bytes(len, &e.payload)) return false;
    high_counter = std::max(high_counter, e.stamp.counter);
    loaded.push_back(std::move(e));
  }
  if (!r.at_end()) return false;                   // trailing garbage

  // Commit only now: everything above touches locals only.
  clock_.device = DeviceId(dev);
  clock_.counter = std::max(clock_.counter, high_counter);
  queue_ = std::move(loaded);
  return true;
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
