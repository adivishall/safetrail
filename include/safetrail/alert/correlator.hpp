#pragma once
//
// Alert correlation.  [GAP 5]
//
// A landslide near a viewpoint with forty tourists produces forty alert cards in
// every existing dashboard. The operator becomes the bottleneck, and alert
// fatigue is a documented cause of missed real incidents.
//
// Forty alerts clustered in space and time are one INCIDENT affecting forty
// people. One card, one dispatch decision, forty names on it.
//
// Clustering in (x, y, t) with an anisotropic metric — 200 m and 60 s are
// comparable "distances" for our purposes, so the time axis is scaled, not
// treated as a third spatial dimension.
//
//   candidate generation:  spatial grid buckets      O(n) expected
//   merging:               union-find over pairs      O(n α(n))
//
// Union-Find again, but for a completely different reason than group cohesion —
// worth pointing out in the report that the same structure serves two unrelated
// purposes here.
//
#include <cstdint>
#include <vector>
#include "safetrail/alert/alert.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::alert {

struct CorrelationConfig {
  double  space_radius_m = 200.0;
  int64_t time_window_ms = 60000;
  size_t  min_cluster    = 2;      // below this it stays a standalone alert
  bool    same_zone_only = false;  // require identical zone id to merge
};

struct Incident {
  IncidentId id;
  int64_t    opened_ms;
  int64_t    last_update_ms;
  geo::LatLon centroid;
  double     radius_m;
  std::vector<AlertId>       alerts;
  std::vector<track::TouristId> affected;
  uint8_t    max_severity;

  size_t people() const { return affected.size(); }
};

class Correlator {
 public:
  explicit Correlator(CorrelationConfig cfg = {});

  // Feed newly raised alerts. Returns incidents created or modified this tick,
  // so the UI patches rather than re-renders.
  std::vector<IncidentId> ingest(const std::vector<Alert>& fresh);

  const Incident* get(IncidentId id) const;
  std::vector<const Incident*> open_incidents() const;
  void close(IncidentId id);

  struct Stats {
    uint64_t alerts_ingested = 0;
    uint64_t incidents_opened = 0;
    uint64_t alerts_absorbed = 0;   // ★ the headline: operator load removed
    double compression_ratio() const {
      return incidents_opened ? double(alerts_ingested) / double(incidents_opened) : 1.0;
    }
  };
  Stats stats() const { return stats_; }

 private:
  CorrelationConfig cfg_;
  std::vector<Incident> incidents_;
  Stats stats_{};
};

}  // namespace safetrail::alert
