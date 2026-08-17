#include "safetrail/fence/evaluator.hpp"
#include <algorithm>
#include <cmath>

namespace safetrail::fence {

Evaluator::Evaluator(const index::SpatialIndex& idx, const ZoneStore& zones,
                     EvaluatorConfig cfg)
    : index_(idx), zones_(zones), cfg_(cfg) {}

void Evaluator::reset_counters() { counters_ = Counters{}; }

// Query radius, derived rather than guessed. We need two things from the index:
//   - zones that could CONTAIN the tourist  -> radius = position uncertainty
//   - zones they could REACH within the prediction horizon (GAP 2)
//                                           -> radius = speed x horizon
// A fixed 20 km guess returns every zone in the district and silently defeats
// the index -- the pruning ratio drops to 1x and the whole design stops paying.
static double query_radius_m(const track::Tourist& t, const EvaluatorConfig& cfg) {
  const double reach = t.speed_mps() * cfg.prediction_horizon_s;
  const double r = t.last_fix.accuracy_m + (reach > 0 ? reach : 0.0);
  return r < 100.0 ? 100.0 : (r > 10000.0 ? 10000.0 : r);
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
  index_.query(geo::Bbox::around(t.last_fix.pos, query_radius_m(t, cfg_)), candidate_buf_);

  if (candidate_buf_.size() > cfg_.max_candidates) {
    ++counters_.candidate_cap_hits;
    candidate_buf_.resize(cfg_.max_candidates);
  }
  counters_.candidates_examined += candidate_buf_.size();

  // Adaptive sampling needs distance to the nearest zone, which we get for free
  // from the candidates we already have.  [GAP 7]
  double nearest_m = 1e12;

  for (ZoneId zid : candidate_buf_) {
    const Zone* z = zones_.get(zid);
    if (!z) continue;

    // Cheap bbox pre-reject before the O(V) polygon distance.
    const double box_d = z->shape.bbox().min_distance_m(t.last_fix.pos);
    if (box_d > t.last_fix.accuracy_m + cfg_.bbox_slack_m && box_d > 0.0)
      nearest_m = std::min(nearest_m, box_d);
    ++counters_.exact_tests_run;

    // ── 5. exact three-valued geometry  [GAP 1] ──────────────────────────────
    const double sd = geo::signed_distance_m(z->shape, t.last_fix.pos);
    nearest_m = std::min(nearest_m, std::fabs(sd));

    geo::Containment raw;
    if (sd < -t.last_fix.accuracy_m)      raw = geo::Containment::Inside;
    else if (sd > t.last_fix.accuracy_m)  raw = geo::Containment::Outside;
    else                                  raw = geo::Containment::Uncertain;

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
