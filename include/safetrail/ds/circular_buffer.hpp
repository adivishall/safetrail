#pragma once
// Fixed-capacity ring buffer.
//
// One per tourist, holding recent GPS pings. Bounded memory regardless of session
// length: 200 tourists x 10 Hz x one day is 172M pings if you keep everything.
// Overwrites oldest on overflow. Stack-allocated, no heap -- it lives inside
// Tourist and is touched every tick.
#include <array>
#include <cstddef>

namespace safetrail::ds {

template <typename T, size_t N>
class CircularBuffer {
 public:
  void push(const T& v) {
    buf_[head_] = v;
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
  }

  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  static constexpr size_t capacity() { return N; }
  void clear() { head_ = 0; count_ = 0; }

  // index 0 = NEWEST, size()-1 = oldest. Chosen this way because every consumer
  // -- anomaly detection, speed estimation, hysteresis confirmation -- walks
  // backwards from the present.
  const T& operator[](size_t i) const {
    return buf_[(head_ + N - 1 - (i % (count_ ? count_ : 1))) % N];
  }
  const T& newest() const { return (*this)[0]; }
  const T& oldest() const { return (*this)[count_ ? count_ - 1 : 0]; }

 private:
  std::array<T, N> buf_{};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace safetrail::ds
