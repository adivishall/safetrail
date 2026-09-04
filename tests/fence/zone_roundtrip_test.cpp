// ZoneStore GeoJSON serialisation: load -> save -> load must be lossless.
//
// "Round-trip" was previously a claim the writer could not support -- it wrote
// name, severity and the outer ring only, so kind, dwell, validity and every hole
// were silently dropped. These assertions are field by field precisely so the
// claim cannot quietly rot again.
#include "../test_harness.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "safetrail/fence/zone.hpp"
#include "safetrail/util/json.hpp"

using namespace safetrail;
using namespace safetrail::fence;

static std::string tmp(const char* name) {
  return std::string("build/test_tmp_") + name;
}

static geo::Ring square(double lat, double lon, double half) {
  return {{lat - half, lon - half}, {lat - half, lon + half},
          {lat + half, lon + half}, {lat + half, lon - half}};
}

static bool same_ring(const geo::Ring& a, const geo::Ring& b, double tol = 1e-9) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::fabs(a[i].lat - b[i].lat) > tol || std::fabs(a[i].lon - b[i].lon) > tol)
      return false;
  return true;
}

int main() {
  // A zone exercising every field the loader understands, plus a hole and a name
  // full of characters that break a naive string concatenation.
  Zone z;
  z.name = "Nohkalikai \"Falls\" \\ Trail\nsecond line\ttab";
  z.kind = ZoneKind::Restricted;
  z.severity = 5;
  z.max_dwell_ms = 90'000;
  z.validity = index::Validity{3'600'000, 7'200'000};
  z.jurisdiction = 7;
  z.enter_margin_m = 12.5;
  z.exit_margin_m = 31.25;
  z.shape = geo::Polygon(square(25.55, 91.88, 0.01));
  z.shape.add_hole(square(25.55, 91.88, 0.002));

  Zone plain;                                    // all defaults, no hole
  plain.name = "Ward's Lake";
  plain.kind = ZoneKind::Safe;
  plain.severity = 1;
  plain.shape = geo::Polygon(square(25.57, 91.89, 0.004));

  ZoneStore a;
  a.add(z);
  a.add(plain);

  const std::string path = tmp("zones.geojson");
  t::ok(a.save_geojson(path), "save_geojson writes");

  ZoneStore b;
  std::string err;
  t::ok(b.load_geojson(path, &err), "the file we wrote loads back: " + err);
  t::ok(b.size() == a.size(), "same zone count");

  const Zone* za = a.get(0);
  const Zone* zb = b.get(0);
  t::ok(za && zb, "zone 0 present on both sides");
  if (za && zb) {
    t::ok(zb->name == za->name, "name survives quotes, backslash, newline and tab");
    t::ok(zb->kind == za->kind, "kind survives (was silently reset to Caution)");
    t::ok(zb->severity == za->severity, "severity survives");
    t::ok(zb->max_dwell_ms == za->max_dwell_ms, "dwell limit survives");
    t::ok(zb->validity.from == za->validity.from, "validity.from survives");
    t::ok(zb->validity.to == za->validity.to, "validity.to survives");
    t::ok(zb->jurisdiction == za->jurisdiction, "jurisdiction survives");
    t::near(zb->enter_margin_m, za->enter_margin_m, 1e-9, "enter margin survives");
    t::near(zb->exit_margin_m, za->exit_margin_m, 1e-9, "exit margin survives");
    t::ok(same_ring(zb->shape.outer(), za->shape.outer()), "outer ring survives exactly");
    t::ok(zb->shape.holes().size() == 1, "the hole survives (was silently dropped)");
    if (zb->shape.holes().size() == 1)
      t::ok(same_ring(zb->shape.holes()[0], za->shape.holes()[0]), "hole ring survives exactly");
  }

  const Zone* pa = a.get(1);
  const Zone* pb = b.get(1);
  if (pa && pb) {
    t::ok(pb->name == pa->name, "apostrophe in a name needs no escaping and survives");
    t::ok(pb->kind == pa->kind, "Safe kind survives");
    t::ok(pb->validity.to == kForever, "an omitted validity window reloads as always-on");
    t::ok(pb->max_dwell_ms == 0, "an omitted dwell limit reloads as no limit");
  }

  // Second generation: save what we loaded, load again, and require the file
  // itself to be byte-identical. Idempotence is a stronger statement than field
  // equality -- it rules out a field that is merely reformatted each pass.
  const std::string path2 = tmp("zones2.geojson");
  t::ok(b.save_geojson(path2), "second save writes");
  {
    std::FILE* f1 = std::fopen(path.c_str(), "rb");
    std::FILE* f2 = std::fopen(path2.c_str(), "rb");
    std::string s1, s2;
    char buf[4096];
    size_t n;
    while (f1 && (n = std::fread(buf, 1, sizeof buf, f1)) > 0) s1.append(buf, n);
    while (f2 && (n = std::fread(buf, 1, sizeof buf, f2)) > 0) s2.append(buf, n);
    if (f1) std::fclose(f1);
    if (f2) std::fclose(f2);
    t::ok(!s1.empty() && s1 == s2, "save -> load -> save is byte-identical");
  }

  // The writer must emit parseable JSON, not merely something our loader tolerates.
  {
    util::Json doc;
    std::string perr;
    t::ok(util::Json::parse_file(path, doc, &perr),
          "the emitted file is valid JSON by the parser's own strict rules: " + perr);
    const util::Json* feats = doc.find("features");
    t::ok(feats && feats->arr.size() == 2, "two features in the FeatureCollection");
    if (feats && feats->arr.size() == 2) {
      const util::Json* geom = feats->arr[0].find("geometry");
      const util::Json* coords = geom ? geom->find("coordinates") : nullptr;
      t::ok(coords && coords->arr.size() == 2, "outer ring plus one hole ring");
      if (coords && !coords->arr.empty()) {
        const auto& ring = coords->arr[0].arr;
        // GeoJSON rings are closed: first vertex repeated at the end.
        t::ok(ring.size() == 5, "4-vertex ring written as 5 coordinates (closed)");
        t::ok(ring.front().arr[0].num == ring.back().arr[0].num &&
              ring.front().arr[1].num == ring.back().arr[1].num,
              "the closing vertex equals the first");
        // [lon, lat] order -- the classic import bug.
        t::ok(ring[0].arr[0].num > 91.0 && ring[0].arr[1].num > 25.0,
              "coordinates written as [lon, lat]");
      }
    }
  }

  // Invalid geometry is rejected on load rather than stored.
  {
    const std::string bad = tmp("bad.geojson");
    std::FILE* f = std::fopen(bad.c_str(), "w");
    // A bowtie: self-intersecting, and with zero signed area, so it also pins
    // that self-intersection is diagnosed BEFORE degeneracy.
    std::fprintf(f, "%s",
                 R"({"type":"FeatureCollection","features":[{"type":"Feature",)"
                 R"("properties":{"name":"bowtie"},"geometry":{"type":"Polygon",)"
                 R"("coordinates":[[[0,0],[1,1],[1,0],[0,1],[0,0]]]}}]})");
    std::fclose(f);
    ZoneStore c;
    std::string e2;
    t::ok(!c.load_geojson(bad, &e2), "a self-intersecting zone is refused");
    t::ok(e2.find("self-intersecting") != std::string::npos,
          "and the error says why: " + e2);
    std::remove(bad.c_str());
  }

  std::remove(path.c_str());
  std::remove(path2.c_str());
  return t::report("fence/zone_roundtrip");
}
