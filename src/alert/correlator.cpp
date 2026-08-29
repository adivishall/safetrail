#include "safetrail/alert/correlator.hpp"
#include "safetrail/ds/dynamic_connectivity.hpp"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdlib>

namespace safetrail::alert {

Correlator::Correlator(CorrelationConfig cfg) : cfg_(cfg) {}

// GAP 5. A landslide with forty tourists produces forty alert cards in every
// existing dashboard, and the operator becomes the bottleneck. Forty alerts
// clustered in space and time are ONE incident affecting forty people.
//
// Anisotropic metric: 200 m and 60 s are comparable "distances" for our purposes,
// so time is scaled onto the spatial axis rather than treated as a third
// dimension. Candidate pairs via O(n^2) over the fresh batch (which is small),
// then union-find to merge -- the same structure as group cohesion, for a
// completely unrelated reason.
// Fold a fresh cluster into an existing incident: append its alerts, add any new
// people, extend the time span, and slide the centroid by a running average
// weighted by how many alerts each side already carries.
static void merge_into(alert::Incident& inc, const std::vector<size_t>& comp,
                       const std::vector<Alert>& fresh) {
  const double w_old = double(inc.alerts.size());
  double slat = inc.centroid.lat * w_old, slon = inc.centroid.lon * w_old;
  for (size_t idx : comp) {
    const Alert& a = fresh[idx];
    inc.alerts.push_back(a.id);
    bool seen = false;
    for (auto t : inc.affected) if (t == a.tourist) { seen = true; break; }
    if (!seen) inc.affected.push_back(a.tourist);
    inc.opened_ms = std::min(inc.opened_ms, a.raised_ms);
    inc.last_update_ms = std::max(inc.last_update_ms, a.raised_ms);
    inc.max_severity = std::max(inc.max_severity, a.severity);
    slat += a.position.lat; slon += a.position.lon;
  }
  const double w = double(inc.alerts.size());
  inc.centroid = {slat / w, slon / w};
  for (size_t idx : comp)
    inc.radius_m = std::max(inc.radius_m,
                            geo::distance_m(inc.centroid, fresh[idx].position));
}

std::vector<IncidentId> Correlator::ingest(const std::vector<Alert>& fresh) {
  std::vector<IncidentId> touched;
  if (fresh.empty()) return touched;
  stats_.alerts_ingested += fresh.size();

  const size_t n = fresh.size();
  int64_t batch_now = fresh[0].raised_ms;
  for (const auto& a : fresh) batch_now = std::max(batch_now, a.raised_ms);

  // Retire incidents whose last activity has fallen outside the merge window;
  // they can no longer absorb an alert, so drop them from the scanned open set.
  {
    std::vector<size_t> still;
    still.reserve(open_idx_.size());
    for (size_t idx : open_idx_)
      if (batch_now - incidents_[idx].last_update_ms <= cfg_.time_window_ms)
        still.push_back(idx);
    open_idx_.swap(still);
  }

  // 1. Cluster the fresh batch among itself.
  ds::RollbackDSU dsu(n);
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 1; j < n; ++j) {
      if (cfg_.same_zone_only && fresh[i].zone != fresh[j].zone) continue;
      const double ds_m = geo::distance_m(fresh[i].position, fresh[j].position);
      const int64_t dt = std::llabs(fresh[i].raised_ms - fresh[j].raised_ms);
      if (ds_m <= cfg_.space_radius_m && dt <= cfg_.time_window_ms) dsu.unite(i, j);
    }

  // 2. Route each cluster: fold into a live incident if one is near enough in
  //    space and time, otherwise open a new one.
  for (const auto& comp : dsu.components()) {
    double slat = 0, slon = 0; int64_t cmin = INT64_MAX;
    for (size_t idx : comp) {
      slat += fresh[idx].position.lat; slon += fresh[idx].position.lon;
      cmin = std::min(cmin, fresh[idx].raised_ms);
    }
    const geo::LatLon ccent{slat / double(comp.size()), slon / double(comp.size())};

    long best = -1; double best_d = cfg_.space_radius_m;
    for (size_t oi : open_idx_) {
      const Incident& inc = incidents_[oi];
      if (cmin - inc.last_update_ms > cfg_.time_window_ms) continue;
      const double d = geo::distance_m(ccent, inc.centroid);
      if (d <= best_d) { best_d = d; best = long(oi); }
    }
    if (best >= 0) {                       // absorbed into a live incident
      merge_into(incidents_[size_t(best)], comp, fresh);
      stats_.alerts_absorbed += comp.size();
      touched.push_back(incidents_[size_t(best)].id);
      continue;
    }

    if (comp.size() < cfg_.min_cluster) {
      // A lone alert with no live incident nearby -- still gets an incident so
      // the UI has one uniform object, and it stays open to absorb the next one.
      for (size_t idx : comp) {
        Incident inc{};
        inc.id = IncidentId(incidents_.size());
        inc.opened_ms = inc.last_update_ms = fresh[idx].raised_ms;
        inc.centroid = fresh[idx].position;
        inc.radius_m = fresh[idx].accuracy_m;
        inc.alerts.push_back(fresh[idx].id);
        inc.affected.push_back(fresh[idx].tourist);
        inc.max_severity = fresh[idx].severity;
        open_idx_.push_back(incidents_.size());
        incidents_.push_back(inc);
        touched.push_back(inc.id);
        ++stats_.incidents_opened;
      }
      continue;
    }

    Incident inc{};
    inc.id = IncidentId(incidents_.size());
    inc.opened_ms = INT64_MAX;
    inc.max_severity = 0;
    double s2lat = 0, s2lon = 0;
    for (size_t idx : comp) {
      const Alert& a = fresh[idx];
      inc.alerts.push_back(a.id);
      bool seen = false;
      for (auto t : inc.affected) if (t == a.tourist) { seen = true; break; }
      if (!seen) inc.affected.push_back(a.tourist);
      inc.opened_ms = std::min(inc.opened_ms, a.raised_ms);
      inc.last_update_ms = std::max(inc.last_update_ms, a.raised_ms);
      inc.max_severity = std::max(inc.max_severity, a.severity);
      s2lat += a.position.lat; s2lon += a.position.lon;
    }
    inc.centroid = {s2lat / double(comp.size()), s2lon / double(comp.size())};
    double r = 0;
    for (size_t idx : comp)
      r = std::max(r, geo::distance_m(inc.centroid, fresh[idx].position));
    inc.radius_m = r;

    open_idx_.push_back(incidents_.size());
    incidents_.push_back(inc);
    touched.push_back(inc.id);
    ++stats_.incidents_opened;
    stats_.alerts_absorbed += comp.size() - 1;   // the operator load removed
  }
  return touched;
}

const Incident* Correlator::get(IncidentId id) const {
  return id < incidents_.size() ? &incidents_[id] : nullptr;
}
std::vector<const Incident*> Correlator::open_incidents() const {
  std::vector<const Incident*> v;
  for (const auto& i : incidents_) v.push_back(&i);
  return v;
}
void Correlator::close(IncidentId) {}

}  // namespace safetrail::alert
