#pragma once
// Mobility models + GPS error injection.
//
// The noise model is not decoration -- it is what makes the hysteresis A/B
// measurement (GAP 8) meaningful. Real GPS is 3-5 m in open sky and 20-50 m under
// multipath, so we model both regimes and switch between them.
#include <cstdint>
#include <vector>
#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"

namespace safetrail::sim {

// Deterministic PRNG. std::mt19937 would work, but an explicit xorshift makes
// runs reproducible across platforms and standard-library versions -- which
// matters because the golden replay tests compare byte-identical output.
class Rng {
 public:
  explicit Rng(uint64_t seed = 0x2545F4914F6CDD1DULL) : s_(seed ? seed : 1) {}
  uint64_t next();
  double uniform();                        // [0,1)
  double range(double lo, double hi);
  double normal();                         // Box-Muller, mean 0 sd 1
  uint32_t below(uint32_t n);
 private:
  uint64_t s_;
};

enum class MobilityKind { RandomWaypoint, RouteFollowing, GuidedGroup };

struct GpsErrorModel {
  double open_sky_m   = 4.0;    // accuracy in good conditions
  double multipath_m  = 35.0;   // dense/steep terrain
  double multipath_probability = 0.25;
  double dropout_probability   = 0.02;   // no fix at all this tick
  bool   enabled = true;
};

// Applies error to a true position, returning what the device would report.
geo::UncertainPoint apply_gps_error(const geo::LatLon& truth, int64_t t_ms,
                                    const GpsErrorModel& m, Rng& rng);

struct MobilityState {
  MobilityKind kind = MobilityKind::RandomWaypoint;
  geo::LatLon  truth{};              // ground truth, never exposed to the engine
  geo::LatLon  target{};
  double       speed_mps = 1.4;      // walking pace
  std::vector<geo::LatLon> route;
  size_t       leg = 0;
  bool         paused = false;
  int64_t      pause_until_ms = 0;
};

// Advance one tourist's true position by dt. Returns the new ground truth.
geo::LatLon step_mobility(MobilityState& st, double dt_s, int64_t now_ms,
                          const geo::Bbox& roam, Rng& rng);

}  // namespace safetrail::sim
