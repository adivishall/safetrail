#include "safetrail/fence/zone.hpp"
#include "safetrail/util/json.hpp"
#include <cmath>
#include <fstream>

namespace safetrail::fence {

ZoneId ZoneStore::add(Zone z) {
  z.id = ZoneId(zones_.size());
  zones_.push_back(std::move(z));
  return zones_.back().id;
}
bool ZoneStore::remove(ZoneId id) {
  if (id >= zones_.size()) return false;
  zones_[id].id = kNoId;                 // tombstone: ids stay stable as indices
  return true;
}
const Zone* ZoneStore::get(ZoneId id) const {
  return id < zones_.size() && zones_[id].id != kNoId ? &zones_[id] : nullptr;
}
Zone* ZoneStore::get_mut(ZoneId id) {
  return id < zones_.size() && zones_[id].id != kNoId ? &zones_[id] : nullptr;
}
size_t ZoneStore::size() const { return zones_.size(); }

std::vector<ZoneId> ZoneStore::all_ids() const {
  std::vector<ZoneId> v;
  for (const auto& z : zones_) if (z.id != kNoId) v.push_back(z.id);
  return v;
}

static void strip_closing_vertex(geo::Ring& r) {
  // GeoJSON rings repeat the first vertex to close the ring. Our geometry treats
  // rings as implicitly closed, so keeping it creates a zero-length edge -- which
  // then trips the self-intersection check on every otherwise-valid polygon.
  if (r.size() >= 2) {
    const auto& f = r.front();
    const auto& l = r.back();
    if (std::fabs(f.lat - l.lat) < 1e-12 && std::fabs(f.lon - l.lon) < 1e-12) r.pop_back();
  }
}

static ZoneKind kind_from(const std::string& s) {
  if (s == "restricted") return ZoneKind::Restricted;
  if (s == "safe") return ZoneKind::Safe;
  if (s == "advisory") return ZoneKind::Advisory;
  return ZoneKind::Caution;
}

const char* to_string(ZoneKind k) {
  switch (k) {
    case ZoneKind::Restricted: return "restricted";
    case ZoneKind::Caution: return "caution";
    case ZoneKind::Safe: return "safe";
    case ZoneKind::Advisory: return "advisory";
  }
  return "caution";
}

// GeoJSON FeatureCollection. Note the coordinate order: GeoJSON is [lon, lat],
// which is the reverse of how everyone says it out loud. Getting this backwards
// puts Meghalaya in the Indian Ocean and is the single most common import bug.
bool ZoneStore::load_geojson(const std::string& path, std::string* error) {
  util::Json root;
  if (!util::Json::parse_file(path, root, error)) return false;
  const util::Json* feats = root.find("features");
  if (!feats || feats->type != util::Json::Type::Array) {
    if (error) *error = "no features array"; return false;
  }
  for (const util::Json& f : feats->arr) {
    const util::Json* geom = f.find("geometry");
    const util::Json* props = f.find("properties");
    if (!geom) continue;
    const util::Json* coords = geom->find("coordinates");
    if (!coords || coords->type != util::Json::Type::Array || coords->arr.empty()) continue;

    Zone z;
    if (props) {
      if (const util::Json* n = props->find("name")) z.name = n->string_or("");
      if (const util::Json* k = props->find("kind")) z.kind = kind_from(k->string_or("caution"));
      if (const util::Json* s = props->find("severity")) z.severity = uint8_t(s->number_or(1));
      if (const util::Json* d = props->find("max_dwell_s"))
        z.max_dwell_ms = Millis(d->number_or(0) * 1000);
      // GAP 3: seconds-into-the-scenario, so demo files stay readable.
      if (const util::Json* a = props->find("active_from_s"))
        z.validity.from = Timestamp(a->number_or(0) * 1000);
      if (const util::Json* b = props->find("active_to_s"))
        z.validity.to = Timestamp(b->number_or(0) * 1000);
      if (const util::Json* sy = props->find("synthetic")) z.synthetic = sy->bool_or(false);
      if (const util::Json* j = props->find("jurisdiction"))
        z.jurisdiction = uint32_t(j->number_or(double(kNoId)));
      if (const util::Json* e = props->find("enter_margin_m"))
        z.enter_margin_m = e->number_or(0.0);
      if (const util::Json* e = props->find("exit_margin_m"))
        z.exit_margin_m = e->number_or(0.0);
    }

    geo::Ring outer;
    for (const util::Json& pt : coords->arr[0].arr)
      if (pt.arr.size() >= 2) outer.push_back({pt.arr[1].num, pt.arr[0].num});   // [lon,lat]
    strip_closing_vertex(outer);
    if (outer.size() < 3) continue;
    z.shape = geo::Polygon(std::move(outer));
    for (size_t h = 1; h < coords->arr.size(); ++h) {
      geo::Ring hole;
      for (const util::Json& pt : coords->arr[h].arr)
        if (pt.arr.size() >= 2) hole.push_back({pt.arr[1].num, pt.arr[0].num});
      strip_closing_vertex(hole);
      if (hole.size() >= 3) z.shape.add_hole(std::move(hole));
    }

    // GAP 10: reject invalid geometry rather than storing it. A self-intersecting
    // polygon in the store makes ray casting return arbitrary results downstream.
    const auto v = z.shape.validate();
    if (v != geo::Polygon::Validity::Ok) {
      if (error) *error = "zone '" + z.name + "': " + geo::Polygon::to_string(v);
      return false;
    }
    add(std::move(z));
  }
  return true;
}

// The matching writer.
//
// "Round-trip" is a claim, so it is worth stating what it covers: every field the
// loader above understands -- name, kind, severity, dwell limit, validity window,
// synthetic flag, jurisdiction, per-zone hysteresis margins, the outer ring AND
// every hole -- is written here, and load->save->load produces a semantically
// identical store. tests/fence/zone_roundtrip_test.cpp asserts that field by
// field rather than trusting this comment.
//
// The previous version wrote only name, severity and the outer ring. Everything
// else -- kind, dwell, validity, holes -- was silently dropped, so saving a zone
// set and reloading it turned every restricted zone into a caution zone with no
// time window and no exempt enclave. That is a data-loss bug wearing the costume
// of a serialiser.
//
// Two details that are easy to get wrong and are the reason a naive
// implementation produces files its own loader rejects:
//   - GeoJSON coordinate order is [lon, lat].
//   - GeoJSON rings are CLOSED: the first vertex is repeated at the end. Our
//     rings are implicitly closed, so the closing vertex is added on write and
//     stripped on read.
bool ZoneStore::save_geojson(const std::string& path) const {
  std::ofstream f(path);
  if (!f) return false;

  auto write_ring = [&](const geo::Ring& r) {
    f << "[";
    for (size_t i = 0; i < r.size(); ++i)
      f << (i ? "," : "") << "[" << util::Json::number(r[i].lon) << ","
        << util::Json::number(r[i].lat) << "]";
    if (!r.empty())                                   // repeat the first vertex
      f << ",[" << util::Json::number(r[0].lon) << "," << util::Json::number(r[0].lat) << "]";
    f << "]";
  };

  f << "{\"type\":\"FeatureCollection\",\"features\":[";
  bool first = true;
  for (const auto& z : zones_) {
    if (z.id == kNoId) continue;
    if (!first) f << ",";
    first = false;

    f << "{\"type\":\"Feature\",\"properties\":{"
      << "\"name\":" << util::Json::escape(z.name)
      << ",\"kind\":\"" << to_string(z.kind) << "\""
      << ",\"severity\":" << int(z.severity)
      << ",\"synthetic\":" << (z.synthetic ? "true" : "false");
    if (z.max_dwell_ms > 0)
      f << ",\"max_dwell_s\":" << util::Json::number(double(z.max_dwell_ms) / 1000.0);
    // Validity is written only when it is not the always-on default, so an
    // ordinary zone file stays readable and a diff stays small.
    if (z.validity.from != 0)
      f << ",\"active_from_s\":" << util::Json::number(double(z.validity.from) / 1000.0);
    if (z.validity.to != kForever)
      f << ",\"active_to_s\":" << util::Json::number(double(z.validity.to) / 1000.0);
    if (z.jurisdiction != kNoId)
      f << ",\"jurisdiction\":" << z.jurisdiction;
    if (z.enter_margin_m > 0)
      f << ",\"enter_margin_m\":" << util::Json::number(z.enter_margin_m);
    if (z.exit_margin_m > 0)
      f << ",\"exit_margin_m\":" << util::Json::number(z.exit_margin_m);
    f << "},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[";
    write_ring(z.shape.outer());
    for (const auto& h : z.shape.holes()) { f << ","; write_ring(h); }
    f << "]}}";
  }
  f << "]}";
  return bool(f);
}

}  // namespace safetrail::fence
