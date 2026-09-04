// Incident lifecycle, clustering semantics, and the incident radius.
//
// Three separate things this pins:
//
//   1. close() actually closes. It used to be an empty function body, and
//      open_incidents() returned every incident ever created -- so the operator
//      board's "open" count was really "total", and resolving an incident had no
//      observable effect anywhere.
//   2. The clustering rule is CONNECTED COMPONENTS under pairwise adjacency, not
//      "everything inside one global radius". That is a deliberate choice with a
//      visible consequence (transitive chaining), so it is asserted rather than
//      left to be discovered.
//   3. The radius stays a true bound over ALL member alerts as the centroid
//      moves. It used to be max'd only against newly arriving alerts, so a
//      cluster that dragged the centroid left the original alerts outside the
//      circle the incident claims to cover.
#include "../test_harness.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "safetrail/alert/correlator.hpp"

using namespace safetrail;
using namespace safetrail::alert;

static Alert at(AlertId id, double lat, double lon, int64_t t_ms, uint8_t sev = 2) {
  Alert a;
  a.id = id;
  a.kind = AlertKind::ZoneBreach;
  a.severity = sev;
  a.tourist = TouristId(id);
  a.position = {lat, lon};
  a.accuracy_m = 5.0;
  a.raised_ms = t_ms;
  return a;
}

// Metres east of a reference longitude, at Shillong's latitude.
static double lon_for(double base_lon, double east_m) {
  return base_lon + east_m / (111320.0 * 0.902);
}

