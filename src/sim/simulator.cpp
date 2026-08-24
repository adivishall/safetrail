#include "safetrail/sim/simulator.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"
#include <algorithm>
#include <cmath>

namespace safetrail::sim {

Simulator::Simulator(SimConfig cfg) : cfg_(cfg), rng_(cfg.seed) {
  index_ = index::make_index(cfg_.index);
  vindex_ = std::make_unique<index::VersionedIndex>();
  eval_ = std::make_unique<fence::Evaluator>(*index_, zones_, cfg_.eval);
  corr_ = std::make_unique<alert::Correlator>();
  coh_ = std::make_unique<group::CohesionMonitor>();
}
Simulator::~Simulator() = default;

bool Simulator::load_zones(const std::string& path, std::string* err) {
  if (!zones_.load_geojson(path, err)) return false;
  reindex();

  // Fit the tourist roam area to the loaded zones so the simulation adapts to any
  // dataset -- real OSM data spans a different extent than the old demo box, and
  // tourists wandering outside every zone would exercise nothing. Inset slightly
  // so they start inside the zone field rather than on its edge.
  geo::Bbox ext = geo::Bbox::empty();
  for (index::ZoneId id : zones_.all_ids()) ext.expand(zones_.get(id)->shape.bbox());
  if (ext.max_lat > ext.min_lat) {
    const double ilat = (ext.max_lat - ext.min_lat) * 0.08;
    const double ilon = (ext.max_lon - ext.min_lon) * 0.08;
    cfg_.roam = {ext.min_lat + ilat, ext.min_lon + ilon,
                 ext.max_lat - ilat, ext.max_lon - ilon};
  }
  return true;
}

// Synthetic zones for the scaling benchmark: we need 100k of them and nobody is
// hand-drawing that.
void Simulator::add_synthetic_zones(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const double clat = rng_.range(cfg_.roam.min_lat - 0.02, cfg_.roam.max_lat + 0.02);
    const double clon = rng_.range(cfg_.roam.min_lon - 0.02, cfg_.roam.max_lon + 0.02);
    const double r = rng_.range(0.0004, 0.0025);
    geo::Ring ring;
    const int verts = 6 + int(rng_.below(10));
    for (int v = 0; v < verts; ++v) {
      const double a = 6.283185307 * double(v) / double(verts);
      ring.push_back({clat + r * std::sin(a), clon + r * std::cos(a) * 1.1});
    }
    fence::Zone z;
    z.name = "synthetic-" + std::to_string(i);
    z.kind = fence::ZoneKind::Caution;
    z.severity = uint8_t(1 + rng_.below(5));
    z.shape = geo::Polygon(std::move(ring));
    zones_.add(std::move(z));
  }
  reindex();
}

void Simulator::reindex() {
  std::vector<std::pair<index::ZoneId, geo::Bbox>> items;
  for (index::ZoneId id : zones_.all_ids())
    items.emplace_back(id, zones_.get(id)->shape.bbox());
  index_->build(items);

  // GAP 3: mirror the zone set into the persistent index, one version per zone,
  // stamped at the moment its validity window opens. That gives the investigation
  // view a real history to query rather than a synthetic one.
  vindex_ = std::make_unique<index::VersionedIndex>();
  std::vector<std::pair<Timestamp, index::ZoneId>> ordered;
  for (index::ZoneId id : zones_.all_ids())
    ordered.emplace_back(zones_.get(id)->validity.from, id);
  std::sort(ordered.begin(), ordered.end());
  for (const auto& [at, id] : ordered) {
    const auto* z = zones_.get(id);
    vindex_->add_zone(id, z->shape.bbox(), z->validity, at);
  }
}

void Simulator::spawn_tourists() {
  tourists_.clear();
  mobility_.clear();
  tourists_.reserve(cfg_.tourists);
  mobility_.reserve(cfg_.tourists);

  const size_t per_group = cfg_.groups ? (cfg_.tourists + cfg_.groups - 1) / cfg_.groups : 1;

  for (size_t i = 0; i < cfg_.tourists; ++i) {
    track::Tourist t;
    t.id = TouristId(i);
    t.group = GroupId(cfg_.groups ? i / per_group : 0);
    t.digital_id = "TID-" + std::string(5 - std::to_string(i).size(), '0') + std::to_string(i);

    MobilityState m;
    m.kind = MobilityKind::RandomWaypoint;
    // Group members start near each other, so cohesion has something to break.
    const double glat = cfg_.roam.min_lat +
        (cfg_.roam.max_lat - cfg_.roam.min_lat) * double(t.group + 1) / double(cfg_.groups + 1);
    m.truth = {glat + rng_.range(-0.002, 0.002),
               rng_.range(cfg_.roam.min_lon, cfg_.roam.max_lon)};
    m.speed_mps = rng_.range(0.9, 1.9);
    t.last_fix = apply_gps_error(m.truth, 0, cfg_.gps, m.drift, rng_);

    tourists_.push_back(std::move(t));
    mobility_.push_back(std::move(m));
  }

  for (size_t g = 0; g < cfg_.groups; ++g) {
    group::DeclaredGroup dg;
    dg.id = GroupId(g);
    dg.label = "party-" + std::to_string(g);
    for (const auto& t : tourists_) if (t.group == g) dg.members.push_back(t.id);
    if (dg.members.size() >= 2) coh_->declare_group(std::move(dg));
  }
}

