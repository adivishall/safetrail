#pragma once
// Responders and their pool.
//
// A responder is an entity that can be sent to an incident: a ranger post, a
// medical team, a rescue unit. It sits at (or near) a road-network junction, and
// its availability flips as it is dispatched and freed. The pool is flat storage
// indexed by ResponderId -- the same shape as ZoneStore -- so the assigner can
// build a cost matrix over the available subset without chasing pointers.
#include <vector>
#include "safetrail/geo/point.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/types.hpp"

namespace safetrail::dispatch {

struct Responder {
  ResponderId     id = kNoId;
  geo::LatLon     pos;                       // last known position
  graph::NodeId   node = graph::kNoNode;     // snapped junction (nearest_node)
  bool            available = true;
  std::string     name;
};

class ResponderPool {
 public:
  ResponderId add(Responder r) {
    if (r.id == kNoId) r.id = ResponderId(pool_.size());
    pool_.push_back(std::move(r));
    return pool_.back().id;
  }
  size_t size() const { return pool_.size(); }
  const Responder& operator[](size_t i) const { return pool_[i]; }
  Responder&       operator[](size_t i)       { return pool_[i]; }

  const std::vector<Responder>& all() const { return pool_; }

  // Snap every responder to the graph. Call once after loading a road network.
  void snap_all(const graph::RoadGraph& g) {
    for (auto& r : pool_) r.node = g.nearest_node(r.pos);
  }

  std::vector<size_t> available_indices() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < pool_.size(); ++i)
      if (pool_[i].available) out.push_back(i);
    return out;
  }

 private:
  std::vector<Responder> pool_;
};

}  // namespace safetrail::dispatch
