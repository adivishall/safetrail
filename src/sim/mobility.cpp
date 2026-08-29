#include "safetrail/sim/mobility.hpp"
#include <cmath>

namespace safetrail::sim {

uint64_t Rng::next() {
  s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17;
  return s_;
}
double Rng::uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
double Rng::range(double lo, double hi) { return lo + uniform() * (hi - lo); }
uint32_t Rng::below(uint32_t n) { return n ? uint32_t(next() % n) : 0; }

double Rng::normal() {
  double u1 = uniform(), u2 = uniform();
  if (u1 < 1e-12) u1 = 1e-12;
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

geo::UncertainPoint apply_gps_error(const geo::LatLon& truth, int64_t t_ms,
                                    const GpsErrorModel& m, GpsDrift& drift, Rng& rng) {
  geo::UncertainPoint p{};
  p.t_ms = t_ms;
  if (!m.enabled) { p.pos = truth; p.accuracy_m = 1.0; return p; }

  if (rng.uniform() < m.dropout_probability) {   // no usable fix this tick
    p.pos = truth; p.accuracy_m = 999.0;
    return p;
  }
  const bool multipath = rng.uniform() < m.multipath_probability;
  const double sigma = multipath ? m.multipath_m : m.open_sky_m;

  // AR(1) drift, per axis (east, north):
  //     e_t = rho * e_{t-1} + sqrt(1 - rho^2) * N(0,1)
  // The sqrt(1-rho^2) innovation term keeps the steady-state variance at 1
  // regardless of rho, so the displacement magnitude stays ~sigma and the
  // reported accuracy remains honest. rho=0 collapses to independent white noise.
  const double rho = m.correlation < 0 ? 0 : (m.correlation > 0.999 ? 0.999 : m.correlation);
  const double innov = std::sqrt(1.0 - rho * rho);
  drift.e = rho * drift.e + innov * rng.normal();
  drift.n = rho * drift.n + innov * rng.normal();

  // Offset truth by (drift.e east, drift.n north) metres, scaled by sigma.
  const double east_m = drift.e * sigma, north_m = drift.n * sigma;
  geo::LatLon q = geo::offset(truth, east_m >= 0 ? 90.0 : 270.0, std::fabs(east_m));
  p.pos = geo::offset(q, north_m >= 0 ? 0.0 : 180.0, std::fabs(north_m));
  p.accuracy_m = sigma;
  return p;
}

geo::LatLon step_mobility(MobilityState& st, double dt_s, int64_t now_ms,
                          const geo::Bbox& roam, Rng& rng) {
  if (st.paused) {
    if (now_ms < st.pause_until_ms) return st.truth;
    st.paused = false;
  }

  auto pick_target = [&]() {
    if (st.kind == MobilityKind::RouteFollowing && !st.route.empty()) {
      st.target = st.route[st.leg % st.route.size()];
      ++st.leg;
    } else if (st.kind == MobilityKind::GuidedGroup) {
      // Head straight for the shared destination; once there, mill around it in
      // a tight radius so the cohort stays clustered (correlator input) rather
      // than dispersing back across the map.
      if (!st.arrived && geo::distance_m(st.truth, st.destination) > st.mill_radius_m) {
        st.target = st.destination;
      } else {
        st.arrived = true;
        const double br = rng.range(0.0, 360.0);
        const double rad = rng.range(0.0, st.mill_radius_m);
        st.target = geo::offset(st.destination, br, rad);
      }
    } else {
      st.target = {rng.range(roam.min_lat, roam.max_lat),
                   rng.range(roam.min_lon, roam.max_lon)};
    }
  };

  if (st.target.lat == 0.0 && st.target.lon == 0.0) pick_target();

  const double d = geo::distance_m(st.truth, st.target);
  const double step = st.speed_mps * dt_s;

  if (d <= step) {
    st.truth = st.target;
    pick_target();
    // People stop to photograph things. Without this the cohesion module never
    // sees a straggler, because everyone moves at identical speed forever.
    if (rng.uniform() < 0.15) {
      st.paused = true;
      st.pause_until_ms = now_ms + int64_t(rng.range(20000, 120000));
    }
    return st.truth;
  }
  st.truth = geo::offset(st.truth, geo::bearing_deg(st.truth, st.target), step);
  return st.truth;
}

}  // namespace safetrail::sim
