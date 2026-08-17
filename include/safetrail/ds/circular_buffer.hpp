#pragma once
// Fixed-capacity ring buffer.
//
// One per tourist, holding recent GPS pings. Bounded memory regardless of session
// length, which matters: 200 tourists × 10 Hz × one day is 172M pings if you keep
// everything. Overwrites oldest on overflow.
//
// Hand-written, on the stack, no allocation — it sits directly inside Tourist and
// is touched every tick.
#include <cstddef>
#include <array>

namespace safetrail::ds {

template <typename T, size_t N>
class CircularBuffer {
 public:
  void push(const T& v);
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  static constexpr size_t capacity() { return N; }

  // index 0 = newest, size()-1 = oldest. Chosen this way because every consumer
  // (anomaly detection, speed estimation, hysteresis confirmation) walks backwards
  // from the present.
  const T& operator[](size_t i) const;
  const T& newest() const;
  const T& oldest() const;
  void clear() { head_ = 0; count_ = 0; }

 private:
  std::array<T, N> buf_{};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace safetrail::ds
