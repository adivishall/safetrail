#pragma once
// Geohash index -- Z-order (Morton) curve over a sorted array.
//
// The third spatial index, and the one that doubles as the offline serialisation
// format (GAP 6). The idea: interleave the bits of the quantised latitude and
// longitude into a single 48-bit key (a Morton code). Points close in 2-D are
// usually close along the 1-D Z-order curve, so a bounding-box query becomes a
// contiguous key range plus a refine step.
//
// Correctness rests on one property of bit interleaving: if a point's cell is
// component-wise between two corner cells, its Morton key is between the corner
// keys. So every box that intersects the query has its centre inside the query
// box expanded by the largest item half-extent, and therefore its key inside the
// Morton range of that expanded box. We scan that key range (binary search) and
// bounding-box-filter the survivors -- filter-then-refine, exactly like the other
// indexes, and it returns identically to brute force (asserted in the equivalence
// test).
//
// Serialisation is why this index exists for the offline story: the sorted key
// array is a flat, endian-fixed blob that ships to a device and is queried with
// zero server round-trips -- the thing PostGIS cannot do in low-connectivity
// terrain.
#include <cstdint>
#include <vector>
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

class Geohash final : public SpatialIndex {
 public:
  const char* name() const override { return "geohash"; }

  void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) override;
  void insert(ZoneId id, const geo::Bbox& box) override;
  bool remove(ZoneId id) override;
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override;

  size_t size() const override { return recs_.size(); }

  // The half-extents the query pads by. Exposed so the churn benchmark can show
  // the pruning leak that recompute-on-remove closes.
  double query_pad_lat() const { return max_half_lat_; }
  double query_pad_lon() const { return max_half_lon_; }
  IndexStats stats() const override;
  void reset_counters() override { st_ = IndexStats{}; }

  bool serialize(std::vector<uint8_t>& out) const override;
  bool deserialize(const std::vector<uint8_t>& in) override;

  // Exposed for the encoding unit test.
  static uint64_t morton(const geo::LatLon& p);

 private:
  static constexpr int      kBits = 24;                 // bits per axis -> 48-bit key
  static constexpr double   kLatMin = -90.0,  kLatSpan = 180.0;
  static constexpr double   kLonMin = -180.0, kLonSpan = 360.0;
  static constexpr size_t   kHeaderBytes = 24;          // magic+count+2 doubles
  static constexpr size_t   kRecBytes    = 44;          // key+id+4 doubles

  struct Rec { uint64_t key; ZoneId id; geo::Bbox box; };

  std::vector<Rec> recs_;                 // always kept sorted by Morton key
  double           max_half_lat_ = 0.0;   // largest item half-extent, for query padding
  double           max_half_lon_ = 0.0;
  mutable IndexStats st_{};

  void note_extent(const geo::Bbox& b);   // grow the query-padding half-extents
  void recompute_extents();               // exact recompute after a removal
  static uint64_t spread(uint32_t v);     // spread 24 bits into every other bit
};

}  // namespace safetrail::index
