#include "safetrail/index/geohash.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "safetrail/util/bytes.hpp"

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

// The query pads by the largest item half-extent, so this value directly sets how
// much of the key range every query scans. Growing it on insert is required for
// correctness; never shrinking it on removal is a pruning leak -- delete the one
// district-sized zone from a set of 50 m ones and every subsequent query keeps
// scanning a district-wide key range forever, for no candidates.
void Geohash::note_extent(const geo::Bbox& b) {
  max_half_lat_ = std::max(max_half_lat_, (b.max_lat - b.min_lat) * 0.5);
  max_half_lon_ = std::max(max_half_lon_, (b.max_lon - b.min_lon) * 0.5);
}

// Exact recomputation, O(n). Affordable precisely because it is only called from
// remove(), which is already O(n) (a linear id scan plus a vector erase), so the
// policy costs nothing asymptotically and needs no heuristic threshold to decide
// when to run. bench/results/index_churn.csv measures what it buys.
void Geohash::recompute_extents() {
  max_half_lat_ = max_half_lon_ = 0.0;
  for (const auto& r : recs_) {
    max_half_lat_ = std::max(max_half_lat_, (r.box.max_lat - r.box.min_lat) * 0.5);
    max_half_lon_ = std::max(max_half_lon_, (r.box.max_lon - r.box.min_lon) * 0.5);
  }
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
    if (it->id == id) {
      recs_.erase(it);                          // erase keeps the sort order
      recompute_extents();                      // see note_extent()
      return true;
    }
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


IndexStats Geohash::stats() const {
  st_.node_count = recs_.size();
  st_.max_depth  = kBits;
  st_.bytes      = kHeaderBytes + recs_.size() * kRecBytes;
  return st_;
}

// ── Serialisation  [GAP 6] ────────────────────────────────────────────────────
// Explicitly little-endian, via util/bytes.hpp -- shifts, not memcpy of a native
// integer, so the layout is the same on every host rather than the same on every
// host we happened to test. Layout:
//   [magic u32]["GEO2"][count u32][max_half_lat f64][max_half_lon f64]
//   count x { key u64, id u32, min_lat/min_lon/max_lat/max_lon f64 }
namespace {
constexpr uint32_t kMagic = 0x4F454732;   // "2GEO" little-endian == "GEO2" on disk
}  // namespace

bool Geohash::serialize(std::vector<uint8_t>& out) const {
  out.clear();
  out.reserve(kHeaderBytes + recs_.size() * kRecBytes);
  util::put_u32(out, kMagic);
  util::put_u32(out, uint32_t(recs_.size()));
  util::put_f64(out, max_half_lat_);
  util::put_f64(out, max_half_lon_);
  for (const auto& r : recs_) {
    util::put_u64(out, r.key);
    util::put_u32(out, uint32_t(r.id));
    util::put_f64(out, r.box.min_lat); util::put_f64(out, r.box.min_lon);
    util::put_f64(out, r.box.max_lat); util::put_f64(out, r.box.max_lon);
  }
  return true;
}

// Rejects, rather than tolerates: a wrong magic, a truncated body, a count that
// the remaining bytes cannot possibly satisfy, coordinates that are not finite,
// a key array that is not sorted, and TRAILING GARBAGE. The last one matters as
// much as the others -- a blob with extra bytes on the end is a concatenation or
// a partial overwrite, and silently loading its prefix is how a device ends up
// evaluating half a district's zones and reporting no error.
//
// The parse builds into a local and only commits on success, so a malformed blob
// leaves the existing index intact instead of half-replaced.
bool Geohash::deserialize(const std::vector<uint8_t>& in) {
  util::Reader r(in);
  uint32_t magic = 0, count = 0;
  if (!r.u32(&magic) || magic != kMagic) return false;
  if (!r.u32(&count)) return false;

  double half_lat = 0, half_lon = 0;
  if (!r.f64(&half_lat) || !r.f64(&half_lon)) return false;
  if (!std::isfinite(half_lat) || !std::isfinite(half_lon)) return false;

  // Refuse an impossible count before reserving for it. Without this a corrupt
  // 4-billion count field turns a 40-byte file into an out-of-memory abort.
  if (r.remaining() != size_t(count) * kRecBytes) return false;

  std::vector<Rec> loaded;
  loaded.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Rec rec{};
    uint32_t id = 0;
    if (!r.u64(&rec.key) || !r.u32(&id)) return false;
    rec.id = id;
    if (!r.f64(&rec.box.min_lat) || !r.f64(&rec.box.min_lon) ||
        !r.f64(&rec.box.max_lat) || !r.f64(&rec.box.max_lon)) return false;
    if (!std::isfinite(rec.box.min_lat) || !std::isfinite(rec.box.min_lon) ||
        !std::isfinite(rec.box.max_lat) || !std::isfinite(rec.box.max_lon)) return false;
    if (rec.box.min_lat > rec.box.max_lat || rec.box.min_lon > rec.box.max_lon) return false;
    loaded.push_back(rec);
  }
  if (!r.at_end()) return false;                    // trailing garbage

  // Trust but verify: the on-disk array is supposed to be key-sorted, since the
  // range query is a binary search over it.
  if (!std::is_sorted(loaded.begin(), loaded.end(),
                      [](const Rec& a, const Rec& b) { return a.key < b.key; }))
    return false;

  recs_ = std::move(loaded);
  max_half_lat_ = half_lat;
  max_half_lon_ = half_lon;
  return true;
}

}  // namespace safetrail::index
