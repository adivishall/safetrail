#pragma once
// Hashed timing wheel -- O(1) amortised timer scheduling and expiry.
//
// Alert escalation runs on deadlines: "if this SOS is not acknowledged within 5
// minutes, escalate." With thousands of live alerts, re-scanning them every tick
// is O(n) per tick and a heap is O(log n) per op. A timing wheel is O(1)
// amortised: bucket each deadline onto a circular array of slots by its expiry
// tick mod the slot count, and on each tick only that one slot's bucket is
// examined.
//
// Each entry stores its ABSOLUTE expiry tick. Many expiry ticks map to one slot
// (they differ by a whole number of revolutions); when the wheel lands on a slot
// it fires only the entries whose expiry tick has actually arrived and keeps the
// rest -- which is the rounds mechanism, expressed as a single comparison. That is
// the classic single-level Varghese & Lauck wheel; the hierarchical multi-level
// version is the extension, noted but unneeded at this scale.
//
// Hand-written: buckets are std::vectors as raw storage; no std::map / std::set /
// std::priority_queue.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace safetrail::ds {

template <typename Id>
class TimerWheel {
 public:
  TimerWheel(int64_t tick_ms, size_t slots, int64_t start_ms = 0)
      : tick_ms_(tick_ms < 1 ? 1 : tick_ms),
        slots_(slots < 1 ? 1 : slots),
        start_ms_(start_ms) {
    wheel_.resize(slots_);
  }

  size_t  pending() const { return count_; }
  int64_t now() const { return start_ms_ + cur_tick_ * tick_ms_; }

  // Schedule `id` to fire at absolute time `deadline_ms`. Quantised to the tick
  // grid: fires on the first tick boundary at or after the deadline. A deadline
  // already in the past fires on the next tick.
  void schedule(Id id, int64_t deadline_ms) {
    int64_t et = ceil_div(deadline_ms - start_ms_, tick_ms_);   // absolute expiry tick
    if (et <= cur_tick_) et = cur_tick_ + 1;                     // overdue -> next tick
    wheel_[size_t(et % int64_t(slots_))].push_back(Entry{id, et});
    ++count_;
  }

  // Advance the clock to `now_ms`, returning every timer that has expired, in the
  // order the wheel yields them. Steps tick by tick; each step touches one slot.
  std::vector<Id> advance(int64_t now_ms) {
    std::vector<Id> fired;
    const int64_t target = (now_ms - start_ms_) / tick_ms_;     // floor: whole ticks reached
    while (cur_tick_ < target) {
      ++cur_tick_;
      auto& bucket = wheel_[size_t(cur_tick_ % int64_t(slots_))];
      std::vector<Entry> keep;
      keep.reserve(bucket.size());
      for (const auto& e : bucket) {
        if (e.expiry_tick <= cur_tick_) { fired.push_back(e.id); --count_; }
        else keep.push_back(e);                                 // future revolution: hold
      }
      bucket.swap(keep);
    }
    return fired;
  }

 private:
  struct Entry { Id id; int64_t expiry_tick; };

  static int64_t ceil_div(int64_t a, int64_t b) {
    if (a <= 0) return 0;
    return (a + b - 1) / b;
  }

  int64_t tick_ms_;
  size_t  slots_;
  int64_t start_ms_;
  int64_t cur_tick_ = 0;
  size_t  count_ = 0;
  std::vector<std::vector<Entry>> wheel_;
};

}  // namespace safetrail::ds
