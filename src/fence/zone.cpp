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

bool ZoneStore::save_geojson(const std::string& path) const {
  std::ofstream f(path);
  if (!f) return false;
  f << "{\"type\":\"FeatureCollection\",\"features\":[";
  bool first = true;
  for (const auto& z : zones_) {
    if (z.id == kNoId) continue;
    if (!first) f << ",";
    first = false;
    f << "{\"type\":\"Feature\",\"properties\":{\"name\":\"" << z.name
      << "\",\"severity\":" << int(z.severity)
      << "},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[";
    const auto& r = z.shape.outer();
    for (size_t i = 0; i < r.size(); ++i)
      f << (i ? "," : "") << "[" << r[i].lon << "," << r[i].lat << "]";
    f << "]]}}";
  }
  f << "]}";
  return true;
}

}  // namespace safetrail::fence
