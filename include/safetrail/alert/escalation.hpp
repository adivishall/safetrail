#pragma once
// Alert escalation -- deadline tracking for unacknowledged alerts.
//
// The rule the field staff actually run on: "if an SOS is not acknowledged within
// N minutes, escalate it" (bump severity, page the next tier). With thousands of
// live alerts, checking every one every tick is O(n) per tick. Instead each alert
// gets a deadline dropped into a hashed timing wheel (timer_wheel.hpp), and a
// hash table (hash_table.hpp) tracks each alert's state and the deadline it was
// given. Advancing one tick examines exactly one wheel slot, and any deadline that
// fires while the alert is still unacknowledged escalates.
//
// Cost, precisely -- the wheel's headline "O(1)" is true of scheduling and is not
// true of advancing, and this caller is the reason the distinction is affordable:
// escalation advances once per simulation tick, so the Δ term in
// advance()'s O(Δticks + fired + held) is 1 every time and the whole thing is O(1)
// per tick plus the alerts that actually fired. A caller that jumped an hour
// forward in one step would pay for every tick in between; see the note in
// timer_wheel.hpp, and the hierarchical wheel is the answer if that ever becomes
// the workload.
//
// This is the payoff of two of the hand-written structures: the timing wheel and
// the hash table, working together -- and the division of labour between them is
// the interesting part. The wheel answers "what is due now"; the hash table
// answers "what is this alert's state, and what deadline did I give it". Neither
// can answer the other's question, which is why both are here.
//
// Acknowledgement cancels the timer EAGERLY (wheel_.cancel), which needs the
// deadline -- hence storing it next to the status. The lazy alternative, letting
// the timer fire and ignoring it, was what this did before: simpler, and wrong in
// a way that only shows up over a long shift. Every acknowledged alert left a
// corpse in the wheel, so pending() counted alerts nobody was waiting on and the
// wheel's memory tracked TOTAL alerts raised rather than the live ones -- the
// same unbounded-growth-under-churn failure the hash table's tombstone policy
// exists to prevent, one module over. tracked() is now a true count.
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
  // Re-arming an alert that is already tracked is a no-op rather than a second
  // timer: the wheel does not deduplicate, so at-most-one is enforced here.
  void arm(AlertId id, Timestamp raised_ms) {
    if (status_.contains(id)) return;
    const Timestamp deadline = raised_ms + timeout_ms_;
    status_.put(id, Record{Armed, deadline});
    wheel_.schedule(id, deadline);
  }

  // The operator acknowledged the alert -- cancel its escalation, removing the
  // timer rather than leaving it to fire into a check that discards it.
  void acknowledge(AlertId id) {
    Record* r = status_.get(id);
    if (!r || r->state != Armed) return;
    r->state = Acknowledged;
    wheel_.cancel(id, r->deadline);
  }

  // Advance the clock. Returns every alert whose deadline passed while it was
  // still unacknowledged -- these escalate. Each is escalated at most once.
  std::vector<AlertId> advance(Timestamp now_ms) {
    std::vector<AlertId> escalated;
    for (AlertId id : wheel_.advance(now_ms)) {
      Record* r = status_.get(id);
      // The Armed check is still here even though cancellation is now eager: a
      // timer can fire in the same advance() call that would have cancelled it,
      // and an id the tracker never armed can only be a caller error. Cheap, and
      // it keeps "escalated at most once" true by construction rather than by
      // argument.
      if (r && r->state == Armed) {
        r->state = Escalated;
        escalated.push_back(id);
      }
    }
    return escalated;
  }

  // Live escalation deadlines: alerts still armed and not yet fired. Acknowledged
  // alerts are gone from the wheel, so this no longer over-reports.
  size_t tracked() const { return wheel_.pending(); }

  // Alerts the tracker knows about at all, in any state. Distinct from tracked()
  // on purpose -- conflating "still waiting on this" with "have ever seen this"
  // is what made the old tracked() misleading.
  size_t known() const { return status_.size(); }

 private:
  enum Status : uint8_t { Armed, Acknowledged, Escalated };
  struct Record { Status state; Timestamp deadline; };

  int64_t                      timeout_ms_;
  ds::TimerWheel<AlertId>      wheel_;
  ds::HashMap<AlertId, Record> status_;
};

}  // namespace safetrail::alert
