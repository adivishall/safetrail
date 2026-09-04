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
//   candidate generation:  all pairs within the fresh batch  O(n²), n small
//   merging:               union-find over those pairs       O(n α(n))
//
// Union-Find again, but for a completely different reason than group cohesion —
// worth pointing out in the report that the same structure serves two unrelated
// purposes here.
//
// ── What "clustered" means here, precisely ───────────────────────────────────
//
// This is CONNECTED-COMPONENT clustering under pairwise space/time adjacency,
// not "every alert within one global radius and window". The difference is
// visible and intended:
//
//   A is 150 m from B, B is 150 m from C, A is 300 m from C, radius 200 m
//     -> A, B, C form ONE incident, even though A and C are not adjacent.
//
// That is the right model for the thing being detected. A landslide across a
// 600 m stretch of trail, or a cohort strung out along a ridge, is one event; the
// alerts form a chain rather than a disc, and cutting the chain into three cards
// recreates exactly the operator overload the module exists to remove. The cost
// is that a long enough chain of unrelated alerts can transitively merge -- the
// standard single-linkage chaining effect. `min_cluster` and the retirement
// window bound it in practice, and tests/alert/correlator_test.cpp pins the
// behaviour explicitly so it is a documented choice rather than a surprise.
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

enum class IncidentStatus {
  Open,     // still absorbing alerts, still on the operator's board
  Closed,   // resolved by an operator; never absorbs another alert
};
const char* to_string(IncidentStatus s);

struct Incident {
  IncidentId id;
  IncidentStatus status = IncidentStatus::Open;
  int64_t    opened_ms;
  int64_t    last_update_ms;
  int64_t    closed_ms = 0;          // meaningful only when status == Closed
  geo::LatLon centroid;
  double     radius_m;
  std::vector<AlertId>       alerts;
  std::vector<track::TouristId> affected;
  // Positions of every alert folded into this incident. Kept because the radius
  // must stay a bound over ALL members as the centroid moves -- see the note on
  // merge_into in the .cpp. Same length as `alerts`.
  std::vector<geo::LatLon>   alert_positions;
  uint8_t    max_severity;

  size_t people() const { return affected.size(); }
  bool   is_open() const { return status == IncidentStatus::Open; }
};

class Correlator {
 public:
  explicit Correlator(CorrelationConfig cfg = {});

  // Feed newly raised alerts. Returns incidents created or modified this tick,
  // so the UI patches rather than re-renders.
  std::vector<IncidentId> ingest(const std::vector<Alert>& fresh);

  const Incident* get(IncidentId id) const;

  // Only incidents still Open. This used to return every incident ever created,
  // which made the operator board's "open" count meaningless and made close() a
  // no-op with no observable effect.
  std::vector<const Incident*> open_incidents() const;

  // Every incident, open or closed -- the history view.
  std::vector<const Incident*> all_incidents() const;

  // Operator resolves an incident. Returns false for an unknown id or one that
  // is already closed. A closed incident is removed from the merge set, so a
  // later alert in the same place opens a NEW incident rather than silently
  // reopening one the operator has signed off.
  bool close(IncidentId id, int64_t at_ms = 0);

  size_t open_count() const;

  struct Stats {
    uint64_t alerts_ingested = 0;
    uint64_t incidents_opened = 0;
    uint64_t incidents_closed = 0;
    uint64_t alerts_absorbed = 0;   // ★ the headline: operator load removed
    double compression_ratio() const {
      return incidents_opened ? double(alerts_ingested) / double(incidents_opened) : 1.0;
    }
  };
  Stats stats() const { return stats_; }

 private:
  CorrelationConfig cfg_;
  std::vector<Incident> incidents_;
  // Indices of incidents still inside their merge window, newest activity first.
  // A fresh alert near one of these joins it instead of opening a new card, so a
  // milling cohort collapses into ONE growing incident rather than one per tick.
  std::vector<size_t> open_idx_;
  Stats stats_{};
};

}  // namespace safetrail::alert
