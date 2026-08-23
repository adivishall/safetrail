#pragma once
// Open-addressed hash table -- O(1) expected entity lookup.
//
// The engine looks entities up by id constantly: zone by ZoneId, tourist by
// TouristId, responder by ResponderId. A flat vector indexed by id works only
// while ids are dense and small; once they are sparse (after churn, or with
// externally assigned ids) you want a hash table, and std::unordered_map is off
// the table -- it is one of the structures the course is about.
//
// Open addressing with linear probing, not separate chaining: no per-node
// allocation, everything in one cache-friendly array, which is the right choice
// for the small POD keys/values here. Deletion uses tombstones (a deleted slot
// stays probeable so it does not break a probe chain that runs through it), and
// the table grows and rehashes at a 0.7 load factor to keep probe chains short.
//
// Hashing is hand-written (Fibonacci hashing for integers, FNV-1a for strings) so
// the structure owns its whole behaviour.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace safetrail::ds {

// ── Hashing ───────────────────────────────────────────────────────────────────
struct Hash {
  size_t operator()(uint32_t k) const { return mix(k); }
  size_t operator()(int32_t k)  const { return mix(uint32_t(k)); }
  size_t operator()(uint64_t k) const { return size_t(k * 0x9E3779B97F4A7C15ull); }
  size_t operator()(const std::string& s) const {
    uint64_t h = 1469598103934665603ull;               // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return size_t(h);
  }
 private:
  static size_t mix(uint32_t k) { return size_t(k * 2654435769u); }   // Knuth/Fibonacci
};

template <typename K, typename V, typename H = Hash>
class HashMap {
 public:
  HashMap() { slots_.resize(kInitial); }

  size_t size() const { return count_; }
  bool   empty() const { return count_ == 0; }

  // Insert or overwrite. Returns true if the key was newly inserted.
  bool put(const K& key, const V& value) {
    if ((count_ + tombstones_ + 1) * 10 >= slots_.size() * 7) grow();
    size_t mask = slots_.size() - 1;
    size_t i = H{}(key) & mask;
    int64_t first_tomb = -1;
    for (;;) {
      Slot& s = slots_[i];
      if (s.state == Empty) {
        Slot& dst = first_tomb >= 0 ? slots_[size_t(first_tomb)] : s;
        if (first_tomb >= 0) --tombstones_;
        dst.state = Full; dst.key = key; dst.value = value;
        ++count_;
        return true;
      }
      if (s.state == Tomb) { if (first_tomb < 0) first_tomb = int64_t(i); }
      else if (s.key == key) { s.value = value; return false; }   // overwrite
      i = (i + 1) & mask;
    }
  }

  // Pointer to the value, or nullptr if absent. Invalidated by the next put/grow.
  V* get(const K& key) {
    const int64_t i = find(key);
    return i < 0 ? nullptr : &slots_[size_t(i)].value;
  }
  const V* get(const K& key) const {
    const int64_t i = find(key);
    return i < 0 ? nullptr : &slots_[size_t(i)].value;
  }
  bool contains(const K& key) const { return find(key) >= 0; }

  bool erase(const K& key) {
    const int64_t i = find(key);
    if (i < 0) return false;
    slots_[size_t(i)].state = Tomb;   // tombstone, not Empty -- preserve probe chains
    --count_;
    ++tombstones_;
    return true;
  }

  // Visit every live entry. Order is unspecified (it is a hash table).
  template <typename Fn>
  void for_each(Fn fn) const {
    for (const auto& s : slots_) if (s.state == Full) fn(s.key, s.value);
  }

 private:
  enum State : uint8_t { Empty, Full, Tomb };
  struct Slot { State state = Empty; K key{}; V value{}; };

  static constexpr size_t kInitial = 16;   // power of two
  std::vector<Slot> slots_;
  size_t count_ = 0, tombstones_ = 0;

  int64_t find(const K& key) const {
    const size_t mask = slots_.size() - 1;
    size_t i = H{}(key) & mask;
    for (size_t probes = 0; probes <= mask; ++probes) {
      const Slot& s = slots_[i];
      if (s.state == Empty) return -1;                 // a truly empty slot ends the chain
      if (s.state == Full && s.key == key) return int64_t(i);
      i = (i + 1) & mask;
    }
    return -1;
  }

  void grow() {
    std::vector<Slot> old = std::move(slots_);
    slots_.assign(old.size() * 2, Slot{});
    count_ = 0; tombstones_ = 0;
    for (auto& s : old) if (s.state == Full) put(s.key, s.value);
  }
};

}  // namespace safetrail::ds
