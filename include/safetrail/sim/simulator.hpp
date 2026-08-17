#pragma once
// Discrete-time simulator. Owns the world; drives the evaluator.
#include <memory>
#include <string>
#include <vector>
#include "safetrail/alert/correlator.hpp"
#include "safetrail/fence/evaluator.hpp"
#include "safetrail/fence/zone.hpp"
#include "safetrail/group/cohesion.hpp"
#include "safetrail/index/spatial_index.hpp"
#include "safetrail/sim/mobility.hpp"
#include "safetrail/track/tourist.hpp"

namespace safetrail::sim {

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

  int64_t now_ms() const { return now_ms_; }
  bool done() const { return now_ms_ >= cfg_.duration_ms; }

  const std::vector<fence::Event>& events() const { return all_events_; }
  const std::vector<track::Tourist>& tourists() const { return tourists_; }
  const fence::ZoneStore& zones() const { return zones_; }
  const index::SpatialIndex& index() const { return *index_; }
  fence::Evaluator::Counters counters() const { return eval_->counters(); }
  const alert::Correlator& correlator() const { return *corr_; }
  const group::CohesionMonitor& cohesion() const { return *coh_; }

  struct Summary {
    uint64_t ticks = 0, enters = 0, exits = 0, uncertain = 0, approaching = 0,
             dwell = 0, cohesion_events = 0;
    uint64_t alerts = 0, incidents = 0;
  };
  Summary summary() const { return sum_; }

 private:
  SimConfig cfg_;
  Rng rng_;
  fence::ZoneStore zones_;
  std::unique_ptr<index::SpatialIndex> index_;
  std::unique_ptr<fence::Evaluator> eval_;
  std::unique_ptr<alert::Correlator> corr_;
  std::unique_ptr<group::CohesionMonitor> coh_;
  std::vector<track::Tourist> tourists_;
  std::vector<MobilityState> mobility_;
  std::vector<fence::Event> tick_events_, all_events_;
  int64_t now_ms_ = 0;
  Summary sum_{};
  void reindex();
};

}  // namespace safetrail::sim
