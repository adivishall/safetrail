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
// the random STREAM reproducible across platforms and standard-library versions:
// it is integer arithmetic with a fixed algorithm, so every host produces the
// same sequence from the same seed. std::mt19937's sequence is standardised too,
// but the distributions layered on it are not.
//
// That is necessary for a reproducible run and is not sufficient, which is worth
// being precise about. The stream feeds normal() (Box-Muller: log, sqrt, sin,
// cos) and then haversine distances and threshold comparisons, all in floating
// point -- so identical random integers can still yield different runs if the
// arithmetic differs. It did: allowing the compiler to fuse `a*b + c` into an FMA
// gave three different answers from one source (clang -O0, clang -O2, g++ -O2),
// because a last-bit difference in a distance flips one inside/outside test and
// every later event diverges. `-ffp-contract=off` in the Makefile and
// CMakeLists.txt closes that; see the note in the Makefile for the measurements.
//
// With both pieces in place the property the golden replay tests rely on is real:
// same seed, same source -> byte-identical output, on any build.
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

  // Temporal correlation of the error, as an AR(1) coefficient in [0,1).
  // Real GPS error does NOT teleport each second -- it drifts smoothly as the
  // satellite geometry and multipath change, so consecutive fixes are correlated.
  // Modelling that faithfully is the honest stress for the hysteresis filter:
  //   rho = 0    -> independent white noise each tick (the naive, jumpy model)
  //   rho = 0.9  -> smooth drift, what a real receiver produces
  // We default to 0.9 and report the hysteresis result against BOTH.
  double correlation = 0.9;
  bool   enabled = true;
};

// Per-device error state for the AR(1) process. Unitless (steady-state variance
// ~1); scaled by the current sigma at each fix. Carried on MobilityState so each
// tourist's drift evolves independently and reproducibly.
struct GpsDrift { double e = 0.0; double n = 0.0; };

// Applies error to a true position, returning what the device would report.
// `drift` is read AND updated: the AR(1) process needs the previous error to
// produce a correlated next one. Pass the same GpsDrift for a given device every
// tick. (A throwaway GpsDrift gives the old independent-per-call behaviour.)
geo::UncertainPoint apply_gps_error(const geo::LatLon& truth, int64_t t_ms,
                                    const GpsErrorModel& m, GpsDrift& drift, Rng& rng);

struct MobilityState {
  MobilityKind kind = MobilityKind::RandomWaypoint;
  geo::LatLon  truth{};              // ground truth, never exposed to the engine
  GpsDrift     drift{};              // AR(1) GPS error state for this device
  geo::LatLon  target{};
  double       speed_mps = 1.4;      // walking pace
  std::vector<geo::LatLon> route;
  size_t       leg = 0;
  bool         paused = false;
  int64_t      pause_until_ms = 0;

  // GuidedGroup: a scripted destination the tourist heads to and then mills
  // around, so a cohort converges on one place at roughly one time. This is what
  // turns "forty scattered alerts" into a single correlated incident.
  geo::LatLon  destination{};        // where the cohort is being led
  double       mill_radius_m = 60.0; // once arrived, wander within this radius
  bool         arrived = false;
};

// Advance one tourist's true position by dt. Returns the new ground truth.
geo::LatLon step_mobility(MobilityState& st, double dt_s, int64_t now_ms,
                          const geo::Bbox& roam, Rng& rng);

}  // namespace safetrail::sim
