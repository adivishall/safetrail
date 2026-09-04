#include "safetrail/fence/evaluator.hpp"
#include <algorithm>
#include <cmath>

namespace safetrail::fence {

Evaluator::Evaluator(const index::SpatialIndex& idx, const ZoneStore& zones,
                     EvaluatorConfig cfg)
    : index_(idx), zones_(zones), cfg_(cfg) {}

void Evaluator::reset_counters() { counters_ = Counters{}; }

const char* to_string(CandidatePolicy p) {
  switch (p) {
    case CandidatePolicy::ExactAlways: return "exact-always";
    case CandidatePolicy::NearestFirstCapped: return "nearest-first-capped";
  }
  return "?";
}

// Query radius, derived rather than guessed. We need two things from the index:
//   - zones that could CONTAIN the tourist  -> radius = position uncertainty
//   - zones they could REACH within the prediction horizon (GAP 2)
//                                           -> radius = speed x horizon
// A fixed 20 km guess returns every zone in the district and silently defeats
// the index -- the pruning ratio drops to 1x and the whole design stops paying.
// The bounds are config (see EvaluatorConfig) because they are project
// constraints, not properties of the problem.
double Evaluator::query_radius_m(const track::Tourist& t) const {
  const double reach = t.speed_mps() * cfg_.prediction_horizon_s;
  const double r = t.last_fix.accuracy_m + (reach > 0 ? reach : 0.0);
  if (r < cfg_.min_query_radius_m) return cfg_.min_query_radius_m;
  if (r > cfg_.max_query_radius_m) return cfg_.max_query_radius_m;
  return r;
}

void Evaluator::evaluate(track::Tourist& t, int64_t now_ms, std::vector<Event>& out) {
  ++counters_.evaluations;

  // ── 1. usability gate ──────────────────────────────────────────────────────
  // A fix with 200 m of uncertainty resolves to Uncertain against everything it
  // is near and produces nothing but noise. Reject it outright.
  if (!t.last_fix.usable()) { ++counters_.fixes_rejected_noise; return; }

  // ── 2/3. spatial prune ─────────────────────────────────────────────────────
  // The entire performance story. 100k zones -> a handful of candidates before
  // any O(V) geometry runs.
  candidate_buf_.clear();
  const double radius = query_radius_m(t);
  if (radius >= cfg_.max_query_radius_m) ++counters_.radius_clamped_max;
  index_.query(geo::Bbox::around(t.last_fix.pos, radius), candidate_buf_);

  if (candidate_buf_.size() > cfg_.max_candidates) {
    ++counters_.candidate_cap_hits;
    if (cfg_.candidate_policy == CandidatePolicy::NearestFirstCapped) {
      // Rank before cutting. Key: (bbox distance to the fix, then -severity, then
      // zone id) -- so what survives the cut is the near and the dangerous, and
      // the choice is deterministic rather than "whatever order the quadtree
      // walked". Still an approximation, and still counted as one.
      ranked_buf_.clear();
      ranked_buf_.reserve(candidate_buf_.size());
      for (ZoneId zid : candidate_buf_) {
        const Zone* z = zones_.get(zid);
        const double d = z ? z->shape.bbox().min_distance_m(t.last_fix.pos) : 1e12;
        const double sev = z ? double(z->severity) : 0.0;
        ranked_buf_.emplace_back(d - sev * 1e-6, zid);   // severity breaks near-ties
      }
      std::sort(ranked_buf_.begin(), ranked_buf_.end());
      counters_.candidates_dropped += candidate_buf_.size() - cfg_.max_candidates;
      candidate_buf_.clear();
      for (size_t i = 0; i < cfg_.max_candidates; ++i)
        candidate_buf_.push_back(ranked_buf_[i].second);
    }
    // ExactAlways: the cap is a diagnostic. Nothing is dropped -- a discarded
    // candidate is a missed breach, and that is not a trade this system makes by
    // default. See CandidatePolicy.
  }
  counters_.candidates_examined += candidate_buf_.size();

  // Adaptive sampling needs distance to the nearest zone, which we get for free
  // from the candidates we already have.  [GAP 7]
  double nearest_m = 1e12;

  for (ZoneId zid : candidate_buf_) {
    const Zone* z = zones_.get(zid);
    if (!z) continue;

    // GAP 3, temporal filter. A zone out of force is not merely low priority --
    // it does not exist right now, so it must not generate transitions either way.
    if (!z->validity.active_at(now_ms)) continue;

    // Bbox distance, used only to feed the adaptive sampler's "how far is the
    // nearest thing I could breach" number cheaply. It is deliberately NOT a
    // reject: the predictive path (step 8) cares about zones the tourist could
    // reach within the horizon, which are by definition further away than the
    // accuracy radius, so rejecting on that would delete exactly the alerts
    // GAP 2 exists to produce.
    const double box_d = z->shape.bbox().min_distance_m(t.last_fix.pos);
    if (box_d > 0.0) nearest_m = std::min(nearest_m, box_d);
    ++counters_.exact_tests_run;

    // ── 5. exact three-valued geometry  [GAP 1] ──────────────────────────────
    // Via geo::evaluate, which owns the Inside/Uncertain/Outside rule. This
    // module adds POLICY (hysteresis, dwell, prediction) on top of that verdict;
    // it does not get to define containment itself. It used to re-derive the
    // classification from signed_distance_m with its own comparisons -- a second
    // copy of the semantics, free to drift from the one the geometry tests pin.
    double sd = 0.0;
    const geo::Containment raw = geo::evaluate(z->shape, t.last_fix, &sd);
    nearest_m = std::min(nearest_m, std::fabs(sd));

    // ── 6. hysteresis  [GAP 8] ───────────────────────────────────────────────
    track::ZoneState& st = t.state_for(zid);
    HysteresisConfig hc = cfg_.hysteresis;
    if (z->enter_margin_m > 0) hc.enter_margin_m = z->enter_margin_m;
    if (z->exit_margin_m  > 0) hc.exit_margin_m  = z->exit_margin_m;

    const geo::Containment confirmed = st.hyst.update(raw, sd, now_ms, hc);
    if (st.hyst.suppressed_last_update()) ++counters_.flaps_suppressed;

    // ── 7. transition diff -> EVENTS ─────────────────────────────────────────
    // State is not an event. We emit only changes; reporting current containment
    // every tick is what makes existing dashboards unusable.
    if (confirmed != st.confirmed) {
      Event e{};
      e.tourist = t.id; e.zone = zid; e.t_ms = now_ms;
      e.depth_m = sd; e.accuracy_m = t.last_fix.accuracy_m; e.containment = confirmed;

      if (confirmed == geo::Containment::Inside) {
        e.kind = EventKind::ZoneEnter;
        st.entered_ms = now_ms;
        st.dwell_reported = false;
        out.push_back(e);
      } else if (st.confirmed == geo::Containment::Inside) {
        e.kind = EventKind::ZoneExit;
        out.push_back(e);
      } else if (confirmed == geo::Containment::Uncertain) {
        e.kind = EventKind::ZoneUncertain;      // the honest third state
        out.push_back(e);
      }
      st.confirmed = confirmed;
      st.approach_reported = false;
    }

    // dwell limit
    if (st.confirmed == geo::Containment::Inside && z->max_dwell_ms > 0 &&
        !st.dwell_reported && now_ms - st.entered_ms > z->max_dwell_ms) {
      Event e{};
      e.kind = EventKind::DwellExceeded; e.tourist = t.id; e.zone = zid;
      e.t_ms = now_ms; e.depth_m = sd; e.containment = st.confirmed;
      out.push_back(e);
      st.dwell_reported = true;
    }

    // ── 8. prediction  [GAP 2] ───────────────────────────────────────────────
    // Alerting on entry is already too late for a border buffer or an active
    // slope. Extrapolate and report time-to-boundary instead.
    if (st.confirmed == geo::Containment::Outside && !st.approach_reported) {
      const double v = t.speed_mps();
      if (v > 0.3 && v < 8.0 && sd > 0) {
        const double eta = sd / v;
        if (eta <= cfg_.prediction_horizon_s) {
          // Confirm the heading actually leads inside, rather than merely being
          // close: project forward and re-test.
          const geo::LatLon future = t.project(std::min(eta * 1.15,
                                                        cfg_.prediction_horizon_s));
          if (geo::contains(z->shape, future)) {
            Event e{};
            e.kind = EventKind::ZoneApproaching; e.tourist = t.id; e.zone = zid;
            e.t_ms = now_ms; e.depth_m = sd; e.eta_s = eta;
            e.accuracy_m = t.last_fix.accuracy_m;
            e.containment = geo::Containment::Outside;
            out.push_back(e);
            st.approach_reported = true;
          }
        }
      }
    }
  }

  // Feed the sampler for the next tick.
  t.sampler.should_sample(now_ms, nearest_m, t.speed_mps(), t.alert_active);
}

void Evaluator::evaluate_all(std::vector<track::Tourist>& ts, int64_t now_ms,
                             std::vector<Event>& out) {
  ++counters_.ticks;
  for (auto& t : ts) evaluate(t, now_ms, out);
}

}  // namespace safetrail::fence
