#pragma once
// Interval tree -- an AVL-balanced BST over intervals, keyed on the low endpoint,
// with every node caching the maximum high endpoint in its subtree.
//
// That augmentation is the whole trick: it lets a search prune an entire subtree
// the moment `subtree_max_high <= query_low`, which turns overlap queries from
// O(n) into O(log n + k). std::multimap cannot do this -- there is no hook to
// maintain a subtree aggregate -- which is why this is hand-written.
//
// Two independent uses, worth noting because they are unrelated problems:
//   - alert/escalation.hpp        which alerts are now overdue
//   - index/versioned_index.hpp   which zones were in force at time t   [GAP 3]
#include <cmath>
#include <cstdint>
#include <vector>
#include "safetrail/types.hpp"

namespace safetrail::ds {

template <typename T>
class IntervalTree {
 public:
  struct Entry { Timestamp low, high; T value; };

  void clear() { nodes_.clear(); root_ = -1; count_ = 0; }
  size_t size() const { return count_; }

  void insert(Timestamp low, Timestamp high, T value) {
    nodes_.push_back(Node{Entry{low, high, value}, high, -1, -1, 1});
    root_ = insert_at(root_, int32_t(nodes_.size()) - 1);
    ++count_;
  }

  bool remove(Timestamp low, Timestamp high, const T& value) {
    // Tombstone rather than restructure. Zone validity changes are rare (an
    // operator action), so paying O(n) on removal to keep insertion simple is the
    // right trade here; the alternative is BST delete with subtree-max repair.
    for (auto& n : nodes_)
      if (!n.dead && n.e.low == low && n.e.high == high && n.e.value == value) {
        n.dead = true; --count_; return true;
      }
    return false;
  }

  // Entries overlapping [low, high). O(log n + k).
  void overlapping(Timestamp low, Timestamp high, std::vector<T>& out) const {
    descend(root_, low, high, out);
  }
  // Entries containing a single instant. O(log n + k).
  void stabbing(Timestamp at, std::vector<T>& out) const {
    descend(root_, at, at + 1, out);
  }

  size_t height() const { return root_ < 0 ? 0 : size_t(nodes_[size_t(root_)].height); }
  // Balance evidence for the report: an AVL tree must satisfy h <= 1.44 log2(n+2).
  bool balanced() const {
    if (count_ < 2) return true;
    double bound = 1.4405 * (std::log(double(count_) + 2.0) / std::log(2.0));
    return double(height()) <= bound + 1.0;
  }

 private:
  struct Node {
    Entry e;
    Timestamp max_high;
    int32_t left = -1, right = -1;
    int32_t height = 1;
    bool dead = false;
  };
  std::vector<Node> nodes_;
  int32_t root_ = -1;
  size_t count_ = 0;

  int32_t h(int32_t i) const { return i < 0 ? 0 : nodes_[size_t(i)].height; }
  Timestamp mh(int32_t i) const { return i < 0 ? INT64_MIN : nodes_[size_t(i)].max_high; }

  void refit(int32_t i) {
    Node& n = nodes_[size_t(i)];
    n.height = 1 + (h(n.left) > h(n.right) ? h(n.left) : h(n.right));
    n.max_high = n.e.high;
    if (mh(n.left) > n.max_high) n.max_high = mh(n.left);
    if (mh(n.right) > n.max_high) n.max_high = mh(n.right);
  }

  int32_t rot_right(int32_t y) {
    int32_t x = nodes_[size_t(y)].left;
    nodes_[size_t(y)].left = nodes_[size_t(x)].right;
    nodes_[size_t(x)].right = y;
    refit(y); refit(x);
    return x;
  }
  int32_t rot_left(int32_t x) {
    int32_t y = nodes_[size_t(x)].right;
    nodes_[size_t(x)].right = nodes_[size_t(y)].left;
    nodes_[size_t(y)].left = x;
    refit(x); refit(y);
    return y;
  }

  int32_t insert_at(int32_t root, int32_t idx) {
    if (root < 0) { refit(idx); return idx; }
    if (nodes_[size_t(idx)].e.low < nodes_[size_t(root)].e.low)
      nodes_[size_t(root)].left = insert_at(nodes_[size_t(root)].left, idx);
    else
      nodes_[size_t(root)].right = insert_at(nodes_[size_t(root)].right, idx);
    refit(root);

    const int32_t bal = h(nodes_[size_t(root)].left) - h(nodes_[size_t(root)].right);
    const Timestamp k = nodes_[size_t(idx)].e.low;
    if (bal > 1) {
      if (k >= nodes_[size_t(nodes_[size_t(root)].left)].e.low)
        nodes_[size_t(root)].left = rot_left(nodes_[size_t(root)].left);
      return rot_right(root);
    }
    if (bal < -1) {
      if (k < nodes_[size_t(nodes_[size_t(root)].right)].e.low)
        nodes_[size_t(root)].right = rot_right(nodes_[size_t(root)].right);
      return rot_left(root);
    }
    return root;
  }

  void descend(int32_t i, Timestamp low, Timestamp high, std::vector<T>& out) const {
    if (i < 0) return;
    const Node& n = nodes_[size_t(i)];
    if (n.max_high <= low) return;                 // ← the augmentation earning its keep
    descend(n.left, low, high, out);
    if (n.e.low < high && low < n.e.high && !n.dead) out.push_back(n.e.value);
    if (n.e.low < high) descend(n.right, low, high, out);
  }
};

}  // namespace safetrail::ds
