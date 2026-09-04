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
//
// ── Deletion ─────────────────────────────────────────────────────────────────
//
// Real AVL deletion -- rotations, height repair and max_high repair on the way
// back up -- in O(log n).
//
// It used to be a tombstone: mark the node dead, decrement the count, leave the
// node in the tree. That was cheap to write and wrong in two ways that compound.
// First, cost: finding the node to tombstone was a linear scan of the node array,
// so "delete" was O(n) in a structure whose entire selling point is O(log n).
// Second, and worse, the tree's SHAPE stopped matching its reported size --
// count_ fell while the height stayed put -- so balanced(), which compares the
// height against 1.44*log2(n+2), was checking a real height against a fictional
// n. Delete most of a large tree and the AVL invariant "fails" while the tree is
// in fact perfectly balanced; the evidence the report cites was measuring
// something else. Dead nodes also kept inflating every subtree's max_high, so the
// pruning bound they exist to tighten got looser with every deletion.
//
// Freed slots go on a free list and are reused, so the node array stays bounded
// by the peak live size rather than by total inserts.
#include <cmath>
#include <cstdint>
#include <vector>
#include "safetrail/types.hpp"

namespace safetrail::ds {

template <typename T>
class IntervalTree {
 public:
  struct Entry { Timestamp low, high; T value; };

  void clear() { nodes_.clear(); free_.clear(); root_ = -1; count_ = 0; }
  size_t size() const { return count_; }

  void insert(Timestamp low, Timestamp high, T value) {
    const int32_t idx = alloc(Entry{low, high, value});
    root_ = insert_at(root_, idx);
    ++count_;
  }

