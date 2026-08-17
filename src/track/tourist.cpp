#include "safetrail/track/tourist.hpp"

namespace safetrail::track {

ZoneState& Tourist::state_for(ZoneId z) {
  for (auto& s : zone_states) if (s.zone == z) return s;
  zone_states.push_back(ZoneState{});
  zone_states.back().zone = z;
  return zone_states.back();
}

const ZoneState* Tourist::peek_state(ZoneId z) const {
  for (const auto& s : zone_states) if (s.zone == z) return &s;
  return nullptr;
}

// Estimated from the ping window rather than the last pair: two consecutive
// noisy fixes 100 ms apart can imply 200 km/h. Averaging over the window is the
// difference between a usable speed and garbage.
double Tourist::speed_mps() const {
  if (pings.size() < 4) return 0.0;
  // Use the whole window and require a real time span. With 35 m multipath noise,
  // two fixes 1 s apart can imply 70 m/s -- which then produces nonsense ETAs in
  // the predictive path. Demanding >= kMinSpanS of history makes the noise average
  // out instead of dominating.
  static constexpr double kMinSpanS = 12.0;
  const Ping& newest = pings[0];
  const Ping& oldest = pings[pings.size() - 1];
  const double span = double(newest.fix.t_ms - oldest.fix.t_ms) / 1000.0;
  if (span < kMinSpanS) return 0.0;
  return geo::distance_m(oldest.fix.pos, newest.fix.pos) / span;
}

double Tourist::heading_deg() const {
  if (pings.size() < 4) return heading;
  return geo::bearing_deg(pings[pings.size() - 1].fix.pos, pings[0].fix.pos);
}

// GAP 2. Straight-line extrapolation at current speed and heading. Beyond a few
// minutes this is fiction in hill terrain, which is why the evaluator caps the
// prediction horizon rather than trusting arbitrary lookahead.
geo::LatLon Tourist::project(double seconds_ahead) const {
  const double v = speed_mps();
  if (v < 0.2) return last_fix.pos;             // stationary: nowhere to project
  return geo::offset(last_fix.pos, heading_deg(), v * seconds_ahead);
}

}  // namespace safetrail::track
