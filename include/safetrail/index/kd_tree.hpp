#pragma once
// 2-D k-d tree -- nearest-neighbour queries over point entities.
//
// The dispatch layer asks "which responders are closest to this incident?" and
// the adaptive sampler asks "how far is the nearest thing I could breach?". Both
// are k-nearest-neighbour queries over points, and a linear scan is O(n) per
// query -- fine for a handful of responders, wrong for thousands of tourists. A
// k-d tree answers NN in O(log n) average by splitting the plane on alternating
// axes and pruning the half it cannot improve on.
//
// Metric. Points are WGS84 lat/lon, but degrees are not isotropic -- a degree of
// longitude is shorter than a degree of latitude away from the equator. We scale
// longitude by cos(mean latitude) so the working space is locally metric (a
// scaled-planar approximation, the same flat-earth tangent-plane trade the rest
// of the engine makes; see geo/point.hpp). Distances are therefore comparable and
// the NN result matches a great-circle nearest to well under GPS noise across a
// district. The scale is captured at build time from the data centroid.
//
// Hand-written: no std::set / std::map. Nodes live in one flat vector (indices,
// not pointers -- cache-friendly and trivially serialisable); build uses
// std::nth_element (an algorithm, permitted) for median partitioning.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "safetrail/geo/point.hpp"
#include "safetrail/types.hpp"

namespace safetrail::index {

template <typename Id>
class KdTree {
 public:
  struct Item { Id id; geo::LatLon pos; };

  void build(std::vector<Item> items) {
    items_ = std::move(items);
    nodes_.clear();
    root_ = -1;
    if (items_.empty()) return;

    // Longitude scale from the data centroid -- makes the space locally metric.
    double sum_lat = 0.0;
    for (const auto& it : items_) sum_lat += it.pos.lat;
    lon_scale_ = std::cos(sum_lat / double(items_.size()) * kDeg2Rad);

    std::vector<int32_t> idx(items_.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = int32_t(i);
    nodes_.reserve(items_.size());
    root_ = build_rec(idx, 0, int32_t(idx.size()), 0);
  }

  size_t size() const { return items_.size(); }

  // Single nearest neighbour. Returns false if the tree is empty.
  bool nearest(const geo::LatLon& q, Id& out_id) const {
    if (root_ < 0) return false;
    int32_t best = -1;
    double best_d2 = 1e300;
    nn_rec(root_, q, best, best_d2);
    out_id = items_[size_t(best)].id;
    return true;
  }

  // k nearest neighbours, returned nearest-first. Fewer than k if the tree is smaller.
  std::vector<Id> k_nearest(const geo::LatLon& q, size_t k) const {
    std::vector<Cand> heap;   // kept as a sorted-by-distance vector, size <= k
    if (k > 0) knn_rec(root_, q, k, heap);
    std::vector<Id> out;
    out.reserve(heap.size());
    for (const auto& c : heap) out.push_back(items_[size_t(c.node)].id);
    return out;
  }

 private:
  static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

  struct Node { int32_t item; int32_t left = -1, right = -1; uint8_t axis = 0; };
  struct Cand { double d2; int32_t node; };

  std::vector<Item> items_;
  std::vector<Node> nodes_;
  int32_t root_ = -1;
  double  lon_scale_ = 1.0;

  double axis_val(int32_t item, uint8_t axis) const {
    return axis == 0 ? items_[size_t(item)].pos.lat : items_[size_t(item)].pos.lon;
  }

  double dist2(const geo::LatLon& a, const geo::LatLon& b) const {
    const double dlat = a.lat - b.lat;
    const double dlon = (a.lon - b.lon) * lon_scale_;
    return dlat * dlat + dlon * dlon;
  }

  int32_t build_rec(std::vector<int32_t>& idx, int32_t lo, int32_t hi, uint8_t depth) {
    if (lo >= hi) return -1;
    const uint8_t axis = depth % 2;
    const int32_t mid = lo + (hi - lo) / 2;
    std::nth_element(idx.begin() + lo, idx.begin() + mid, idx.begin() + hi,
                     [&](int32_t a, int32_t b) { return axis_val(a, axis) < axis_val(b, axis); });
    const int32_t me = int32_t(nodes_.size());
    nodes_.push_back(Node{idx[size_t(mid)], -1, -1, axis});
    const int32_t l = build_rec(idx, lo, mid, uint8_t(depth + 1));
    const int32_t r = build_rec(idx, mid + 1, hi, uint8_t(depth + 1));
    nodes_[size_t(me)].left = l;
    nodes_[size_t(me)].right = r;
    return me;
  }

  void nn_rec(int32_t n, const geo::LatLon& q, int32_t& best, double& best_d2) const {
    if (n < 0) return;
    const Node& nd = nodes_[size_t(n)];
    const double d2 = dist2(q, items_[size_t(nd.item)].pos);
    if (d2 < best_d2) { best_d2 = d2; best = nd.item; }

    const double diff = (nd.axis == 0 ? q.lat - items_[size_t(nd.item)].pos.lat
                                      : (q.lon - items_[size_t(nd.item)].pos.lon) * lon_scale_);
    const int32_t near = diff < 0 ? nd.left : nd.right;
    const int32_t far  = diff < 0 ? nd.right : nd.left;
    nn_rec(near, q, best, best_d2);
    if (diff * diff < best_d2) nn_rec(far, q, best, best_d2);   // the plane could hide a closer point
  }

  // Maintain `heap` as a distance-sorted vector of the k best seen so far.
  void knn_rec(int32_t n, const geo::LatLon& q, size_t k, std::vector<Cand>& heap) const {
    if (n < 0) return;
    const Node& nd = nodes_[size_t(n)];
    const double d2 = dist2(q, items_[size_t(nd.item)].pos);
    if (heap.size() < k || d2 < heap.back().d2) {
      Cand c{d2, nd.item};
      auto it = std::upper_bound(heap.begin(), heap.end(), c,
                                 [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
      heap.insert(it, c);
      if (heap.size() > k) heap.pop_back();
    }
    const double diff = (nd.axis == 0 ? q.lat - items_[size_t(nd.item)].pos.lat
                                      : (q.lon - items_[size_t(nd.item)].pos.lon) * lon_scale_);
    const int32_t near = diff < 0 ? nd.left : nd.right;
    const int32_t far  = diff < 0 ? nd.right : nd.left;
    knn_rec(near, q, k, heap);
    if (heap.size() < k || diff * diff < heap.back().d2) knn_rec(far, q, k, heap);
  }
};

}  // namespace safetrail::index
