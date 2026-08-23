// k-d tree nearest-neighbour, against a linear-scan oracle.
//
// The oracle uses the SAME scaled-planar metric as the tree, so a mismatch is a
// tree bug, not a metric disagreement. NN and k-NN are checked on random point
// clouds; ties in distance are handled by comparing the achieved distance, not
// the id (two equidistant points are both correct answers).
#include <algorithm>
#include <cmath>
#include <vector>
#include "safetrail/index/kd_tree.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::index;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
  int below(int n) { return int(next() % uint64_t(n)); }
};

double scaled_d2(const geo::LatLon& a, const geo::LatLon& b, double lon_scale) {
  const double dlat = a.lat - b.lat, dlon = (a.lon - b.lon) * lon_scale;
  return dlat * dlat + dlon * dlon;
}
}  // namespace

int main() {
  Rng rng(0x7D7EE);

  for (int trial = 0; trial < 60; ++trial) {
    const int n = 1 + rng.below(400);
    std::vector<KdTree<int>::Item> items;
    double sum_lat = 0.0;
    for (int i = 0; i < n; ++i) {
      geo::LatLon p{25.4 + rng.unit() * 0.4, 91.6 + rng.unit() * 0.5};
      items.push_back({i, p});
      sum_lat += p.lat;
    }
    const double lon_scale = std::cos(sum_lat / n * 3.14159265358979323846 / 180.0);

    KdTree<int> tree;
    tree.build(items);
    t::ok(tree.size() == size_t(n), "size matches");

    // A few random queries per tree.
    for (int qi = 0; qi < 8; ++qi) {
      geo::LatLon q{25.4 + rng.unit() * 0.4, 91.6 + rng.unit() * 0.5};

      // 1-NN.
      int got = -1;
      tree.nearest(q, got);
      double best = 1e300;
      for (const auto& it : items) best = std::min(best, scaled_d2(q, it.pos, lon_scale));
      t::near(scaled_d2(q, items[size_t(got)].pos, lon_scale), best, 1e-12,
              "1-NN distance == linear-scan minimum");

      // k-NN: the k returned distances equal the k smallest, in order.
      const size_t k = size_t(1 + rng.below(6));
      auto knn = tree.k_nearest(q, k);
      std::vector<double> all;
      for (const auto& it : items) all.push_back(scaled_d2(q, it.pos, lon_scale));
      std::sort(all.begin(), all.end());
      const size_t expect = std::min(k, size_t(n));
      t::ok(knn.size() == expect, "k-NN returns min(k, n) results");
      bool ordered = true, correct = true;
      for (size_t i = 0; i < knn.size(); ++i) {
        const double di = scaled_d2(q, items[size_t(knn[i])].pos, lon_scale);
        if (i > 0 && di + 1e-15 < scaled_d2(q, items[size_t(knn[i - 1])].pos, lon_scale))
          ordered = false;
        if (std::fabs(di - all[i]) > 1e-12) correct = false;   // i-th nearest matches i-th smallest
      }
      t::ok(ordered, "k-NN results are nearest-first");
      t::ok(correct, "k-NN distances == k smallest distances");
    }
  }

  // Empty tree is well-behaved.
  {
    KdTree<int> empty;
    empty.build({});
    int id = -1;
    t::ok(!empty.nearest({25.5, 91.8}, id), "nearest on empty tree returns false");
    t::ok(empty.k_nearest({25.5, 91.8}, 3).empty(), "k-NN on empty tree is empty");
  }

  return t::report("index/kd_tree");
}
