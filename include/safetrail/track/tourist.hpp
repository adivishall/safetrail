#pragma once
// A tracked person: current fix, recent history, and per-zone hysteresis state.
#include <string>
#include <vector>
#include "safetrail/ds/circular_buffer.hpp"
#include "safetrail/fence/hysteresis.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/power/adaptive_sampler.hpp"
#include "safetrail/types.hpp"

namespace safetrail::track {

using safetrail::TouristId;

struct Ping {
  geo::UncertainPoint fix{};
  double speed_mps = 0.0;
  double heading_deg = 0.0;
};

// Per-(tourist, zone) tracking. Lives in a small vector on the tourist rather
// than a global map: a tourist is near a handful of zones, so linear scan over
// 3-5 entries beats hashing, and it keeps the data next to what uses it.
struct ZoneState {
  ZoneId zone = kNoId;
  fence::HysteresisState hyst{};
  geo::Containment confirmed = geo::Containment::Outside;
  Timestamp entered_ms = 0;
  bool dwell_reported = false;
  bool approach_reported = false;
  // The Tourist::eval_epoch in which the spatial index last returned this zone.
  // fence::Evaluator uses it to find states the index did NOT return this tick,
  // which must still be observed -- see the reconciliation step there.
  uint64_t seen_epoch = 0;
};

struct Tourist {
  TouristId   id = kNoId;
  std::string digital_id;         // index into the Merkle evidence log
  GroupId     group = kNoId;

  geo::UncertainPoint last_fix{};
  double heading = 0.0;
  double speed = 0.0;

  static constexpr size_t kPingWindow = 64;
  ds::CircularBuffer<Ping, kPingWindow> pings{};

  std::vector<geo::LatLon> planned_route;
  size_t route_leg = 0;

  // One entry per zone the tourist is near or has open state with. Bounded by the
  // candidate window: fence::Evaluator drops an entry once its zone has left the
  // window and its state has settled back to Outside.
  std::vector<ZoneState> zone_states;
  uint64_t eval_epoch = 0;        // incremented by every usable evaluation
  power::AdaptiveSampler sampler{};
  bool alert_active = false;

  ZoneState& state_for(ZoneId z);
  ZoneState* find_state(ZoneId z);
  const ZoneState* peek_state(ZoneId z) const;

  double speed_mps() const;
  double heading_deg() const;
  // Extrapolated position for predictive alerting (GAP 2).
  geo::LatLon project(double seconds_ahead) const;
};

}  // namespace safetrail::track
