#pragma once
//
// Group cohesion and straggler detection.  [GAP 4]
//
// No existing implementation models group structure — every tourist is tracked as
// an isolated point. But the signal that matters is usually not absolute position,
// it is SEPARATION. A tourist 800 m behind their group and falling further behind
// is the incident, and they may be nowhere near any zone boundary. A purely
// geofence-driven system is structurally blind to it.
//
// Model: groups are connected components under a proximity threshold. Each tick,
// build the proximity graph and compare components against the previous tick.
//
//   proximity pairs   grid-bucketed, O(n) expected rather than O(n²)
//   components        RollbackDSU, O(n log n)
//   split detection   set difference against previous tick, O(n)
//
// Declared groups (a tour party that booked together) are compared against
// OBSERVED components. Divergence is the alert: a declared group of eight that
// currently forms two components of six and two has lost two people.
//
#include <cstdint>
#include <string>
#include <vector>
#include "safetrail/ds/dynamic_connectivity.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::group {

using safetrail::GroupId;

struct CohesionConfig {
  double  proximity_m        = 150.0;  // within this = same component
  double  straggler_m        = 400.0;  // beyond this from centroid = straggler
  int64_t confirm_ms         = 60000;  // sustain before alerting; people stop to
                                       // photograph things
  double  widening_rate_mps  = 0.5;    // separation growing this fast = leaving
};

struct DeclaredGroup {
  GroupId id;
  std::vector<track::TouristId> members;
  std::string label;
};

enum class CohesionEventKind {
  GroupFragmented,     // declared group now spans multiple components
  StragglerDetected,   // member beyond straggler_m and widening
  MemberRejoined,      // separation resolved — close the alert
  GroupDispersed,      // no component holds a majority; serious
};

struct CohesionEvent {
  CohesionEventKind kind;
  GroupId           group;
  track::TouristId  subject;       // the straggler, where applicable
  int64_t           t_ms;
  double            separation_m;
  double            widening_mps;
  size_t            component_count;
};

class CohesionMonitor {
 public:
  explicit CohesionMonitor(CohesionConfig cfg = {});

  void declare_group(DeclaredGroup g);

  // Once per tick, after positions update.
  void update(const std::vector<track::Tourist>& ts, int64_t now_ms,
              std::vector<CohesionEvent>& out);

  // Current observed components, for the map overlay: convex hulls per group
  // with stragglers drawn outside them reads instantly.
  std::vector<std::vector<track::TouristId>> observed_components() const;

 private:
  CohesionConfig cfg_;
  std::vector<DeclaredGroup> declared_;
  ds::RollbackDSU dsu_{0};
  std::vector<std::vector<track::TouristId>> prev_components_;
  // Previous per-group state, so we emit CHANGES rather than re-reporting the
  // same fragmentation every tick. Same "transitions, not states" rule the
  // evaluator follows -- reporting state floods the operator.
  std::vector<uint8_t> was_fragmented_;
  std::vector<std::vector<track::TouristId>> prev_stragglers_;
};

}  // namespace safetrail::group
