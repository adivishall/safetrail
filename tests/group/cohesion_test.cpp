// GAP 4: a declared group that splits into two clusters must be detected.
#include "../test_harness.hpp"
#include <vector>
#include "safetrail/group/cohesion.hpp"
using namespace safetrail;
using namespace safetrail::group;

static track::Tourist at(TouristId id, GroupId g, double lat, double lon) {
  track::Tourist t; t.id = id; t.group = g;
  t.last_fix = {{lat, lon}, 4.0, 1000};
  return t;
}

int main() {
  CohesionConfig cfg;                 // proximity 150m, straggler 400m
  CohesionMonitor mon(cfg);
  DeclaredGroup g; g.id = 0; g.label = "party";
  for (TouristId i = 0; i < 4; ++i) g.members.push_back(i);
  mon.declare_group(g);

  // all four clustered within a few metres -> cohesive, no event
  std::vector<track::Tourist> ts = {
    at(0,0,25.5700,91.8800), at(1,0,25.5701,91.8801),
    at(2,0,25.5700,91.8802), at(3,0,25.5701,91.8800)};
  std::vector<CohesionEvent> ev;
  mon.update(ts, 60000, ev);
  t::ok(ev.empty(), "tight group produces no cohesion event");

  // move two members ~1km away -> the group has fragmented
  ts[2].last_fix.pos = {25.5790, 91.8900};
  ts[3].last_fix.pos = {25.5791, 91.8901};
  ev.clear();
  mon.update(ts, 120000, ev);
  bool frag = false;
  for (auto& e : ev)
    if (e.kind == CohesionEventKind::GroupFragmented ||
        e.kind == CohesionEventKind::GroupDispersed) frag = true;
  t::ok(frag, "split group raises a fragmentation/dispersal event");

  // observed components should now be two clusters of two
  auto comps = mon.observed_components();
  t::ok(comps.size() >= 2, "observed as at least two components after the split");
  return t::report("group/cohesion");
}
