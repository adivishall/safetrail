#include "safetrail/ds/dynamic_connectivity.hpp"
#include <climits>

namespace safetrail::ds {

RollbackDSU::RollbackDSU(size_t n)
    : parent_(n), rank_(n, 0), size_(n, 1), components_(n) {
  for (size_t i = 0; i < n; ++i) parent_[i] = i;
}

// NO path compression -- deliberately. Compression writes an unbounded number of
// parent pointers per find, which cannot be recorded on a bounded undo stack.
// Losing near-constant find to gain O(1) undo is the central trade in this file.
size_t RollbackDSU::find(size_t x) const {
  while (parent_[x] != x) x = parent_[x];
  return x;
}

bool RollbackDSU::unite(size_t x, size_t y) {
  size_t rx = find(x), ry = find(y);
  if (rx == ry) return false;                    // nothing pushed to undo stack
  if (rank_[rx] < rank_[ry]) { size_t t = rx; rx = ry; ry = t; }

  undo_.push_back(Undo{ry, parent_[ry], rx, rank_[rx]});
  parent_[ry] = rx;
  size_[rx] += size_[ry];
  if (rank_[rx] == rank_[ry]) ++rank_[rx];
  --components_;
  return true;
}

size_t RollbackDSU::component_size(size_t x) const { return size_[find(x)]; }

void RollbackDSU::rollback_to(size_t mark) {
  while (undo_.size() > mark) {
    const Undo u = undo_.back();
    undo_.pop_back();
    size_[u.parent_of] -= size_[u.child];
    parent_[u.child] = u.old_parent;
    rank_[u.parent_of] = u.old_rank;
    ++components_;
  }
}

std::vector<std::vector<size_t>> RollbackDSU::components() const {
  const size_t n = parent_.size();
  std::vector<size_t> slot(n, SIZE_MAX);
  std::vector<std::vector<size_t>> out;
  for (size_t i = 0; i < n; ++i) {
    const size_t r = find(i);
    if (slot[r] == SIZE_MAX) { slot[r] = out.size(); out.emplace_back(); }
    out[slot[r]].push_back(i);
  }
  return out;
}

// Correctness oracle: O(n^2) flood fill, no cleverness. RollbackDSU is asserted
// to agree with this on randomised merge/split sequences. Keep it forever.
std::vector<std::vector<size_t>> components_bruteforce(
    size_t n, const std::vector<std::pair<size_t, size_t>>& edges) {
  std::vector<std::vector<size_t>> adj(n);
  for (const auto& e : edges) { adj[e.first].push_back(e.second); adj[e.second].push_back(e.first); }
  std::vector<bool> seen(n, false);
  std::vector<std::vector<size_t>> out;
  for (size_t i = 0; i < n; ++i) {
    if (seen[i]) continue;
    out.emplace_back();
    std::vector<size_t> stack{i};
    seen[i] = true;
    while (!stack.empty()) {
      const size_t v = stack.back(); stack.pop_back();
      out.back().push_back(v);
      for (size_t w : adj[v]) if (!seen[w]) { seen[w] = true; stack.push_back(w); }
    }
  }
  return out;
}

}  // namespace safetrail::ds
