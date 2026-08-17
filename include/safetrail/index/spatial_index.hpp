#pragma once
//
// The spatial index interface — arguably the most important file in the project.
//
// Every index implementation sits behind this, including the brute-force one.
// That single decision buys three things:
//
//   1. tests/index/equivalence_test.cpp can assert that every implementation
//      returns identical results to BruteForce on randomised input. Without
//      this, a benchmark risks comparing a correct slow thing against a fast
//      wrong thing — the classic way to "win" a benchmark chapter.
//
//   2. apps/safetrail_bench.cpp can sweep all implementations through the same
//      harness with no special-casing.
//
//   3. The hot loop in fence/evaluator.hpp is written once against the
//      interface, so swapping the index is a one-line change.
//
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/types.hpp"

namespace safetrail::index {

using safetrail::ZoneId;

// ─── Statistics, for the benchmark and the diagnostics overlay ──────────────
struct IndexStats {
  size_t node_count      = 0;
  size_t max_depth       = 0;
  size_t bytes           = 0;   // for the serialisation comparison, GAP 6
  size_t queries         = 0;
  size_t candidates_returned = 0;   // the number that matters: how much did the
                                    // index actually prune?
  double avg_candidates() const {
    return queries ? double(candidates_returned) / double(queries) : 0.0;
  }
};

// ─── Interface ──────────────────────────────────────────────────────────────
class SpatialIndex {
 public:
  virtual ~SpatialIndex() = default;

  virtual const char* name() const = 0;

  // Bulk build. Prefer this over repeated insert() where possible — an R-tree
  // built by STR packing is markedly better than one built by insertion, and
  // that difference is itself a benchmark row.
  virtual void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) = 0;

  virtual void insert(ZoneId id, const geo::Bbox& box) = 0;
  virtual bool remove(ZoneId id) = 0;

  // ── The hot-path query ────────────────────────────────────────────────────
  //
  // Returns every zone whose bounding box intersects `query`. This is a
  // CONSERVATIVE filter — callers must run the exact geometric test on each
  // candidate. That two-phase filter-then-refine split is the standard shape of
  // spatial query processing and the reason the index only stores boxes.
  //
  // Appends to `out` rather than returning, so the caller reuses one buffer
  // across the whole tick and we do not allocate 200 vectors per 100 ms.
  virtual void query(const geo::Bbox& query, std::vector<ZoneId>& out) const = 0;

  // k nearest zone boxes to a point. Drives the adaptive sampler (GAP 7):
  // "how far is the nearest thing I could possibly breach?"
  virtual void nearest(const geo::LatLon& p, size_t k,
                       std::vector<ZoneId>& out) const = 0;

  virtual size_t size() const = 0;
  virtual IndexStats stats() const = 0;
  virtual void reset_counters() = 0;

  // ── Serialisation  [GAP 6] ────────────────────────────────────────────────
  //
  // The offline story depends on this. A district's zones serialise to a compact
  // blob, ship to the device, and get evaluated locally with no server. No
  // existing implementation can do this — they all need a round trip to PostGIS,
  // which is exactly what fails in the terrain this problem statement targets.
  //
  // Not every index needs to support it; return false if unsupported.
  virtual bool serialize(std::vector<uint8_t>& out) const { (void)out; return false; }
  virtual bool deserialize(const std::vector<uint8_t>& in) { (void)in; return false; }
};

// ─── Factory ────────────────────────────────────────────────────────────────
enum class IndexKind { BruteForce, Quadtree, RTree, Geohash };

std::unique_ptr<SpatialIndex> make_index(IndexKind kind);
const char* to_string(IndexKind k);

}  // namespace safetrail::index
