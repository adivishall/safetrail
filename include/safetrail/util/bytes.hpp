#pragma once
// Explicit little-endian byte codec for the binary formats.
//
// Three formats in this project ship bytes between machines -- the geohash index
// blob (the offline story, GAP 6), the offline event queue (GAP 6), and the
// Merkle evidence log (GAP 9) -- and all three documented themselves as
// "fixed little-endian" while actually doing `memcpy(&value, bytes, sizeof)`.
// That is HOST-endian. It happens to be little-endian on x86-64 and on Apple
// silicon, so the claim was never falsified by a test; write a blob on any
// big-endian machine, or read one there, and every integer and every coordinate
// comes back byte-reversed. The documentation was the aspiration, not the code.
//
// These helpers make the claim true by construction: values are assembled and
// disassembled with shifts, which have no endianness at all. Doubles go through
// the IEEE-754 bit pattern (the memcpy to a uint64_t is the standard,
// strict-aliasing-safe type pun), so a double is written as a defined sequence of
// eight bytes on every host.
//
// Reading is bounds-checked at every step and returns false rather than trusting
// a length field, because these files come off a device that may have been
// unplugged mid-write.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace safetrail::util {

// ── writing ─────────────────────────────────────────────────────────────────
inline void put_u8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

inline void put_u32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v));
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v >> 16));
  b.push_back(uint8_t(v >> 24));
}

inline void put_u64(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
}

inline void put_i64(std::vector<uint8_t>& b, int64_t v) {
  put_u64(b, uint64_t(v));                      // two's complement, defined in C++20
}

inline void put_f64(std::vector<uint8_t>& b, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof bits);          // IEEE-754 bit pattern
  put_u64(b, bits);
}

// ── reading ─────────────────────────────────────────────────────────────────
// Every getter advances `off` only on success, so a caller can chain them with
// && and a failure part-way leaves the offset where the error was.
struct Reader {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t off = 0;

  Reader() = default;
  Reader(const uint8_t* d, size_t n) : data(d), size(n) {}
  explicit Reader(const std::vector<uint8_t>& v) : data(v.data()), size(v.size()) {}

  size_t remaining() const { return size - off; }
  bool at_end() const { return off == size; }
  bool have(size_t n) const { return remaining() >= n; }

  bool u8(uint8_t* out) {
    if (!have(1)) return false;
    *out = data[off++];
    return true;
  }
  bool u32(uint32_t* out) {
    if (!have(4)) return false;
    *out = uint32_t(data[off]) | (uint32_t(data[off + 1]) << 8) |
           (uint32_t(data[off + 2]) << 16) | (uint32_t(data[off + 3]) << 24);
    off += 4;
    return true;
  }
  bool u64(uint64_t* out) {
    if (!have(8)) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(data[off + size_t(i)]) << (8 * i);
    off += 8;
    *out = v;
    return true;
  }
  bool i64(int64_t* out) {
    uint64_t v = 0;
    if (!u64(&v)) return false;
    *out = int64_t(v);
    return true;
  }
  bool f64(double* out) {
    uint64_t bits = 0;
    if (!u64(&bits)) return false;
    std::memcpy(out, &bits, sizeof bits);
    return true;
  }
  // Raw bytes, with the length already read. Refuses a length that cannot
  // possibly be satisfied BEFORE allocating for it -- a 2^32-byte length field in
  // a 40-byte file must be an error, not an out-of-memory abort.
  bool bytes(size_t n, std::vector<uint8_t>* out) {
    if (!have(n)) return false;
    out->assign(data + off, data + off + n);
    off += n;
    return true;
  }
};

}  // namespace safetrail::util
