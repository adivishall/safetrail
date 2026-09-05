#pragma once
// Hashed timing wheel -- Varghese & Lauck, single level.
//
// Alert escalation runs on deadlines: "if this SOS is not acknowledged within 5
// minutes, escalate." With thousands of live alerts, re-scanning them every tick
// is O(n) per tick and a heap is O(log n) per operation. A wheel buckets each
// deadline onto a circular array of slots by its expiry tick mod the slot count,
// so a tick examines exactly one slot.
//
// Each entry stores its ABSOLUTE expiry tick. Many expiry ticks map to one slot
// (they differ by a whole number of revolutions); when the wheel lands on a slot
// it fires only the entries whose expiry tick has actually arrived and keeps the
// rest -- which is the rounds mechanism, expressed as a single comparison. The
// hierarchical multi-level version is the extension, noted but unneeded here.
//
// ── Complexity, stated as the implementation actually behaves ────────────────
//
// The wheel is often described as "O(1)", and that is true of the operation
// people mean by it, but it is not true of all three, so:
//
//   schedule(id, t)     O(1). One modulo and one push_back.
//   cancel(id, t)       O(b), b = entries in that deadline's slot. Bounded by the
//                       live timer count divided by the slot count when deadlines
//                       are spread, which is the regime a wheel is sized for.
//   advance(now)        O(D + F + S), where D = ticks elapsed since the last
//                       call, F = timers fired, S = entries examined and kept
//                       because their revolution has not come round.
//                       AMORTISED over the timers themselves this is O(1) per
//                       timer -- each is examined a bounded number of times per
//                       revolution -- but it is NOT O(1) per call, and the D term
//                       is real: advance() steps tick by tick, so jumping an hour
//                       on a 1 s grid walks 3600 slots whether or not anything is
//                       in them.
//
// That D term is a deliberate trade and not a defect: stepping is what makes the
// rounds comparison correct, and the caller in this project (alert escalation)
// advances once per simulation tick, so D = 1 every time. A workload that jumps
// far ahead sparsely would want the hierarchical wheel instead, which is exactly
// the argument for the multi-level version. Saying "O(1)" flatly would hide that.
//
// ── Cancellation ────────────────────────────────────────────────────────────
//
// Eager, via cancel(). The alternative -- leave the entry and have the caller
// ignore it when it fires -- is cheaper to write and is what this had before, but
// it makes pending() a lie (it counts timers that will never mean anything) and
// lets a long-running tracker accumulate dead entries in proportion to total
// alerts rather than live ones. cancel() needs the deadline the timer was
// scheduled with, because that is what identifies its slot; the caller already
// knows it, and requiring it keeps a secondary id->slot index out of a structure
// whose whole point is that it does not need one.
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
  //
  // Scheduling the same id twice creates two independent timers, both of which
  // fire. The wheel does not deduplicate -- it has no id index to do it with, and
  // the caller that wants at-most-one semantics (EscalationTracker) already
  // tracks state per id and is the right place for the decision.
  void schedule(Id id, int64_t deadline_ms) {
    wheel_[slot_of(expiry_tick(deadline_ms))].push_back(Entry{id, expiry_tick(deadline_ms)});
    ++count_;
  }

  // Remove one timer for `id` scheduled at `deadline_ms`. Returns false if it is
  // not there -- because it already fired, was already cancelled, or was never
  // scheduled with that deadline. The deadline must be the one passed to
  // schedule(); it is what identifies the slot.
  bool cancel(Id id, int64_t deadline_ms) {
    const int64_t et = expiry_tick(deadline_ms);
    if (et <= cur_tick_) return false;                  // already fired
    auto& bucket = wheel_[slot_of(et)];
    for (size_t i = 0; i < bucket.size(); ++i)
      if (bucket[i].expiry_tick == et && bucket[i].id == id) {
        bucket[i] = bucket.back();      // order within a slot is not meaningful
        bucket.pop_back();
        --count_;
        return true;
      }
    return false;
  }

  // Advance the clock to `now_ms`, returning every timer that has expired.
  //
  // Order: ticks in chronological order, and within one tick the order the slot
  // holds -- which is insertion order except where a cancel() has swapped the
  // last entry into a hole. Callers that need a stable order across runs must not
  // rely on within-tick order after cancellations; alert::EscalationTracker sorts
  // nothing because escalation is per-alert and order-independent.
  std::vector<Id> advance(int64_t now_ms) {
    std::vector<Id> fired;
    const int64_t target = (now_ms - start_ms_) / tick_ms_;     // floor: whole ticks
    while (cur_tick_ < target) {
      ++cur_tick_;
      auto& bucket = wheel_[slot_of(cur_tick_)];
      // In place, no per-tick allocation: compact the survivors to the front.
      size_t keep = 0;
      for (size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i].expiry_tick <= cur_tick_) { fired.push_back(bucket[i].id); --count_; }
        else bucket[keep++] = bucket[i];                        // future revolution
      }
      bucket.resize(keep);
    }
    return fired;
  }

 private:
  struct Entry { Id id; int64_t expiry_tick; };

  static int64_t ceil_div(int64_t a, int64_t b) {
    if (a <= 0) return 0;
    return (a + b - 1) / b;
  }
  int64_t expiry_tick(int64_t deadline_ms) const {
    const int64_t et = ceil_div(deadline_ms - start_ms_, tick_ms_);
    return et <= cur_tick_ ? cur_tick_ + 1 : et;                // overdue -> next tick
  }
  size_t slot_of(int64_t tick) const { return size_t(tick % int64_t(slots_)); }

  int64_t tick_ms_;
  size_t  slots_;
  int64_t start_ms_;
  int64_t cur_tick_ = 0;
  size_t  count_ = 0;
  std::vector<std::vector<Entry>> wheel_;
};

}  // namespace safetrail::ds
