#pragma once
//
// The hot loop. Everything else in this project is scaffolding around Evaluator.
//
// Called once per fix for every tracked tourist (the simulator's default tick is
// one second), against up to 100k zones, so this is where every performance
// decision either pays off or doesn't.
//
// Structure of one evaluation, and why it is in this order:
//
//   1. usability gate    — reject fixes too noisy to mean anything      O(1)
//   2. (reserved)        — the adaptive sampler is FED at the end of each
//                          evaluation (distance to the nearest zone); it
//                          is a device-side policy for when to take the
//                          next fix and gates nothing here. Measured on
//                          its own in `make bench` §13.
//   3. spatial prune     — 100k zones → a handful of candidates   O(log n + k)
//   4. temporal prune    — of those, which are in force now?    O(1) each
//   5. exact geometry    — three-valued containment per candidate     O(k·V)
//   6. hysteresis        — filter drift-induced flapping               O(1)
//   7. transition diff   — states → EVENTS, the actual output          O(k)
//   8. prediction        — where will they be, and does it cross?      O(k·V)
//   9. reconciliation    — observe every zone with OPEN state that the
//                          index did not return this tick               O(s)
//
// Step 3 is the entire performance story. Without it this is
// O(T·Z·V) = 200 × 100,000 × 40 = 800M operations per tick, ten times a second.
// With it, roughly 200 × (17 + 3×40) ≈ 27k. See bench/results/index_scaling.csv.
//
// Steps 7 and 9 are where the bugs live.
//
// ── Why step 9 exists ────────────────────────────────────────────────────────
//
// Per-zone state only advanced for zones the index returned. A tourist inside a
// zone who then left its candidate window in one step -- a GPS gap, a vehicle
// ride, a fix after the phone was off -- was never observed against that zone
// again: no ZoneExit, state stuck at Inside forever, and a later genuine
// re-entry produced no ZoneEnter because the state already said Inside. A zone
// whose validity window closed while someone was inside had the same effect.
// Both are silent missed alerts.
//
// Now every zone with state is observed every tick:
//   - returned by the index and in force  -> the exact geometry, as before;
//   - NOT returned by the index           -> a certain Outside: the query box is
//     the conservative bound of a disc at least as large as the fix's
//     uncertainty (geo::Bbox::around, tests/geo/bbox_around_test.cpp), so a
//     zone it misses cannot contain any point the fix could be at. That
//     observation goes through HYSTERESIS like any other, so one wild fix
//     cannot force an exit on its own;
//   - out of force, or deleted            -> the state is closed at once, with a
//     ZoneExit if it was Inside. A rule change is not measurement noise.
// A state that has settled to Outside outside the window is dropped, which
// also keeps zone_states bounded by the candidate window rather than by every
// zone the tourist has ever been near.
//
// Candidates are evaluated in ZONE-ID order, not in whatever order the index
// emitted them. That makes the event stream a function of the zone set and the
// fixes alone -- tests/fence/index_independence_test.cpp runs the full
// simulation under all four indexes and asserts byte-identical output.
//
#include <cstdint>
#include <vector>

#include "safetrail/fence/hysteresis.hpp"
#include "safetrail/fence/zone.hpp"
#include "safetrail/geo/containment.hpp"
#include "safetrail/index/spatial_index.hpp"
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

// ─── Candidate-cap policy  ──────────────────────────────────────────────────
//
// What to do when the spatial index hands back more candidate zones than the
// per-tick budget. The old behaviour was `candidates.resize(cap)` -- truncate in
// whatever order the index happened to emit, which is a quadtree traversal order
// and has nothing to do with risk. In a system whose output is safety alerts, a
// dropped candidate is a MISSED BREACH: a false negative, silently, with no
// signal that it happened beyond a counter nobody reads.
//
// So truncation is no longer the default, and when it is selected it is at least
// ordered by risk rather than by tree layout.
enum class CandidatePolicy {
  // Never drop a candidate. The cap becomes a diagnostic: exceeding it increments
  // candidate_cap_hits (and means the index is misconfigured or the query radius
  // is too wide), but every candidate is still evaluated exactly. This is the
  // default because a bounded tick is not worth a missed breach at this scale --
  // 200 tourists against a district's zones, where the cap is essentially never
  // reached with a correctly built index.
  ExactAlways,