  // Remove one entry matching (low, high, value) exactly. O(log n).
  bool remove(Timestamp low, Timestamp high, const T& value) {
    bool found = false;
    root_ = erase_at(root_, low, high, value, found);
    if (found) --count_;
    return found;
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
  // Now that deletion actually removes nodes, `count_` is the live node count and
  // this compares two quantities that describe the same tree.
  bool balanced() const {
    if (count_ < 2) return true;
    const double bound = 1.4405 * (std::log(double(count_) + 2.0) / std::log(2.0));
    return double(height()) <= bound + 1.0;
  }

  // Full structural audit: BST ordering on `low`, the AVL height/balance
  // invariant at every node, and max_high equal to the true subtree maximum.
  // Exists so the deletion tests can assert the invariants directly rather than
  // inferring them from query results -- a broken rotation often still answers
  // small queries correctly, which is exactly how it survives to production.
  bool check_invariants() const {
    size_t live = 0;
    const bool ok = audit(root_, INT64_MIN, INT64_MAX, live);
    return ok && live == count_;
  }

 private:
  struct Node {
    Entry e;
    Timestamp max_high;
    int32_t left = -1, right = -1;
    int32_t height = 1;
  };
  std::vector<Node> nodes_;
  std::vector<int32_t> free_;      // reusable slots, from erased nodes
  int32_t root_ = -1;
  size_t count_ = 0;

  int32_t alloc(Entry e) {
    if (!free_.empty()) {
      const int32_t i = free_.back();
      free_.pop_back();
      nodes_[size_t(i)] = Node{e, e.high, -1, -1, 1};
      return i;
    }
    nodes_.push_back(Node{e, e.high, -1, -1, 1});
    return int32_t(nodes_.size()) - 1;
  }
  void release(int32_t i) { free_.push_back(i); }

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

  // Shared by insert and erase. Deletion can unbalance a node by two in either
  // direction and, unlike insertion, can require a rotation at EVERY level on the
  // way back up -- which is why this has to be a general rebalance keyed on the
  // children's balance factors rather than insertion's "which way did the new key
  // go" shortcut.
  int32_t rebalance(int32_t i) {
    refit(i);
    const int32_t bal = h(nodes_[size_t(i)].left) - h(nodes_[size_t(i)].right);
    if (bal > 1) {
      const int32_t l = nodes_[size_t(i)].left;
      if (h(nodes_[size_t(l)].left) < h(nodes_[size_t(l)].right))
        nodes_[size_t(i)].left = rot_left(l);          // left-right
      return rot_right(i);
    }
    if (bal < -1) {
      const int32_t r = nodes_[size_t(i)].right;
      if (h(nodes_[size_t(r)].right) < h(nodes_[size_t(r)].left))
        nodes_[size_t(i)].right = rot_right(r);        // right-left
      return rot_left(i);
    }
    return i;
  }

  int32_t insert_at(int32_t root, int32_t idx) {
    if (root < 0) { refit(idx); return idx; }
    if (nodes_[size_t(idx)].e.low < nodes_[size_t(root)].e.low)
      nodes_[size_t(root)].left = insert_at(nodes_[size_t(root)].left, idx);
    else
      nodes_[size_t(root)].right = insert_at(nodes_[size_t(root)].right, idx);
    return rebalance(root);
  }

  int32_t min_node(int32_t i) const {
    while (nodes_[size_t(i)].left >= 0) i = nodes_[size_t(i)].left;
    return i;
  }

  // Detach the node at `i` itself, returning the replacement subtree root.
  int32_t erase_node(int32_t i) {
    Node& n = nodes_[size_t(i)];
    if (n.left < 0 || n.right < 0) {                   // 0 or 1 child
      const int32_t child = n.left >= 0 ? n.left : n.right;
      release(i);
      return child;
    }
    // Two children: replace this node's payload with its in-order successor, then
    // erase the successor from the right subtree. Copying the payload (rather
    // than relinking) keeps the free list and the index arithmetic simple.
    const int32_t succ = min_node(n.right);
    const Entry se = nodes_[size_t(succ)].e;
    bool found = false;
    n.right = erase_at(n.right, se.low, se.high, se.value, found);
    nodes_[size_t(i)].e = se;
    return rebalance(i);
  }

  int32_t erase_at(int32_t root, Timestamp low, Timestamp high, const T& value,
                   bool& found) {
    if (root < 0 || found) return root;
    Node& n = nodes_[size_t(root)];
    if (low < n.e.low) {
      n.left = erase_at(n.left, low, high, value, found);
    } else if (low > n.e.low) {
      n.right = erase_at(n.right, low, high, value, found);
    } else if (n.e.high == high && n.e.value == value) {
      found = true;
      return erase_node(root);
    } else {
      // Equal `low`, different interval. insert_at sends equal keys right, but
      // rotations can move them either side, so both subtrees must be tried.
      n.right = erase_at(n.right, low, high, value, found);
      if (!found) nodes_[size_t(root)].left =
          erase_at(nodes_[size_t(root)].left, low, high, value, found);
    }
    if (!found) { refit(root); return root; }
    return rebalance(root);
  }

  void descend(int32_t i, Timestamp low, Timestamp high, std::vector<T>& out) const {
    if (i < 0) return;
    const Node& n = nodes_[size_t(i)];
    if (n.max_high <= low) return;                 // ← the augmentation earning its keep
    descend(n.left, low, high, out);
    if (n.e.low < high && low < n.e.high) out.push_back(n.e.value);
    if (n.e.low < high) descend(n.right, low, high, out);
  }

  bool audit(int32_t i, Timestamp lo_bound, Timestamp hi_bound, size_t& live) const {
    if (i < 0) return true;
    const Node& n = nodes_[size_t(i)];
    if (n.e.low < lo_bound || n.e.low > hi_bound) return false;
    ++live;
    if (!audit(n.left, lo_bound, n.e.low, live)) return false;
    if (!audit(n.right, n.e.low, hi_bound, live)) return false;
    const int32_t want_h = 1 + (h(n.left) > h(n.right) ? h(n.left) : h(n.right));
    if (n.height != want_h) return false;
    const int32_t bal = h(n.left) - h(n.right);
    if (bal < -1 || bal > 1) return false;
    Timestamp want_mh = n.e.high;
    if (mh(n.left) > want_mh) want_mh = mh(n.left);
    if (mh(n.right) > want_mh) want_mh = mh(n.right);
    return n.max_high == want_mh;
  }
};

}  // namespace safetrail::ds
