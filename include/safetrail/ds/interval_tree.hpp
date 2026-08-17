#pragma once
// Interval tree — augmented BST over intervals, keyed on low endpoint with each
// node caching the maximum high endpoint in its subtree. That augmentation is
// what makes overlap queries O(log n + k) instead of O(n).
//
// Two independent uses in this project, which is worth noting in the report:
//   - alert/escalation.hpp : deadline tracking, "which alerts are now overdue"
//   - index/versioned_index.hpp : zone validity spans, GAP 3
//
// Hand-written. std::multimap would not give the subtree-max augmentation.
#include <cstdint>
#include <vector>
#include "safetrail/types.hpp"

namespace safetrail::ds {

template <typename T>
class IntervalTree {
 public:
  struct Entry { Timestamp low, high; T value; };

  void insert(Timestamp low, Timestamp high, T value);
  bool remove(Timestamp low, Timestamp high, const T& value);
  void clear();

  // All entries whose interval overlaps [low, high). O(log n + k).
  void overlapping(Timestamp low, Timestamp high, std::vector<T>& out) const;
  // All entries active at a single instant. O(log n + k).
  void stabbing(Timestamp at, std::vector<T>& out) const;

  size_t size() const { return count_; }
  size_t height() const;   // for the balance-quality benchmark row

 private:
  struct Node {
    Entry    e;
    Timestamp max_high;
    int32_t  left = -1, right = -1;
    int32_t  height = 1;
  };
  std::vector<Node> nodes_;
  int32_t root_  = -1;
  size_t  count_ = 0;
};

}  // namespace safetrail::ds
