#include "safetrail/track/trajectory.hpp"

#include "safetrail/geo/point.hpp"

namespace safetrail::track {

double path_length_m(const Tourist& t) {
  const size_t n = t.pings.size();
  if (n < 2) return 0.0;
  double total = 0.0;
  for (size_t i = 0; i + 1 < n; ++i)
    total += geo::distance_m(t.pings[i].fix.pos, t.pings[i + 1].fix.pos);
  return total;
}

double displacement_m(const Tourist& t) {
  const size_t n = t.pings.size();
  if (n < 2) return 0.0;
  return geo::distance_m(t.pings[0].fix.pos, t.pings[n - 1].fix.pos);   // newest .. oldest
}

double tortuosity(const Tourist& t) {
  const double disp = displacement_m(t);
  const double path = path_length_m(t);
  if (disp < 1e-6) return path > 1e-6 ? 1e9 : 1.0;   // moved nowhere but walked = wandering
  return path / disp;
}

double average_speed_mps(const Tourist& t, int64_t window_ms) {
  const size_t n = t.pings.size();
  if (n < 2) return 0.0;
  const int64_t now = t.pings[0].fix.t_ms;
  double dist = 0.0;
  int64_t span = 0;
  for (size_t i = 0; i + 1 < n; ++i) {
    if (now - t.pings[i + 1].fix.t_ms > window_ms) break;   // older than the window
    dist += geo::distance_m(t.pings[i].fix.pos, t.pings[i + 1].fix.pos);
    span = now - t.pings[i + 1].fix.t_ms;
  }
  return span > 0 ? dist / (double(span) / 1000.0) : 0.0;
}

}  // namespace safetrail::track
