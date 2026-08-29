// GAP 5: many alerts clustered in space+time collapse into ONE incident.
#include "../test_harness.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include "safetrail/alert/correlator.hpp"
using namespace safetrail;
using namespace safetrail::alert;

static Alert mk(AlertId id, TouristId who, double lat, double lon, Timestamp t) {
  Alert a; a.id = id; a.tourist = who; a.zone = 0; a.severity = 4;
  a.position = {lat, lon}; a.accuracy_m = 10; a.raised_ms = t;
  a.kind = AlertKind::ZoneBreach;
  return a;
}

int main() {
  { // 5 alerts within ~50m and a few seconds -> one incident
    Correlator c;
    std::vector<Alert> fresh;
    for (int i = 0; i < 5; ++i)
      fresh.push_back(mk(AlertId(i), TouristId(i), 25.5700+0.0001*i, 91.8800, 1000+ i*500));
    auto touched = c.ingest(fresh);
    t::ok(c.stats().incidents_opened == 1, "5 co-located alerts -> 1 incident");
    t::ok(c.stats().alerts_absorbed == 4, "4 of 5 absorbed (not shown as cards)");
    const Incident* inc = c.get(touched[0]);
    t::ok(inc && inc->people() == 5, "incident lists all 5 affected people");
  }
  { // two far-apart clusters -> two incidents
    Correlator c;
    std::vector<Alert> fresh = {
      mk(0,0,25.5700,91.8800,1000), mk(1,1,25.5701,91.8801,1200),
      mk(2,2,25.6500,91.9500,1400), mk(3,3,25.6501,91.9501,1600)};
    c.ingest(fresh);
    t::ok(c.stats().incidents_opened == 2, "two distant clusters -> 2 incidents");
  }
  { // alerts far apart in TIME don't merge even if co-located
    Correlator c;
    std::vector<Alert> fresh = {
      mk(0,0,25.5700,91.8800, 1000),
      mk(1,1,25.5700,91.8800, 1000 + 10*60*1000)};   // 10 min later
    c.ingest(fresh);
    t::ok(c.stats().incidents_opened == 2, "same place, 10min apart -> not merged");
  }

  // ── Cross-tick behaviour. These are the cases the batch-only correlator failed:
  //    it clustered only within one ingest() call, so a cohort breaching one
  //    person per tick opened a fresh incident every tick and never accumulated.
  { // alerts arriving over successive ticks fold into ONE growing incident
    Correlator c;
    for (int k = 0; k < 6; ++k)
      c.ingest({ mk(AlertId(k), TouristId(k),
                    25.5700 + 0.00008 * (k % 3), 91.8800, 1000 + k * 10000) });
    t::ok(c.stats().incidents_opened == 1,
          "six alerts over six ticks (<=60s apart, co-located) -> one incident");
    size_t max_people = 0;
    for (const auto* inc : c.open_incidents())
      max_people = std::max(max_people, inc->people());
    t::ok(max_people == 6, "the incident accumulates all six people across ticks");
  }
  { // a gap longer than the window opens a fresh incident, same place
    Correlator c;
    c.ingest({ mk(0, 0, 25.5700, 91.8800, 0) });
    c.ingest({ mk(1, 1, 25.5700, 91.8800, 120000) });   // 2 min later
    t::ok(c.stats().incidents_opened == 2,
          "same place, a gap beyond the window -> a second incident");
  }
  { // an alert far from the live incident does not fold into it
    Correlator c;
    c.ingest({ mk(0, 0, 25.5700, 91.8800, 1000) });
    c.ingest({ mk(1, 1, 25.6500, 91.9500, 6000) });     // ~13 km away, 5 s later
    t::ok(c.stats().incidents_opened == 2,
          "far from the live incident -> a separate incident");
  }
  return t::report("alert/correlator");
}
