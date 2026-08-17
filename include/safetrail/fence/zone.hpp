#pragma once
// Zones and their storage.
#include <string>
#include <vector>
#include "safetrail/geo/polygon.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/types.hpp"

namespace safetrail::fence {

using safetrail::ZoneId;

enum class ZoneKind { Restricted, Caution, Safe, Advisory };

struct Zone {
  ZoneId       id = kNoId;
  ZoneKind     kind = ZoneKind::Caution;
  uint8_t      severity = 1;             // 1..5
  std::string  name;
  geo::Polygon shape;

  // Jurisdiction that owns alerts raised here. Resolved from the administrative
  // boundary hierarchy.  [GAP 11]
  uint32_t jurisdiction = kNoId;

  // Dwell limit: inside longer than this raises DwellExceeded. 0 = no limit.
  Millis max_dwell_ms = 0;

  // GAP 3. When this zone is actually in force. A river crossing is safe in dry
  // season and lethal after rain; a mountain road closes at dusk. Those are the
  // same zone with different validity, not different zones -- and an incident
  // investigation needs to know which rule applied at the time.
  index::Validity validity{};

  // Per-zone hysteresis override. A hard border buffer wants tight margins and
  // fast confirmation; a broad landslide advisory wants the opposite.
  double enter_margin_m = 0.0;   // 0 = inherit the global config
  double exit_margin_m  = 0.0;
};

// Flat storage indexed by ZoneId. The spatial index holds only ids and boxes and
// resolves through this, so geometry lives in exactly one place.
class ZoneStore {
 public:
  ZoneId add(Zone z);
  bool   remove(ZoneId id);
  const Zone* get(ZoneId id) const;
  Zone*       get_mut(ZoneId id);
  size_t size() const;

  std::vector<ZoneId> all_ids() const;

  // GeoJSON round-trip. Validates on load and REJECTS invalid geometry rather
  // than storing it — a self-intersecting polygon in the store is a landmine.
  // See Polygon::validate(), GAP 10.
  bool load_geojson(const std::string& path, std::string* error);
  bool save_geojson(const std::string& path) const;

 private:
  std::vector<Zone> zones_;
};

}  // namespace safetrail::fence
