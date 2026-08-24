#pragma once
// Alert escalation -- deadline tracking for unacknowledged alerts.
//
// The rule the field staff actually run on: "if an SOS is not acknowledged within
// N minutes, escalate it" (bump severity, page the next tier). With thousands of
// live alerts, checking every one every tick is O(n) per tick. Instead each alert
// gets a deadline dropped into a hashed timing wheel (timer_wheel.hpp), and a
// hash table (hash_table.hpp) tracks whether each alert has since been
// acknowledged. On advance, only the wheel slot for the current tick is examined
// -- O(1) amortised -- and any deadline that fires while the alert is still
// unacknowledged escalates.
//
// This is the payoff of two of the hand-written structures: the timing wheel and
// the hash table, working together.
#include <cstdint>
#include <vector>
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/ds/timer_wheel.hpp"
#include "safetrail/types.hpp"

namespace safetrail::alert {

using safetrail::AlertId;

class EscalationTracker {
 public:
  // timeout_ms: how long an alert may sit unacknowledged before it escalates.
  EscalationTracker(int64_t timeout_ms, int64_t tick_ms, size_t slots, int64_t start_ms = 0)
      : timeout_ms_(timeout_ms), wheel_(tick_ms, slots, start_ms) {}

  // Register a newly raised alert. Its escalation deadline is raised_ms + timeout.
  void arm(AlertId id, Timestamp raised_ms) {
    status_.put(id, Armed);
    wheel_.schedule(id, raised_ms + timeout_ms_);
  }

  // The operator acknowledged the alert -- cancel its escalation. (The wheel entry
  // stays but will be ignored when it fires; the hash table is the source of truth.)
  void acknowledge(AlertId id) {
    if (status_.contains(id)) status_.put(id, Acknowledged);
  }

  // Advance the clock. Returns every alert whose deadline passed while it was
  // still unacknowledged -- these escalate. Each is escalated at most once.
  std::vector<AlertId> advance(Timestamp now_ms) {
    std::vector<AlertId> escalated;
    for (AlertId id : wheel_.advance(now_ms)) {
      const uint8_t* st = status_.get(id);
      if (st && *st == Armed) {
        status_.put(id, Escalated);
        escalated.push_back(id);
      }
    }
    return escalated;
  }

  size_t tracked() const { return wheel_.pending(); }

 private:
  enum Status : uint8_t { Armed, Acknowledged, Escalated };

  int64_t                       timeout_ms_;
  ds::TimerWheel<AlertId>       wheel_;
  ds::HashMap<AlertId, uint8_t> status_;
};

}  // namespace safetrail::alert
