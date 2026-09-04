// Binary serialisation: round-trip, and refusal of malformed input.
//
// The geohash blob is the offline story (GAP 6) -- it is written on one machine
// and read on another, possibly after a device was unplugged mid-write. So the
// interesting half of this file is not that a good blob loads; it is that every
// kind of bad blob is REFUSED, and refused without corrupting the index that was
// already loaded.
#include "../test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/sim/mobility.hpp"
#include "safetrail/util/bytes.hpp"

using namespace safetrail;
using namespace safetrail::index;

static geo::Bbox box_at(double lat, double lon, double r) {
  return {lat - r, lon - r, lat + r, lon + r};
}

int main() {
  sim::Rng rng(7);
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  for (ZoneId i = 0; i < 300; ++i)
    items.emplace_back(i, box_at(rng.range(25.50, 25.62), rng.range(91.80, 91.96),
                                 rng.range(0.0003, 0.003)));

  Geohash src;
  src.build(items);
  std::vector<uint8_t> blob;
  t::ok(src.serialize(blob), "serialize succeeds");
  t::ok(!blob.empty(), "and produces bytes");

  // ── round trip ─────────────────────────────────────────────────────────────
  {
    Geohash dst;
    t::ok(dst.deserialize(blob), "a good blob loads");
    t::ok(dst.size() == src.size(), "same item count");
    t::near(dst.query_pad_lat(), src.query_pad_lat(), 1e-12, "query padding survives");
    t::near(dst.query_pad_lon(), src.query_pad_lon(), 1e-12, "query padding survives (lon)");

    BruteForceIndex bf;
    bf.build(items);
    size_t bad = 0;
    for (int i = 0; i < 100; ++i) {
      const geo::Bbox q = geo::Bbox::around(
          {rng.range(25.49, 25.63), rng.range(91.79, 91.97)}, rng.range(150, 2500));
      std::vector<ZoneId> a, b;
      bf.query(q, a);
      dst.query(q, b);
      std::sort(a.begin(), a.end());
      std::sort(b.begin(), b.end());
      if (a != b) ++bad;
    }
    t::ok(bad == 0, "the reloaded index answers identically to brute force");

    // Re-serialising must reproduce the same bytes: the format has no free
    // choices left in it.
    std::vector<uint8_t> again;
    dst.serialize(again);
    t::ok(again == blob, "serialize -> deserialize -> serialize is byte-identical");
  }

  // ── the layout is explicitly little-endian ─────────────────────────────────
  //
  // Not "little-endian on this machine". The first four bytes are the magic, and
  // reading them back with the shift-based decoder must give the same number the
  // encoder was asked for -- which is a statement about the FILE, not about the
  // host. This is the assertion that would have caught the old memcpy-based
  // implementation on any big-endian target.
  {
    util::Reader r(blob);
    uint32_t magic = 0, count = 0;
    t::ok(r.u32(&magic), "magic reads");
    t::ok(magic == 0x4F454732u, "magic is the documented constant");
    t::ok(r.u32(&count) && count == src.size(), "count field matches the item count");
    // Byte order is stated, not inferred: the low byte comes first.
    t::ok(blob[0] == uint8_t(0x32) && blob[1] == uint8_t(0x47),
          "the magic's low byte is written first (little-endian on disk)");

    std::vector<uint8_t> rt;
    util::put_f64(rt, 25.5701234567891);
    util::Reader rr(rt);
    double back = 0;
    t::ok(rr.f64(&back) && back == 25.5701234567891,
          "a double survives the byte codec exactly");
    t::ok(rt.size() == 8, "and occupies exactly 8 bytes");
  }

  // ── malformed input is refused, and refuses non-destructively ──────────────
  {
    struct Case { std::string what; std::vector<uint8_t> bytes; };
    std::vector<Case> bad;

    bad.push_back({"empty input", {}});
    bad.push_back({"header only", std::vector<uint8_t>(blob.begin(), blob.begin() + 4)});

    { auto b = blob; b[0] ^= 0xFF; bad.push_back({"wrong magic", b}); }
    { auto b = blob; b.resize(b.size() / 2); bad.push_back({"truncated mid-record", b}); }
    { auto b = blob; b.pop_back(); bad.push_back({"one byte short", b}); }
    { auto b = blob; b.push_back(0x00); bad.push_back({"one trailing byte", b}); }
    { auto b = blob;
      b.insert(b.end(), blob.begin(), blob.end());
      bad.push_back({"two blobs concatenated", b}); }
    { // A count field far larger than the file can supply: must be a parse error,
      // not an attempted 4-billion-element allocation.
      auto b = blob;
      b[4] = 0xFF; b[5] = 0xFF; b[6] = 0xFF; b[7] = 0xFF;
      bad.push_back({"absurd count field", b}); }
    { // Corrupt a key so the array is no longer sorted -- the binary search the
      // range query depends on would silently return wrong answers.
      auto b = blob;
      const size_t first_key = 24;
      for (int i = 0; i < 8; ++i) b[first_key + size_t(i)] = 0xFF;
      bad.push_back({"key array not sorted", b}); }
    { // A NaN coordinate: every comparison against it is false, so it would
      // never match a query and never be reported as a problem.
      auto b = blob;
      const size_t first_minlat = 24 + 8 + 4;
      std::vector<uint8_t> nan_bytes;
      util::put_f64(nan_bytes, std::nan(""));
      for (int i = 0; i < 8; ++i) b[first_minlat + size_t(i)] = nan_bytes[size_t(i)];
      bad.push_back({"NaN coordinate", b}); }
    { // min > max: an inverted box intersects nothing, silently.
      auto b = blob;
      const size_t first_minlat = 24 + 8 + 4;
      std::vector<uint8_t> big;
      util::put_f64(big, 89.0);
      for (int i = 0; i < 8; ++i) b[first_minlat + size_t(i)] = big[size_t(i)];
      bad.push_back({"inverted bounding box", b}); }

    for (const auto& c : bad) {
      // Load a good blob first, so we can check that a failed load leaves it alone.
      Geohash g;
      g.deserialize(blob);
      const size_t before = g.size();
      t::ok(!g.deserialize(c.bytes), "refused: " + c.what);
      t::ok(g.size() == before,
            "a refused load leaves the existing index intact: " + c.what);
    }
  }

  // ── an empty index round-trips ─────────────────────────────────────────────
  {
    Geohash empty_src, empty_dst;
    std::vector<uint8_t> eb;
    t::ok(empty_src.serialize(eb), "an empty index serialises");
    t::ok(empty_dst.deserialize(eb), "and deserialises");
    t::ok(empty_dst.size() == 0, "as empty");
    std::vector<ZoneId> out;
    empty_dst.query(geo::Bbox::around({25.55, 91.88}, 1000), out);
    t::ok(out.empty(), "and answers queries with nothing");
  }

  return t::report("index/serialization");
}
