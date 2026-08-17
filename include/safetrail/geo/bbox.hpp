#pragma once
// Axis-aligned bounding box in geographic coordinates.
//
// The index stores only boxes, never polygons. That is the standard
// filter-then-refine split: the index gives a conservative superset cheaply, and
// the caller runs exact geometry on the survivors. Keeping this type trivial and
// branch-light matters — it is touched millions of times per benchmark run.
#include <algorithm>
#include "safetrail/geo/point.hpp"

namespace safetrail::geo {

struct Bbox {
  double min_lat = 0, min_lon = 0, max_lat = 0, max_lon = 0;

  static Bbox empty();
  static Bbox around(const LatLon& c, double radius_m);
  static Bbox of(const LatLon* pts, size_t n);

  bool contains(const LatLon& p) const {
    return p.lat >= min_lat && p.lat <= max_lat &&
           p.lon >= min_lon && p.lon <= max_lon;
  }
  bool intersects(const Bbox& o) const {
    return !(o.min_lat > max_lat || o.max_lat < min_lat ||
             o.min_lon > max_lon || o.max_lon < min_lon);
  }
  void expand(const LatLon& p) {
    min_lat = std::min(min_lat, p.lat);  max_lat = std::max(max_lat, p.lat);
    min_lon = std::min(min_lon, p.lon);  max_lon = std::max(max_lon, p.lon);
  }
  void expand(const Bbox& o) {
    min_lat = std::min(min_lat, o.min_lat);  max_lat = std::max(max_lat, o.max_lat);
    min_lon = std::min(min_lon, o.min_lon);  max_lon = std::max(max_lon, o.max_lon);
  }

  LatLon center() const { return {(min_lat + max_lat) / 2, (min_lon + max_lon) / 2}; }
  double area() const { return (max_lat - min_lat) * (max_lon - min_lon); }

  // Used by the R-tree split heuristic and by nearest() ordering.
  double enlargement_to_include(const Bbox& o) const;
  double min_distance_m(const LatLon& p) const;
};

}  // namespace safetrail::geo
