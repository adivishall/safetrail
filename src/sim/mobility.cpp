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
                                    const GpsErrorModel& m, Rng& rng) {
  geo::UncertainPoint p{};
  p.t_ms = t_ms;
  if (!m.enabled) { p.pos = truth; p.accuracy_m = 1.0; return p; }

  if (rng.uniform() < m.dropout_probability) {   // no usable fix this tick
    p.pos = truth; p.accuracy_m = 999.0;
    return p;
  }
  const bool multipath = rng.uniform() < m.multipath_probability;
  const double sigma = multipath ? m.multipath_m : m.open_sky_m;
  // Displace in a random direction by a normally-distributed magnitude. Reported
  // accuracy is the 68% radius, i.e. sigma -- which is what real devices report,
  // and it is why a single fix can legitimately land outside its own radius.
  const double mag = std::fabs(rng.normal()) * sigma;
  p.pos = geo::offset(truth, rng.range(0, 360), mag);
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
