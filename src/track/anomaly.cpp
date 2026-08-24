#include "safetrail/track/anomaly.hpp"

#include <algorithm>
#include <cmath>
#include "safetrail/geo/point.hpp"

namespace safetrail::track {

const char* to_string(AnomalyKind k) {
  switch (k) {
    case AnomalyKind::None: return "none";
    case AnomalyKind::SignalLost: return "signal-lost";
    case AnomalyKind::Stationary: return "stationary";
    case AnomalyKind::RouteDeviation: return "route-deviation";
  }
  return "?";
}

namespace {
// Distance from p to segment a-b, in metres, via a local equirectangular
// projection (fine at these scales, far below GPS noise).
double point_seg_m(const geo::LatLon& p, const geo::LatLon& a, const geo::LatLon& b) {
  const double kM = 111320.0;
  const double clat = std::cos(a.lat * 3.14159265358979323846 / 180.0);
  const double ax = 0.0, ay = 0.0;
  const double bx = (b.lon - a.lon) * kM * clat, by = (b.lat - a.lat) * kM;
  const double px = (p.lon - a.lon) * kM * clat, py = (p.lat - a.lat) * kM;
  const double len2 = bx * bx + by * by;
  double t = len2 > 0 ? ((px - ax) * bx + (py - ay) * by) / len2 : 0.0;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  const double cx = ax + t * bx, cy = ay + t * by;
  const double dx = px - cx, dy = py - cy;
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

AnomalyResult detect(const Tourist& t, int64_t now_ms, const AnomalyConfig& cfg) {
  const size_t n = t.pings.size();
  if (n == 0) return {AnomalyKind::SignalLost, double(cfg.signal_gap_ms)};

  // 1. Signal loss -- no data trumps everything.
  const int64_t gap = now_ms - t.pings[0].fix.t_ms;
  if (gap > cfg.signal_gap_ms) return {AnomalyKind::SignalLost, double(gap)};

  // 2. Stationary -- net movement across a long-enough window is tiny. We compare
  // the AVERAGE of the newest few fixes against the average of the oldest few in
  // the window, not raw endpoints, so GPS jitter (which can spike 30+ m on a
  // single fix) averages out and does not masquerade as movement.
  {
    size_t last = 0;
    int64_t covered = 0;
    for (size_t i = 1; i < n; ++i) {
      covered = t.pings[0].fix.t_ms - t.pings[i].fix.t_ms;
      last = i;
      if (covered >= cfg.stationary_window_ms) break;
    }
    if (covered >= cfg.stationary_window_ms) {
      auto avg = [&](size_t a, size_t b) {
        double la = 0, lo = 0; size_t c = 0;
        for (size_t i = a; i <= b && i < n; ++i) { la += t.pings[i].fix.pos.lat; lo += t.pings[i].fix.pos.lon; ++c; }
        return geo::LatLon{la / double(c ? c : 1), lo / double(c ? c : 1)};
      };
      const size_t k = std::min<size_t>(5, last / 2 ? last / 2 : 1);
      const double net = geo::distance_m(avg(0, k), avg(last > k ? last - k : 0, last));
      if (net < cfg.stationary_radius_m) return {AnomalyKind::Stationary, net};
    }
  }

  // 3. Route deviation -- too far from the nearest leg of the planned route.
  if (t.planned_route.size() >= 2) {
    const geo::LatLon& p = t.pings[0].fix.pos;
    double best = 1e300;
    for (size_t i = 0; i + 1 < t.planned_route.size(); ++i)
      best = std::min(best, point_seg_m(p, t.planned_route[i], t.planned_route[i + 1]));
    if (best > cfg.route_deviation_m) return {AnomalyKind::RouteDeviation, best};
  }

  return {AnomalyKind::None, 0.0};
}

}  // namespace safetrail::track
