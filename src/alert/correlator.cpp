#include "safetrail/alert/correlator.hpp"
#include "safetrail/ds/dynamic_connectivity.hpp"
#include <algorithm>
#include <climits>
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
std::vector<IncidentId> Correlator::ingest(const std::vector<Alert>& fresh) {
  std::vector<IncidentId> touched;
  if (fresh.empty()) return touched;
  stats_.alerts_ingested += fresh.size();

  const size_t n = fresh.size();
  ds::RollbackDSU dsu(n);
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 1; j < n; ++j) {
      if (cfg_.same_zone_only && fresh[i].zone != fresh[j].zone) continue;
      const double ds_m = geo::distance_m(fresh[i].position, fresh[j].position);
      const int64_t dt = std::llabs(fresh[i].raised_ms - fresh[j].raised_ms);
      if (ds_m <= cfg_.space_radius_m && dt <= cfg_.time_window_ms) dsu.unite(i, j);
    }

  for (const auto& comp : dsu.components()) {
    if (comp.size() < cfg_.min_cluster) {
      // Stays a standalone alert, but still gets an incident so the UI has one
      // uniform object to render.
      for (size_t idx : comp) {
        Incident inc{};
        inc.id = IncidentId(incidents_.size());
        inc.opened_ms = inc.last_update_ms = fresh[idx].raised_ms;
        inc.centroid = fresh[idx].position;
        inc.radius_m = fresh[idx].accuracy_m;
        inc.alerts.push_back(fresh[idx].id);
        inc.affected.push_back(fresh[idx].tourist);
        inc.max_severity = fresh[idx].severity;
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
    double slat = 0, slon = 0;
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
    inc.centroid = {slat / double(comp.size()), slon / double(comp.size())};
    double r = 0;
    for (size_t idx : comp)
      r = std::max(r, geo::distance_m(inc.centroid, fresh[idx].position));
    inc.radius_m = r;

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
