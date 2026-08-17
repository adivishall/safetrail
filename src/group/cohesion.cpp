#include "safetrail/group/cohesion.hpp"
#include <algorithm>
#include <cmath>

namespace safetrail::group {

CohesionMonitor::CohesionMonitor(CohesionConfig cfg) : cfg_(cfg) {}

void CohesionMonitor::declare_group(DeclaredGroup g) {
  declared_.push_back(std::move(g));
  was_fragmented_.push_back(0);
  prev_stragglers_.emplace_back();
}

// GAP 4. No existing implementation models group structure -- every tourist is an
// isolated point. But the signal is SEPARATION, not absolute position: someone
// 800 m behind their party and falling further behind is the incident, and they
// may be nowhere near a zone boundary. A purely geofence-driven system is
// structurally blind to it.
void CohesionMonitor::update(const std::vector<track::Tourist>& ts, int64_t now_ms,
                             std::vector<CohesionEvent>& out) {
  const size_t n = ts.size();
  if (n == 0) return;
  dsu_ = ds::RollbackDSU(n);

  // Proximity graph. O(n^2) here; grid bucketing makes it O(n) expected and is
  // the documented next step once n justifies it.
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 1; j < n; ++j)
      if (geo::distance_m(ts[i].last_fix.pos, ts[j].last_fix.pos) <= cfg_.proximity_m)
        dsu_.unite(i, j);

  auto comps = dsu_.components();
  prev_components_.clear();
  for (const auto& c : comps) {
    std::vector<track::TouristId> ids;
    for (size_t idx : c) ids.push_back(ts[idx].id);
    prev_components_.push_back(std::move(ids));
  }

  // Compare DECLARED groups against OBSERVED components. Divergence is the alert:
  // a declared party of eight now forming components of six and two has lost two.
  for (size_t gi = 0; gi < declared_.size(); ++gi) {
    const auto& g = declared_[gi];
    if (g.members.size() < 2) continue;

    std::vector<size_t> idx;
    for (TouristId m : g.members)
      for (size_t i = 0; i < n; ++i) if (ts[i].id == m) { idx.push_back(i); break; }
    if (idx.size() < 2) continue;

    size_t distinct = 0;
    std::vector<size_t> roots;
    for (size_t i : idx) {
      const size_t r = dsu_.find(i);
      if (std::find(roots.begin(), roots.end(), r) == roots.end()) {
        roots.push_back(r); ++distinct;
      }
    }

    // Centroid of the largest component -- the party proper.
    size_t biggest_root = roots.front();
    size_t biggest = 0;
    for (size_t r : roots) {
      size_t c = 0;
      for (size_t i : idx) if (dsu_.find(i) == r) ++c;
      if (c > biggest) { biggest = c; biggest_root = r; }
    }
    double slat = 0, slon = 0;
    for (size_t i : idx) if (dsu_.find(i) == biggest_root)
      { slat += ts[i].last_fix.pos.lat; slon += ts[i].last_fix.pos.lon; }
    const geo::LatLon centroid{slat / double(biggest), slon / double(biggest)};

    // Transition only: fire when the group BECOMES fragmented, and again when it
    // recovers -- never every tick while it stays split.
    const bool fragmented = distinct > 1;
    if (fragmented && !was_fragmented_[gi]) {
      CohesionEvent e{};
      e.kind = biggest * 2 < idx.size() ? CohesionEventKind::GroupDispersed
                                        : CohesionEventKind::GroupFragmented;
      e.group = g.id; e.subject = kNoId; e.t_ms = now_ms;
      e.component_count = distinct;
      double worst = 0;
      for (size_t i : idx) if (dsu_.find(i) != biggest_root)
        worst = std::max(worst, geo::distance_m(centroid, ts[i].last_fix.pos));
      e.separation_m = worst;
      out.push_back(e);
    } else if (!fragmented && was_fragmented_[gi]) {
      CohesionEvent e{};
      e.kind = CohesionEventKind::MemberRejoined;
      e.group = g.id; e.subject = kNoId; e.t_ms = now_ms; e.component_count = 1;
      out.push_back(e);
    }
    was_fragmented_[gi] = fragmented ? 1 : 0;

    std::vector<track::TouristId> now_straggling;
    for (size_t i : idx) {
      const double sep = geo::distance_m(centroid, ts[i].last_fix.pos);
      if (sep > cfg_.straggler_m && dsu_.find(i) != biggest_root) {
        now_straggling.push_back(ts[i].id);
        const auto& prev = prev_stragglers_[gi];
        if (std::find(prev.begin(), prev.end(), ts[i].id) == prev.end()) {
          CohesionEvent e{};                       // newly straggling
          e.kind = CohesionEventKind::StragglerDetected;
          e.group = g.id; e.subject = ts[i].id; e.t_ms = now_ms;
          e.separation_m = sep; e.component_count = distinct;
          out.push_back(e);
        }
      }
    }
    prev_stragglers_[gi] = std::move(now_straggling);
  }
}

std::vector<std::vector<track::TouristId>> CohesionMonitor::observed_components() const {
  return prev_components_;
}

}  // namespace safetrail::group
