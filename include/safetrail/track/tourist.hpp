#pragma once
// A tracked person and their recent position history.
#include <string>
#include <vector>
#include "safetrail/ds/circular_buffer.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/power/adaptive_sampler.hpp"
#include "safetrail/types.hpp"

namespace safetrail::track {

using safetrail::TouristId;

struct Ping {
  geo::UncertainPoint fix;
  double speed_mps = 0.0;
  double heading_deg = 0.0;
};

struct Tourist {
  TouristId   id = kNoId;
  std::string digital_id;        // Merkle log entry reference, see evidence/
  GroupId     group = kNoId;

  geo::UncertainPoint last_fix{};

  // Fixed-capacity ring of recent pings. Bounded memory per tourist regardless of
  // session length — with 200 tourists at 10 Hz over a day, unbounded history is
  // 172M pings. The window is all the anomaly detectors need.
  static constexpr size_t kPingWindow = 256;
  ds::CircularBuffer<Ping, kPingWindow> pings{};

  // Declared itinerary, for route-deviation detection. Empty = unconstrained.
  std::vector<geo::LatLon> planned_route;

  power::AdaptiveSampler sampler{};   // GAP 7, per-device rate control

  double speed_mps() const;
  double heading_deg() const;
  // Extrapolated position, for the predictive alerting path. GAP 2.
  geo::LatLon project(double seconds_ahead) const;
};

}  // namespace safetrail::track