void Simulator::step() {
  const double dt = double(cfg_.tick_ms) / 1000.0;
  now_ms_ += cfg_.tick_ms;

  for (size_t i = 0; i < tourists_.size(); ++i) {
    const geo::LatLon truth = step_mobility(mobility_[i], dt, now_ms_, cfg_.roam, rng_);
    tourists_[i].last_fix = apply_gps_error(truth, now_ms_, cfg_.gps, mobility_[i].drift, rng_);
    track::Ping p{};
    p.fix = tourists_[i].last_fix;
    tourists_[i].pings.push(p);
  }

  tick_events_.clear();
  eval_->evaluate_all(tourists_, now_ms_, tick_events_);

  // fence::Event -> alert::Alert, then correlate.  [GAP 5]
  std::vector<alert::Alert> fresh;
  for (const auto& e : tick_events_) {
    all_events_.push_back(e);
    switch (e.kind) {
      case fence::EventKind::ZoneEnter: ++sum_.enters; break;
      case fence::EventKind::ZoneExit: ++sum_.exits; continue;   // no alert on exit
      case fence::EventKind::ZoneUncertain: ++sum_.uncertain; break;
      case fence::EventKind::ZoneApproaching: ++sum_.approaching; break;
      case fence::EventKind::DwellExceeded: ++sum_.dwell; break;
    }
    const fence::Zone* z = zones_.get(e.zone);
    if (!z) continue;
    if (z->kind == fence::ZoneKind::Safe) continue;

    alert::Alert a{};
    a.id = AlertId(sum_.alerts++);
    a.kind = e.kind == fence::EventKind::ZoneApproaching ? alert::AlertKind::ZoneApproaching
           : e.kind == fence::EventKind::ZoneUncertain   ? alert::AlertKind::ZoneUncertain
           : e.kind == fence::EventKind::DwellExceeded   ? alert::AlertKind::DwellExceeded
                                                         : alert::AlertKind::ZoneBreach;
    a.severity = z->severity;
    a.tourist = e.tourist; a.zone = e.zone;
    a.position = tourists_[e.tourist].last_fix.pos;
    a.accuracy_m = e.accuracy_m;
    a.raised_ms = e.t_ms;
    a.eta_s = e.eta_s;
    fresh.push_back(a);
  }
  if (!fresh.empty()) corr_->ingest(fresh);

  // Cohesion is expensive (O(n^2)); once a minute is plenty for walking pace.
  if (now_ms_ % 60000 == 0) {
    std::vector<group::CohesionEvent> ce;
    coh_->update(tourists_, now_ms_, ce);
    sum_.cohesion_events += ce.size();
  }
  ++sum_.ticks;
}

// Assign responders to the incidents the correlator opened, over a road network:
// greedy nearest-first vs the Hungarian optimum, side by side. This is where the
// graph + dispatch stack meets the simulation.  [Phase 8]
void Simulator::dispatch_responders() {
  if (!cfg_.dispatch) return;

  // Build a synthetic road grid over the roam area and place responders on it, once.
  if (roads_.node_count() == 0) {
    roads_ = graph::RoadGraph::grid(cfg_.roam, 12, 12, cfg_.seed);
    for (size_t i = 0; i < cfg_.responders && roads_.node_count() > 0; ++i) {
      dispatch::Responder r;
      r.pos = roads_.pos(graph::NodeId(rng_.below(uint32_t(roads_.node_count()))));
      responders_.add(r);
    }
    responders_.snap_all(roads_);
  }

  std::vector<dispatch::Incident> incidents;
  for (const auto* inc : corr_->open_incidents())
    incidents.push_back({inc->id, inc->centroid, graph::kNoNode});
  dispatch::snap_incidents(incidents, roads_);

  const auto greedy  = dispatch::assign_greedy(responders_, incidents, roads_);
  const auto optimal = dispatch::assign_optimal(responders_, incidents, roads_);
  sum_.dispatched         = optimal.dispatches.size();
  sum_.unassigned         = optimal.unassigned;
  sum_.greedy_response_m  = greedy.total_m;
  sum_.optimal_response_m = optimal.total_m;
}

void Simulator::run() {
  while (!done()) step();
  sum_.incidents = corr_->stats().incidents_opened;
  dispatch_responders();
}

}  // namespace safetrail::sim
