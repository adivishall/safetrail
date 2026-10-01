#pragma once
// Interval tree -- an AVL-balanced BST over intervals, keyed on the low endpoint,
// with every node caching the maximum high endpoint in its subtree.
//
// That augmentation is the whole trick: it lets a search prune an entire subtree
// the moment `subtree_max_high <= query_low`. std::multimap cannot do this --
// there is no hook to maintain a subtree aggregate -- which is why this is
// hand-written.
//
// What the bound actually is. Finding ONE overlapping interval is O(log n).
// Reporting all k of them with this traversal (descend left while the subtree's
// max_high says it could hold one; descend right while low < query_high) costs
// O(min(n, (k + 1) log n)) in the worst case, not O(log n + k): every reported
// interval can light up a different root-to-leaf path, and the nodes on those
// paths are visited whether or not they overlap. With AVL height <= 1.44 log n
// that is a guarantee, and it is never worse than the scan; it is also why the
// tree only wins when the query is selective (RESULTS.md section 8). A centred
// interval tree or a priority search tree would give O(log n + k); neither is
// needed here, because the per-tick path does not use this structure at all.
//
// Used by index/versioned_index.hpp for active_at(t): which zones were in force
// at time t, by the rules of time t [GAP 3]. It is deliberately NOT the per-fix
// temporal filter -- the evaluator checks validity in O(1) on the few candidates
// the spatial index returned -- and alert escalation deadlines use
// ds/timer_wheel.hpp, which fits "what has expired by now" better.
//
// ── The ordering key, and why it is not just `low` ───────────────────────────
//
// Nodes are ordered by the TOTAL key (low, high, value, seq), not by `low` alone.
// `seq` is a per-insert counter, so no two live nodes ever compare equal.
//
// That matters for deletion, not for queries. When the key was `low` alone, a set
// of intervals sharing a low endpoint -- which is the normal case here, since
// every zone whose closure starts at midnight shares one -- formed a block of
// equal keys, and rotations can move an equal key to either side of its
// neighbour. remove() therefore could not descend a single path: on a key match
// with the wrong payload it had to try the right subtree AND then the left,
// degenerating to a full traversal, O(n) in a structure whose entire claim is
// O(log n). The complexity table said O(log n); the code said "it depends on your
// data", and the data this project actually has is the bad case.
//
// With a strict total order there is exactly one path. remove(low, high, value)
// compares on the (low, high, value) TRIPLE, which is monotone in the full key,
// so the nodes matching a triple occupy a contiguous in-order range and an
// ordinary BST descent lands in it -- O(log n), unconditionally, whatever the
// endpoint multiplicity. `seq` never appears in the public interface; it exists
// so that the shape of the tree is a deterministic function of the insertion
// sequence rather than of how rotations happened to break ties.
//
// Requirement on T: `<` and `==`. Every instantiation here is an integer id.
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

  void clear() { nodes_.clear(); free_.clear(); root_ = -1; count_ = 0; next_seq_ = 0; }
  size_t size() const { return count_; }

  void insert(Timestamp low, Timestamp high, T value) {
    const int32_t idx = alloc(Entry{low, high, value}, next_seq_++);
    root_ = insert_at(root_, idx);
    ++count_;
  }

  // Remove one entry matching (low, high, value) exactly. O(log n) -- a single
  // root-to-node descent, whatever the endpoint multiplicity. See the note above.
  bool remove(Timestamp low, Timestamp high, const T& value) {
    bool found = false;
    root_ = erase_first(root_, low, high, value, found);
    if (found) --count_;
    return found;
  }

  // Entries overlapping [low, high): O(min(n, (k + 1) log n)), see the top of the
  // file. An empty or inverted range overlaps nothing -- without the guard,
  // [10, 5) would "overlap" [0, 20).
  void overlapping(Timestamp low, Timestamp high, std::vector<T>& out) const {
    if (low >= high) return;
    descend(root_, low, high, out);
  }
  // Entries containing a single instant; same bound as overlapping(). Every
  // interval is half-open with high <= INT64_MAX, so none contains INT64_MAX
  // itself; the early return is also what keeps `at + 1` from overflowing
  // (signed overflow is UB, and kForever == INT64_MAX is a value callers really
  // pass).
  void stabbing(Timestamp at, std::vector<T>& out) const {
    if (at == INT64_MAX) return;
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
    const bool ok = audit(root_, -1, -1, live);
    return ok && live == count_;
  }

 private:
  struct Node {
    Entry e;
    uint64_t seq = 0;              // insertion order: the final tie-break
    Timestamp max_high;
    int32_t left = -1, right = -1;
    int32_t height = 1;
  };
  std::vector<Node> nodes_;
  std::vector<int32_t> free_;      // reusable slots, from erased nodes
  int32_t root_ = -1;
  size_t count_ = 0;
  uint64_t next_seq_ = 0;          // never reused, unlike a slot index

  // ── The order ──────────────────────────────────────────────────────────────
  // cmp_triple is what the PUBLIC operations compare on: it is monotone in the
  // full key, so a descent driven by it lands inside the contiguous run of nodes
  // sharing a triple. cmp_full adds `seq` and is a strict total order, which is
  // what insert uses so that no two live nodes ever tie.
  static int cmp_triple(Timestamp alow, Timestamp ahigh, const T& av,
                        Timestamp blow, Timestamp bhigh, const T& bv) {
    if (alow != blow) return alow < blow ? -1 : 1;
    if (ahigh != bhigh) return ahigh < bhigh ? -1 : 1;
    if (av < bv) return -1;
    if (bv < av) return 1;
    return 0;
  }
  bool less_full(int32_t a, int32_t b) const {
    const Node& x = nodes_[size_t(a)];
    const Node& y = nodes_[size_t(b)];
    const int c = cmp_triple(x.e.low, x.e.high, x.e.value,
                             y.e.low, y.e.high, y.e.value);
    return c != 0 ? c < 0 : x.seq < y.seq;
  }

  int32_t alloc(Entry e, uint64_t seq) {
    if (!free_.empty()) {
      const int32_t i = free_.back();
      free_.pop_back();
      nodes_[size_t(i)] = Node{e, seq, e.high, -1, -1, 1};
      return i;
    }
    nodes_.push_back(Node{e, seq, e.high, -1, -1, 1});
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
    if (less_full(idx, root))
      nodes_[size_t(root)].left = insert_at(nodes_[size_t(root)].left, idx);
    else
      nodes_[size_t(root)].right = insert_at(nodes_[size_t(root)].right, idx);
    return rebalance(root);
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
    // unlink the successor from the right subtree. Copying the payload (rather
    // than relinking) keeps the free list and the index arithmetic simple. The
    // successor's `seq` travels with its payload, so the node that takes its
    // place sits at exactly the successor's position in the total order.
    //
    // The successor is unlinked by POSITION (erase_min), not by searching for
    // its (low, high, value) triple. A triple search is free to land on a
    // different node with the same triple -- exact duplicates are legal -- and
    // then the successor survives in the right subtree while its payload and
    // `seq` are also copied up here: two live nodes with one total-order key,
    // which check_invariants() rejects. Queries still answered correctly, which
    // is how that bug hid; tests/ds/interval_tree_test.cpp now audits after
    // every single removal of an exact duplicate.
    int32_t succ = -1;
    const int32_t new_right = erase_min(n.right, succ);
    nodes_[size_t(i)].right = new_right;
    nodes_[size_t(i)].e = nodes_[size_t(succ)].e;
    nodes_[size_t(i)].seq = nodes_[size_t(succ)].seq;
    release(succ);
    return rebalance(i);
  }

  // Unlink the minimum node of the subtree at `i`, returning the new subtree
  // root and the unlinked node's index in `min_out`. The caller releases it.
  // Rebalances on the way back up, like any other deletion path.
  int32_t erase_min(int32_t i, int32_t& min_out) {
    const int32_t left = nodes_[size_t(i)].left;
    if (left < 0) {
      min_out = i;
      return nodes_[size_t(i)].right;
    }
    const int32_t new_left = erase_min(left, min_out);
    nodes_[size_t(i)].left = new_left;
    return rebalance(i);
  }

  // A single-path descent on the (low, high, value) triple. The triple is
  // monotone in the tree's total order, so every node sharing a triple forms one
  // contiguous in-order run; landing anywhere in that run is a match, and the
  // entries in it are indistinguishable to the caller. Hence: compare, go one
  // way, never both. That is what makes remove() O(log n) rather than O(n) when
  // many intervals share an endpoint.
  int32_t erase_first(int32_t root, Timestamp low, Timestamp high, const T& value,
                      bool& found) {
    if (root < 0) return root;
    Node& n = nodes_[size_t(root)];
    const int c = cmp_triple(low, high, value, n.e.low, n.e.high, n.e.value);
    if (c == 0) {
      found = true;
      return erase_node(root);
    }
    if (c < 0) n.left  = erase_first(n.left,  low, high, value, found);
    else       n.right = erase_first(n.right, low, high, value, found);
    if (!found) { refit(root); return root; }
    return rebalance(root);
  }

  // Both prunes survive the richer key unchanged, because the total order is
  // lexicographic with `low` first: everything in the right subtree has a low at
  // least this node's, so `n.e.low >= high` still rules the whole subtree out.
  void descend(int32_t i, Timestamp low, Timestamp high, std::vector<T>& out) const {
    if (i < 0) return;
    const Node& n = nodes_[size_t(i)];
    if (n.max_high <= low) return;                 // ← the augmentation earning its keep
    descend(n.left, low, high, out);
    if (n.e.low < high && low < n.e.high) out.push_back(n.e.value);
    if (n.e.low < high) descend(n.right, low, high, out);
  }

  // `lo` and `hi` are node indices bounding this subtree in the TOTAL order
  // (-1 = unbounded). Checking against `low` alone would accept a tree whose
  // duplicate-endpoint entries had drifted to the wrong side of a rotation --
  // precisely the shape that made single-path deletion unsafe before.
  bool audit(int32_t i, int32_t lo, int32_t hi, size_t& live) const {
    if (i < 0) return true;
    const Node& n = nodes_[size_t(i)];
    if (lo >= 0 && !less_full(lo, i)) return false;
    if (hi >= 0 && !less_full(i, hi)) return false;
    ++live;
    if (!audit(n.left, lo, i, live)) return false;
    if (!audit(n.right, i, hi, live)) return false;
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
