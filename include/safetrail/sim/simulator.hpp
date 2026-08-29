#pragma once
// Discrete-time simulator. Owns the world; drives the evaluator.
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "safetrail/alert/correlator.hpp"
#include "safetrail/dispatch/assigner.hpp"
#include "safetrail/dispatch/responder.hpp"
#include "safetrail/fence/evaluator.hpp"
#include "safetrail/fence/zone.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/group/cohesion.hpp"
#include "safetrail/index/spatial_index.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sim/mobility.hpp"
#include "safetrail/track/anomaly.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::sim {

// A scripted "incident day" laid over the population. Without it the tourists
// wander at random and never converge, so alert correlation has nothing to
// collapse and responder dispatch has no interesting choice to make. The
// scenario steers a cohort onto one real hazard so a genuine mass incident
// forms, while the rest of the population produces the scattered independent
// incidents that make greedy-vs-optimal dispatch diverge.
struct Scenario {
  bool   enabled = true;
  double cohort_fraction = 0.55;   // share of tourists led toward the hazard
  double mill_radius_m   = 70.0;   // how tightly the cohort clusters on arrival
  // Which loaded hazard the cohort converges on. Empty -> the highest-severity
  // restricted zone found at load (Sonapani Waterfall Cliff in the OSM set).
  std::string hazard_name;
};

struct SimConfig {
  size_t   tourists      = 50;
  size_t   groups        = 8;
  int64_t  tick_ms       = 1000;
  int64_t  duration_ms   = 3600000;      // one simulated hour
  uint64_t seed          = 12345;
  geo::Bbox roam{25.50, 91.80, 25.62, 91.95};   // default: around Shillong
  GpsErrorModel gps{};
  fence::EvaluatorConfig eval{};
  index::IndexKind index = index::IndexKind::Quadtree;
  size_t   responders    = 8;        // dispatch: available responders on the road net
  bool     dispatch      = true;     // assign responders to incidents at end of run
  std::string roads_path;            // real OSM road file; empty -> synthetic grid
  double   collapse_fraction = 0.05; // fraction of tourists who stop moving mid-run (injury)
  Scenario scenario{};               // scripted incident day; disable for a pure random run
  bool     verbose = false;
};

class Simulator {
 public:
  explicit Simulator(SimConfig cfg);
  ~Simulator();

  bool load_zones(const std::string& geojson_path, std::string* err);
  void add_synthetic_zones(size_t n);      // for the scaling benchmark
  void spawn_tourists();

  void run();                              // to duration_ms
  void step();                             // one tick
  void finalize();                         // post-run: incident count + dispatch (run() calls it)

  int64_t now_ms() const { return now_ms_; }
  bool done() const { return now_ms_ >= cfg_.duration_ms; }

  const std::vector<fence::Event>& events() const { return all_events_; }
  const std::vector<track::Tourist>& tourists() const { return tourists_; }
  const fence::ZoneStore& zones() const { return zones_; }
  const index::SpatialIndex& index() const { return *index_; }
  const index::VersionedIndex& versioned() const { return *vindex_; }
  fence::Evaluator::Counters counters() const { return eval_->counters(); }
  const alert::Correlator& correlator() const { return *corr_; }
  const group::CohesionMonitor& cohesion() const { return *coh_; }

  struct Summary {
    uint64_t ticks = 0, enters = 0, exits = 0, uncertain = 0, approaching = 0,
             dwell = 0, cohesion_events = 0;
    uint64_t alerts = 0, incidents = 0, anomalies = 0;
    // Dispatch: greedy vs optimal responder assignment over the road network.
    uint64_t dispatched = 0, unassigned = 0;
    double   greedy_response_m = 0.0, optimal_response_m = 0.0;
  };
  Summary summary() const { return sum_; }

  // For the dashboard: responder positions and the optimal responder->incident
  // dispatch lines (resp_lat, resp_lon, inc_lat, inc_lon).
  const dispatch::ResponderPool& responders() const { return responders_; }
  const std::vector<std::array<double, 4>>& dispatch_lines() const { return dispatch_lines_; }

 private:
  SimConfig cfg_;
  Rng rng_;
  fence::ZoneStore zones_;
  std::unique_ptr<index::SpatialIndex> index_;
  std::unique_ptr<index::VersionedIndex> vindex_;
  std::unique_ptr<fence::Evaluator> eval_;
  std::unique_ptr<alert::Correlator> corr_;
  std::unique_ptr<group::CohesionMonitor> coh_;
  std::vector<track::Tourist> tourists_;
  std::vector<MobilityState> mobility_;
  std::vector<fence::Event> tick_events_, all_events_;
  graph::RoadGraph roads_;
  dispatch::ResponderPool responders_;
  std::vector<track::AnomalyKind> anomaly_state_;   // confirmed anomaly per tourist
  std::vector<track::AnomalyKind> anomaly_raw_;     // last raw reading (for confirmation)
  std::vector<int> anomaly_run_;                    // consecutive ticks of the raw reading
  std::vector<int64_t> collapse_at_;                // time each tourist stops moving (kForever = never)
  std::vector<std::array<double, 4>> dispatch_lines_;   // optimal responder->incident lines, for viz
  int64_t now_ms_ = 0;
  Summary sum_{};
  void reindex();
  void apply_scenario();        // lay the scripted incident day over the population
  void dispatch_responders();   // greedy vs optimal, at end of run
};

}  // namespace safetrail::sim