  // Hard real-time bound: evaluate at most max_candidates, chosen NEAREST FIRST
  // and then by descending severity, so what gets dropped is the far away and the
  // low-consequence rather than an arbitrary suffix. Sets `degraded` on the tick
  // and counts candidates_dropped, so the approximation is visible and
  // measurable instead of silent. Use only where a stall is worse than a miss.
  NearestFirstCapped,
};
const char* to_string(CandidatePolicy p);

// ─── Configuration ──────────────────────────────────────────────────────────
struct EvaluatorConfig {
  // How far ahead the predictive path looks. Beyond ~5 min, extrapolating a
  // walking trajectory in hill terrain is fiction.
  double prediction_horizon_s = 300.0;

  // Candidate zones considered per tourist per tick, and what to do when the
  // index returns more. See CandidatePolicy.
  size_t max_candidates = 64;
  CandidatePolicy candidate_policy = CandidatePolicy::ExactAlways;

  // ── Query radius bounds  ──────────────────────────────────────────────────
  //
  // The per-tick index query is a disc around the fix whose radius is DERIVED:
  // position uncertainty (zones that could contain them now) plus speed x horizon
  // (zones they could reach within the prediction window). These two numbers
  // clamp that derived radius, and both are project constraints rather than
  // mathematical truths -- so they are configuration, not constants buried in a
  // .cpp, and the counters report when either bites.
  //
  //   min: below ~100 m the query returns nothing useful and the fixed cost of
  //        the index lookup dominates anyway.
  //   max: an explicit, admitted horizon on the predictive search. A tourist who
  //        somehow reads as moving at 60 m/s would otherwise ask for a 5-hour
  //        reachability disc covering the whole state, which defeats the index
  //        (pruning ratio -> 1x) and is fiction anyway. Beyond this radius the
  //        engine does NOT claim to predict; it is a bounded look-ahead, not a
  //        complete reachability analysis, and docs/DESIGN_DEFENSE.md says so.
  double min_query_radius_m = 100.0;
  double max_query_radius_m = 10000.0;

  // Skip the exact test when the bbox says the point is further than this from
  // the box. Cheap pre-reject before the O(V) geometry.
  double bbox_slack_m = 0.0;

  HysteresisConfig hysteresis{};
};

// ─── Evaluator ──────────────────────────────────────────────────────────────
class Evaluator {
 public:
  Evaluator(const index::SpatialIndex& idx,
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
    uint64_t fixes_rejected_noise = 0;  // accuracy worse than usable
    uint64_t flaps_suppressed    = 0;   // ★ hysteresis value, GAP 8
    uint64_t candidate_cap_hits  = 0;   // the cap was exceeded
    uint64_t candidates_dropped  = 0;   // ...and, under NearestFirstCapped, how
                                        // many candidates were NOT evaluated.
                                        // Must be 0 under ExactAlways: that is
                                        // the invariant the false-negative test
                                        // asserts.
    uint64_t radius_clamped_max  = 0;   // derived query radius hit the ceiling
    uint64_t out_of_window_observations = 0;  // step 9: open state, zone not returned
    uint64_t states_closed       = 0;   // step 9: zone went out of force or was deleted
  };
  Counters counters() const { return counters_; }
  void reset_counters();

  // The derived index-query radius for a tourist, exposed so the benchmark and
  // the tests can check the clamping behaviour directly rather than inferring it.
  double query_radius_m(const track::Tourist& t) const;

 private:
  const index::SpatialIndex& index_;
  const ZoneStore&             zones_;
  EvaluatorConfig              cfg_;
  Counters                     counters_{};

  // Reused across ticks — allocation-free steady state.
  mutable std::vector<ZoneId> candidate_buf_;
  mutable std::vector<std::pair<double, ZoneId>> ranked_buf_;

  void emit_transition(track::Tourist& t, track::ZoneState& st, geo::Containment confirmed,
                       double sd, int64_t now_ms, std::vector<Event>& out);
  void reconcile(track::Tourist& t, uint64_t epoch, int64_t now_ms, std::vector<Event>& out);
};

}  // namespace safetrail::fence
