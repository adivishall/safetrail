#pragma once
//
// The hot loop. Everything else in this project is scaffolding around Evaluator.
//
// Called once per simulation tick (default 100 ms) for every tracked tourist.
// Runs at 200 tourists × 10 Hz against up to 100k zones, so this is where every
// performance decision either pays off or doesn't.
//
// Structure of one evaluation, and why it is in this order:
//
//   1. usability gate    — reject fixes too noisy to mean anything      O(1)
//   2. adaptive sampling — should we even have a fix right now?         O(1)
//   3. spatial prune     — 100k zones → ~3 candidates            O(log n + k)
//   4. temporal prune    — of those, which are active right now?  O(log n + k)
//   5. exact geometry    — three-valued containment per candidate     O(k·V)
//   6. hysteresis        — filter drift-induced flapping               O(1)
//   7. transition diff   — states → EVENTS, the actual output          O(k)
//   8. prediction        — where will they be, and does it cross?      O(k·V)
//
// Step 3 is the entire performance story. Without it this is
// O(T·Z·V) = 200 × 100,000 × 40 = 800M operations per tick, ten times a second.
// With it, roughly 200 × (17 + 3×40) ≈ 27k. See bench/results/index_scaling.csv.
//
// Step 7 is where the bugs live. See the note on transitions below.
//
#include <cstdint>
#include <vector>

#include "safetrail/fence/hysteresis.hpp"
#include "safetrail/fence/zone.hpp"
#include "safetrail/geo/containment.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/power/adaptive_sampler.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::fence {

// ─── Events ─────────────────────────────────────────────────────────────────
//
// The evaluator's only output. Note what is NOT here: there is no
// "tourist is currently inside zone X" event. State is not an event.
//
// Existing implementations report current containment every tick, which floods
// the operator and makes the alert rail meaningless. We diff against the previous
// tick and emit only TRANSITIONS. That single decision is the difference between
// a usable dashboard and a scrolling wall.
//
enum class EventKind {
  ZoneEnter,        // Outside/Uncertain → Inside, confirmed by hysteresis
  ZoneExit,         // Inside → Outside, confirmed
  ZoneUncertain,    // entered the ambiguous band — GAP 1, a real third state
  ZoneApproaching,  // predicted crossing within the horizon — GAP 2
  DwellExceeded,    // inside longer than the zone's dwell limit
};

struct Event {
  EventKind kind;
  track::TouristId tourist;
  ZoneId           zone;
  int64_t          t_ms;
  double           depth_m       = 0.0;   // signed distance; negative = inside
  double           accuracy_m    = 0.0;   // the fix's uncertainty, carried through
  double           eta_s         = 0.0;   // ZoneApproaching only
  geo::Containment containment   = geo::Containment::Outside;
};

// ─── Configuration ──────────────────────────────────────────────────────────
struct EvaluatorConfig {
  // How far ahead the predictive path looks. Beyond ~5 min, extrapolating a
  // walking trajectory in hill terrain is fiction.
  double prediction_horizon_s = 300.0;

  // Cap on candidate zones considered per tourist per tick. A safety valve: if
  // the index ever returns thousands of candidates we want a bounded tick, not a
  // stall. Log it when it trips — it means the index is misconfigured.
  size_t max_candidates = 64;

  // Skip the exact test when the bbox says the point is further than this from
  // the box. Cheap pre-reject before the O(V) geometry.
  double bbox_slack_m = 0.0;

  HysteresisConfig hysteresis{};
};

// ─── Evaluator ──────────────────────────────────────────────────────────────
class Evaluator {
 public:
  Evaluator(const index::VersionedIndex& idx,
            const ZoneStore& zones,
            EvaluatorConfig cfg = {});

  // Evaluate one tourist at one instant. Appends to `out`.
  //
  // `out` is caller-owned and reused across the whole tick — we do not allocate
  // per tourist. Same for the internal candidate buffer.
  void evaluate(track::Tourist& t, int64_t now_ms, std::vector<Event>& out);

  // Whole population. Straight loop over evaluate(); exists as the single place
  // to add parallelism later if the benchmark demands it.
  void evaluate_all(std::vector<track::Tourist>& ts, int64_t now_ms,
                    std::vector<Event>& out);

  struct Counters {
    uint64_t ticks               = 0;
    uint64_t evaluations         = 0;
    uint64_t candidates_examined = 0;   // post-index
    uint64_t exact_tests_run     = 0;   // post-bbox-reject
    uint64_t fixes_skipped_power = 0;   // adaptive sampler said no
    uint64_t fixes_rejected_noise = 0;  // accuracy worse than usable
    uint64_t flaps_suppressed    = 0;   // ★ hysteresis value, GAP 8
    uint64_t candidate_cap_hits  = 0;
  };
  Counters counters() const { return counters_; }
  void reset_counters();

 private:
  const index::VersionedIndex& index_;
  const ZoneStore&             zones_;
  EvaluatorConfig              cfg_;
  Counters                     counters_{};

  // Reused across ticks — allocation-free steady state.
  mutable std::vector<ZoneId> candidate_buf_;
};

}  // namespace safetrail::fence
