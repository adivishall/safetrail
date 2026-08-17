#include "safetrail/alert/alert.hpp"

namespace safetrail::alert {

const char* to_string(AlertKind k) {
  switch (k) {
    case AlertKind::ZoneBreach: return "ZONE BREACH";
    case AlertKind::ZoneApproaching: return "APPROACHING";
    case AlertKind::ZoneUncertain: return "UNCERTAIN";
    case AlertKind::DwellExceeded: return "DWELL EXCEEDED";
    case AlertKind::Straggler: return "STRAGGLER";
    case AlertKind::GroupFragmented: return "GROUP SPLIT";
    case AlertKind::RouteDeviation: return "ROUTE DEVIATION";
    case AlertKind::Stationary: return "STATIONARY";
    case AlertKind::SignalLost: return "SIGNAL LOST";
    case AlertKind::SosTriggered: return "SOS";
  }
  return "?";
}
const char* to_string(AlertState s) {
  switch (s) {
    case AlertState::Open: return "open";
    case AlertState::Acknowledged: return "ack";
    case AlertState::Dispatched: return "dispatched";
    case AlertState::Resolved: return "resolved";
    case AlertState::Dismissed: return "dismissed";
  }
  return "?";
}

}  // namespace safetrail::alert
