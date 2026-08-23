#pragma once
// Binary heap -- an array-backed complete binary tree with the heap property,
// pop always returning the extreme element in O(log n).
//
// Hand-written because std::priority_queue is off the table (it is one of the
// structures the course is about) and because we want two things it does not
// give: a peekable, iterable backing store for the alert-triage frontier, and a
// clean min-heap without the `std::greater` comparator dance.
//
// Two independent uses:
//   - graph/dijkstra.hpp, graph/astar.hpp   the shortest-path frontier
//   - alert/triage.hpp                      the escalation frontier (highest severity first)
//
// The comparator defines priority: `Less` making the SMALLEST element pop first
// (a min-heap), which is what Dijkstra wants on distance. Flip the comparator for
// a max-heap.
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace safetrail::ds {

template <typename T, typename Less = std::less<T>>
class BinaryHeap {
 public:
  BinaryHeap() = default;
  explicit BinaryHeap(Less less) : less_(std::move(less)) {}

  bool   empty() const { return heap_.empty(); }
  size_t size()  const { return heap_.size(); }
  void   clear()       { heap_.clear(); }
  void   reserve(size_t n) { heap_.reserve(n); }

  const T& top() const { return heap_.front(); }

  void push(T value) {
    heap_.push_back(std::move(value));
    sift_up(heap_.size() - 1);
  }

  // Remove and return the extreme element. Undefined on an empty heap -- the
  // caller checks empty() first, exactly as with the frontier loops.
  T pop() {
    T out = std::move(heap_.front());
    heap_.front() = std::move(heap_.back());
    heap_.pop_back();
    if (!heap_.empty()) sift_down(0);
    return out;
  }

 private:
  std::vector<T> heap_;
  Less less_;

  // "a is higher priority than b" -- pops before it. For a min-heap that is a<b.
  bool higher(const T& a, const T& b) const { return less_(a, b); }

  void sift_up(size_t i) {
    while (i > 0) {
      size_t parent = (i - 1) / 2;
      if (!higher(heap_[i], heap_[parent])) break;
      std::swap(heap_[i], heap_[parent]);
      i = parent;
    }
  }

  void sift_down(size_t i) {
    const size_t n = heap_.size();
    for (;;) {
      size_t best = i, l = 2 * i + 1, r = 2 * i + 2;
      if (l < n && higher(heap_[l], heap_[best])) best = l;
      if (r < n && higher(heap_[r], heap_[best])) best = r;
      if (best == i) break;
      std::swap(heap_[i], heap_[best]);
      i = best;
    }
  }
};

}  // namespace safetrail::ds
