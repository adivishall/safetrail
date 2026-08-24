#pragma once
// Alert triage -- a priority queue over open alerts.
//
// An operator cannot look at forty simultaneous alerts at once; they need the one
// that matters most, now. "Most" is deliberately NOT just severity. As the Alert
// struct notes: a severity-3 alert open for four minutes with no responder within
// 20 km outranks a fresh severity-4 next to a police post. So the triage score
// blends three things -- severity, how long the alert has been open, and how
// isolated the tourist is (distance to the nearest responder) -- and the frontier
// is a max-heap on that score.
//
// The heap is our hand-written ds::BinaryHeap (priority_queue.hpp). That is the
// point: the binary heap exists to serve exactly this, and Dijkstra's frontier.
#include <cstdint>
#include "safetrail/alert/alert.hpp"
#include "safetrail/ds/priority_queue.hpp"

namespace safetrail::alert {

// Tunable weights for the triage score. Defaults are chosen so the documented
// case holds: sev-3 + 4 min old + 20 km isolated (score 7) beats sev-4 + fresh +
// next to help (score 4).
struct TriageWeights {
  double per_severity   = 1.0;    // per severity level (1..5)
  double per_age_minute = 0.5;    // per minute the alert has been open
  double per_isolation_km = 0.1;  // per km to the nearest responder
  double sos_boost      = 100.0;  // an SOS always floats to the top
};

// The triage score. Higher = more urgent. Pure function of the alert plus the two
// pieces of live context (current time, nearest-responder distance) that the
// static severity cannot capture.
inline double triage_score(const Alert& a, Timestamp now_ms, double nearest_responder_m,
                           const TriageWeights& w = {}) {
  const double sev = double(a.severity);
  const double age_min = (now_ms > a.raised_ms) ? double(now_ms - a.raised_ms) / 60000.0 : 0.0;
  const double iso_km  = nearest_responder_m / 1000.0;
  double s = w.per_severity * sev + w.per_age_minute * age_min + w.per_isolation_km * iso_km;
  if (a.kind == AlertKind::SosTriggered) s += w.sos_boost;
  return s;
}

// Max-heap of alerts by their stored `priority` field. Callers set `priority`
// (via triage_score) before pushing; storing it on the alert keeps heap
// comparisons O(1) and lets the operator UI show the number.
class TriageQueue {
 public:
  bool   empty() const { return heap_.empty(); }
  size_t size()  const { return heap_.size(); }

  void push(const Alert& a) { heap_.push(a); }

  const Alert& top() const { return heap_.top(); }   // the most urgent open alert
  Alert pop() { return heap_.pop(); }

 private:
  // BinaryHeap pops the element the comparator ranks "higher"; making that
  // "greater priority" turns the min-heap into the max-heap triage wants.
  struct MoreUrgent {
    bool operator()(const Alert& a, const Alert& b) const { return a.priority > b.priority; }
  };
  ds::BinaryHeap<Alert, MoreUrgent> heap_;
};

}  // namespace safetrail::alert