int main() {
  const double LAT = 25.5700, LON = 91.8800;

  // ── close() and open_incidents() ───────────────────────────────────────────
  {
    Correlator c;
    c.ingest({at(1, LAT, LON, 1000), at(2, LAT, lon_for(LON, 40), 1000)});
    t::ok(c.open_count() == 1, "one incident opened");
    t::ok(c.open_incidents().size() == 1, "open_incidents() sees it");
    t::ok(c.all_incidents().size() == 1, "all_incidents() sees it too");

    const IncidentId id = c.open_incidents()[0]->id;
    t::ok(c.close(id, 5000), "close() reports success");
    t::ok(c.open_count() == 0, "and the incident is no longer open");
    t::ok(c.open_incidents().empty(), "open_incidents() no longer returns it");
    t::ok(c.all_incidents().size() == 1, "but the history still has it");
    t::ok(c.get(id)->status == IncidentStatus::Closed, "its status is Closed");
    t::ok(c.get(id)->closed_ms == 5000, "and it records when");
    t::ok(c.stats().incidents_closed == 1, "the stats count it");

    t::ok(!c.close(id, 6000), "closing an already-closed incident reports false");
    t::ok(c.get(id)->closed_ms == 5000, "and does not overwrite the close time");
    t::ok(!c.close(9999, 6000), "closing an unknown id reports false");

    // A later nearby alert must open a NEW incident, not silently reopen the one
    // an operator has signed off.
    c.ingest({at(3, LAT, lon_for(LON, 20), 20000)});
    t::ok(c.open_count() == 1, "a later nearby alert opens a fresh incident");
    t::ok(c.all_incidents().size() == 2, "so there are now two in total");
    t::ok(c.get(id)->status == IncidentStatus::Closed, "and the closed one stays closed");
  }

  // ── clustering is single-linkage: transitivity is intended ─────────────────
  //
  // A is 150 m from B, B is 150 m from C, A is 300 m from C, radius 200 m. All
  // three become ONE incident. This is the right model for a landslide spread
  // along a trail or a cohort strung out on a ridge -- the alerts form a chain,
  // not a disc -- and cutting the chain into pieces would recreate the operator
  // overload the module exists to remove.
  {
    CorrelationConfig cfg;
    cfg.space_radius_m = 200.0;
    cfg.time_window_ms = 60000;
    Correlator c(cfg);

    c.ingest({at(1, LAT, lon_for(LON, 0), 1000),
              at(2, LAT, lon_for(LON, 150), 1000),
              at(3, LAT, lon_for(LON, 300), 1000)});

    t::ok(c.open_count() == 1, "A-B-C chain collapses into ONE incident");
    const Incident* inc = c.open_incidents()[0];
    t::ok(inc->alerts.size() == 3, "carrying all three alerts");
    t::ok(inc->people() == 3, "and all three people");

    // The endpoints really are further apart than the radius: this is genuine
    // transitivity, not three alerts that happened to be mutually close.
    const double a_to_c = geo::distance_m({LAT, lon_for(LON, 0)}, {LAT, lon_for(LON, 300)});
    t::ok(a_to_c > cfg.space_radius_m,
          "the chain's endpoints are further apart than the merge radius (" +
              std::to_string(int(a_to_c)) + " m > 200 m)");

    // And the negative case: a true gap does NOT merge.
    Correlator c2(cfg);
    c2.ingest({at(1, LAT, lon_for(LON, 0), 1000),
               at(2, LAT, lon_for(LON, 900), 1000)});
    t::ok(c2.open_count() == 2, "two alerts with no chain between them stay separate");
  }

  // ── time is part of the adjacency, not just space ──────────────────────────
  {
    CorrelationConfig cfg;
    cfg.space_radius_m = 200.0;
    cfg.time_window_ms = 10000;
    Correlator c(cfg);
    // Same place, but far apart in time and delivered in separate batches so the
    // retirement window applies.
    c.ingest({at(1, LAT, LON, 1000)});
    c.ingest({at(2, LAT, LON, 500000)});
    t::ok(c.open_count() == 2,
          "co-located alerts outside the time window are separate incidents");
  }

  // ── the radius stays a bound over every member ─────────────────────────────
  //
  // The regression case, constructed so the OLD code is provably wrong rather
  // than accidentally right: one lone alert, then a tight group of five arriving
  // 200 m away. The merged centroid lands near the group, which makes the LONE,
  // ORIGINAL alert the furthest member. The old incremental update only max'd the
  // radius against the newly arriving alerts, so it reported ~57 m -- the spread
  // of the new group about the new centroid -- while the true bound is ~183 m.
  // The operator map draws that circle and the dispatcher sizes the response from
  // it, so under-reporting it is a safety-relevant error.
  {
    CorrelationConfig cfg;
    cfg.space_radius_m = 250.0;
    cfg.time_window_ms = 600000;
    Correlator c(cfg);

    c.ingest({at(1, LAT, lon_for(LON, 0), 1000)});
    c.ingest({at(2, LAT, lon_for(LON, 200), 2000), at(3, LAT, lon_for(LON, 210), 2000),
              at(4, LAT, lon_for(LON, 220), 2000), at(5, LAT, lon_for(LON, 230), 2000),
              at(6, LAT, lon_for(LON, 240), 2000)});

    t::ok(c.open_count() == 1, "the lone alert and the arriving group are one incident");
    const Incident* inc = c.open_incidents()[0];
    t::ok(inc->alerts.size() == 6, "with all six alerts");
    t::ok(inc->alert_positions.size() == inc->alerts.size(),
          "and a position recorded for each");

    // Every member must be inside the reported radius. The FIRST alert is the one
    // the old code left outside.
    double worst = 0.0;
    for (const auto& p : inc->alert_positions)
      worst = std::max(worst, geo::distance_m(inc->centroid, p));
    t::near(inc->radius_m, worst, 1e-6,
            "the radius equals the true distance to the furthest member");

    const double to_first = geo::distance_m(inc->centroid, {LAT, lon_for(LON, 0)});
    t::ok(inc->radius_m >= to_first - 1e-6,
          "the FIRST alert is inside the circle after the centroid moved toward the group");
    t::ok(to_first > 150.0,
          "and it really is the far one (" + std::to_string(int(to_first)) + " m out)");

    // The spread of the NEW group alone -- what the old code would have reported.
    double new_group_spread = 0.0;
    for (int i = 1; i < 6; ++i)
      new_group_spread = std::max(new_group_spread,
          geo::distance_m(inc->centroid, inc->alert_positions[size_t(i)]));
    t::ok(inc->radius_m > new_group_spread * 1.5,
          "the correct radius is far larger than the new-arrivals-only radius (" +
              std::to_string(int(inc->radius_m)) + " m vs " +
              std::to_string(int(new_group_spread)) + " m)");

    // The centroid is the mean of all members, not of the newest batch.
    double slat = 0, slon = 0;
    for (const auto& p : inc->alert_positions) { slat += p.lat; slon += p.lon; }
    t::near(inc->centroid.lat, slat / double(inc->alert_positions.size()), 1e-12,
            "centroid is the mean over every member (lat)");
    t::near(inc->centroid.lon, slon / double(inc->alert_positions.size()), 1e-12,
            "centroid is the mean over every member (lon)");
  }

  // ── severity and people-count aggregation ──────────────────────────────────
  {
    Correlator c;
    c.ingest({at(1, LAT, LON, 1000, 2), at(2, LAT, lon_for(LON, 30), 1000, 5),
              at(3, LAT, lon_for(LON, 60), 1000, 1)});
    const Incident* inc = c.open_incidents()[0];
    t::ok(inc->max_severity == 5, "the incident carries the worst member severity");
    t::ok(inc->people() == 3, "and counts distinct people");

    // The same tourist raising two alerts is one person.
    Correlator c2;
    Alert a1 = at(1, LAT, LON, 1000);
    Alert a2 = at(2, LAT, lon_for(LON, 30), 1000);
    a2.tourist = a1.tourist;
    c2.ingest({a1, a2});
    t::ok(c2.open_incidents()[0]->alerts.size() == 2, "two alerts");
    t::ok(c2.open_incidents()[0]->people() == 1, "but one affected person");
  }

  // ── compression, the headline number ───────────────────────────────────────
  {
    Correlator c;
    std::vector<Alert> mass;
    for (int i = 0; i < 40; ++i)
      mass.push_back(at(AlertId(i), LAT, lon_for(LON, i * 5.0), 1000));
    c.ingest(mass);
    t::ok(c.open_count() == 1, "40 co-located alerts become one operator card");
    t::ok(c.stats().alerts_ingested == 40, "40 alerts ingested");
    t::ok(c.stats().incidents_opened == 1, "1 incident opened");
    t::near(c.stats().compression_ratio(), 40.0, 1e-9, "compression ratio is 40:1");
  }

  return t::report("alert/incident_lifecycle");
}
