// Binary heap: the heap property and sorted extraction, against std::sort truth.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "safetrail/ds/priority_queue.hpp"
#include "../test_harness.hpp"

using namespace safetrail;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  int below(int n) { return int(next() % uint64_t(n)); }
};
}  // namespace

int main() {
  Rng rng(0x4EA9);

  // ── Min-heap pops in non-decreasing order, matching a sorted copy ───────────
  for (int trial = 0; trial < 50; ++trial) {
    const int n = rng.below(300);
    std::vector<int> data;
    ds::BinaryHeap<int> heap;
    for (int i = 0; i < n; ++i) { int v = rng.below(10000); data.push_back(v); heap.push(v); }
    std::sort(data.begin(), data.end());

    t::ok(heap.size() == size_t(n), "size tracks pushes");
    bool ordered = true;
    for (int i = 0; i < n; ++i) {
      if (heap.top() != data[size_t(i)]) ordered = false;   // peek == next pop
      if (heap.pop() != data[size_t(i)]) ordered = false;
    }
    t::ok(ordered, "min-heap extraction == sorted order (n=" + std::to_string(n) + ")");
    t::ok(heap.empty(), "heap empty after draining");
  }

  // ── Max-heap via a flipped comparator ──────────────────────────────────────
  {
    ds::BinaryHeap<int, std::greater<int>> maxh;
    for (int v : {3, 1, 4, 1, 5, 9, 2, 6}) maxh.push(v);
    std::vector<int> got;
    while (!maxh.empty()) got.push_back(maxh.pop());
    std::vector<int> want = {9, 6, 5, 4, 3, 2, 1, 1};
    t::ok(got == want, "greater<> comparator gives a max-heap");
  }

  // ── Interleaved push/pop keeps the invariant ───────────────────────────────
  {
    ds::BinaryHeap<int> h;
    h.push(5); h.push(3);
    t::ok(h.pop() == 3, "pop smallest (3)");
    h.push(1); h.push(4);
    t::ok(h.pop() == 1, "pop smallest after more pushes (1)");
    t::ok(h.pop() == 4, "then 4");
    t::ok(h.pop() == 5, "then 5");
    t::ok(h.empty(), "drained");
  }

  return t::report("ds/priority_queue");
}
