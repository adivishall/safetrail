#include "safetrail/index/geohash.hpp"

#include <algorithm>
#include <cstring>

namespace safetrail::index {

// Spread the low 24 bits of v so bit i lands at position 2i -- the interleave step.
// A plain loop: called only at build/query time (never per candidate), so clarity
// beats the branchless magic-mask trick, which is easy to get subtly wrong for a
// 24-bit width.
uint64_t Geohash::spread(uint32_t v) {
  uint64_t x = 0;
  for (int i = 0; i < kBits; ++i)
    if (v & (1u << i)) x |= (uint64_t(1) << (2 * i));
  return x;
}

uint64_t Geohash::morton(const geo::LatLon& p) {
  double flat = (p.lat - kLatMin) / kLatSpan;   // -> [0,1]
  double flon = (p.lon - kLonMin) / kLonSpan;
  if (flat < 0) flat = 0; else if (flat > 1) flat = 1;
  if (flon < 0) flon = 0; else if (flon > 1) flon = 1;
  const uint32_t maxq = (1u << kBits) - 1u;
  const uint32_t qlat = uint32_t(flat * maxq);
  const uint32_t qlon = uint32_t(flon * maxq);
  return (spread(qlat) << 1) | spread(qlon);
}

void Geohash::note_extent(const geo::Bbox& b) {
  max_half_lat_ = std::max(max_half_lat_, (b.max_lat - b.min_lat) * 0.5);
  max_half_lon_ = std::max(max_half_lon_, (b.max_lon - b.min_lon) * 0.5);
}

void Geohash::build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  recs_.clear();
  max_half_lat_ = max_half_lon_ = 0.0;
  recs_.reserve(items.size());
  for (const auto& it : items) {
    recs_.push_back(Rec{morton(it.second.center()), it.first, it.second});
    note_extent(it.second);
  }
  std::sort(recs_.begin(), recs_.end(), [](const Rec& a, const Rec& b) { return a.key < b.key; });
}

void Geohash::insert(ZoneId id, const geo::Bbox& box) {
  Rec r{morton(box.center()), id, box};
  auto it = std::upper_bound(recs_.begin(), recs_.end(), r,
                             [](const Rec& a, const Rec& b) { return a.key < b.key; });
  recs_.insert(it, r);
  note_extent(box);
}

bool Geohash::remove(ZoneId id) {
  for (auto it = recs_.begin(); it != recs_.end(); ++it)
    if (it->id == id) { recs_.erase(it); return true; }   // erase keeps the sort order
  return false;
}

void Geohash::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  if (recs_.empty()) return;

  // Pad the query by the largest item half-extent so a box whose centre lies just
  // outside the query but whose body overlaps is still inside the scanned range.
  const geo::LatLon lo{q.min_lat - max_half_lat_, q.min_lon - max_half_lon_};
  const geo::LatLon hi{q.max_lat + max_half_lat_, q.max_lon + max_half_lon_};
  const uint64_t z_lo = morton(lo), z_hi = morton(hi);

  auto begin = std::lower_bound(recs_.begin(), recs_.end(), z_lo,
                                [](const Rec& r, uint64_t k) { return r.key < k; });
  auto end   = std::upper_bound(recs_.begin(), recs_.end(), z_hi,
                                [](uint64_t k, const Rec& r) { return k < r.key; });
  for (auto it = begin; it != end; ++it) {
    ++st_.candidates_returned;
    if (it->box.intersects(q)) out.push_back(it->id);
  }
}

void Geohash::nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const {
  // Linear by box min-distance. The geohash's headline is range query and
  // serialisation; NN falls back to a scan (the k-d tree is the NN structure).
  std::vector<std::pair<double, ZoneId>> d;
  d.reserve(recs_.size());
  for (const auto& r : recs_) d.emplace_back(r.box.min_distance_m(p), r.id);
  if (k < d.size())
    std::partial_sort(d.begin(), d.begin() + long(k), d.end());
  else
    std::sort(d.begin(), d.end());
  for (size_t i = 0; i < k && i < d.size(); ++i) out.push_back(d[i].second);
}

IndexStats Geohash::stats() const {
  st_.node_count = recs_.size();
  st_.max_depth  = kBits;
  st_.bytes      = kHeaderBytes + recs_.size() * kRecBytes;
  return st_;
}

// ── Serialisation  [GAP 6] ────────────────────────────────────────────────────
// Fixed little-endian layout so a blob written on one machine loads on another:
//   [magic u32][count u32][max_half_lat f64][max_half_lon f64]
//   count x { key u64, id u32, min_lat/min_lon/max_lat/max_lon f64 }
namespace {
constexpr uint32_t kMagic = 0x47454F31;   // "GEO1"
template <typename T> void put(std::vector<uint8_t>& b, T v) {
  const auto* p = reinterpret_cast<const uint8_t*>(&v);
  b.insert(b.end(), p, p + sizeof(T));
}
template <typename T> bool get(const std::vector<uint8_t>& b, size_t& off, T& v) {
  if (off + sizeof(T) > b.size()) return false;
  std::memcpy(&v, b.data() + off, sizeof(T));
  off += sizeof(T);
  return true;
}
}  // namespace

bool Geohash::serialize(std::vector<uint8_t>& out) const {
  out.clear();
  put(out, kMagic);
  put(out, uint32_t(recs_.size()));
  put(out, max_half_lat_);
  put(out, max_half_lon_);
  for (const auto& r : recs_) {
    put(out, r.key);
    put(out, uint32_t(r.id));
    put(out, r.box.min_lat); put(out, r.box.min_lon);
    put(out, r.box.max_lat); put(out, r.box.max_lon);
  }
  return true;
}

bool Geohash::deserialize(const std::vector<uint8_t>& in) {
  size_t off = 0;
  uint32_t magic = 0, count = 0;
  if (!get(in, off, magic) || magic != kMagic) return false;
  if (!get(in, off, count)) return false;
  if (!get(in, off, max_half_lat_) || !get(in, off, max_half_lon_)) return false;
  recs_.clear();
  recs_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Rec r{};
    uint32_t id = 0;
    if (!get(in, off, r.key) || !get(in, off, id)) return false;
    r.id = id;
    if (!get(in, off, r.box.min_lat) || !get(in, off, r.box.min_lon) ||
        !get(in, off, r.box.max_lat) || !get(in, off, r.box.max_lon)) return false;
    recs_.push_back(r);
  }
  // Trust but verify: the on-disk array is supposed to be key-sorted.
  return std::is_sorted(recs_.begin(), recs_.end(),
                        [](const Rec& a, const Rec& b) { return a.key < b.key; });
}

}  // namespace safetrail::index
