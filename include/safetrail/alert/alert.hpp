#pragma once
// Alerts and their lifecycle.
#include <string>
#include <vector>
#include "safetrail/geo/point.hpp"
#include "safetrail/types.hpp"

namespace safetrail::alert {

using safetrail::AlertId;
using safetrail::IncidentId;
using safetrail::TouristId;
using safetrail::ZoneId;

enum class AlertKind {
  ZoneBreach,       // confirmed entry into a restricted zone
  ZoneApproaching,  // predicted crossing            [GAP 2]
  ZoneUncertain,    // ambiguous containment         [GAP 1]
  DwellExceeded,
  Straggler,        // separated from group          [GAP 4]
  GroupFragmented,  //                               [GAP 4]
  RouteDeviation,
  Stationary,       // no movement, possible injury
  SignalLost,       // pings stopped
  SosTriggered,
};

enum class AlertState { Open, Acknowledged, Dispatched, Resolved, Dismissed };

struct Alert {
  AlertId    id = kNoId;
  AlertKind  kind;
  AlertState state = AlertState::Open;
  uint8_t    severity = 1;          // 1..5, from the zone or the rule

  TouristId  tourist = kNoId;
  ZoneId     zone    = kNoId;
  IncidentId incident = kNoId;      // set by the correlator  [GAP 5]

  geo::LatLon position{};
  double     accuracy_m = 0.0;      // carried through so the operator sees it
  Timestamp  raised_ms = 0;
  Timestamp  acknowledged_ms = 0;

  double eta_s = 0.0;               // ZoneApproaching only
  std::string note;

  // Triage score. Deliberately not just severity: a severity-3 alert open for
  // four minutes with no responder within 20 km outranks a fresh severity-4 next
  // to a police post. Computed in triage.hpp, stored here so the heap is cheap.
  double priority = 0.0;
};

const char* to_string(AlertKind k);
const char* to_string(AlertState s);

}  // namespace safetrail::alert
