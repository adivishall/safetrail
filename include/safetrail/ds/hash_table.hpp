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
// the table rehashes at a 0.7 load factor to keep probe chains short.
//
// ── Tombstones, and why the rehash policy has two cases ──────────────────────
//
// A tombstone occupies a slot for probing purposes without holding a live entry,
// so under insert/delete churn the table fills with them while the live count
// stays flat. The load factor that triggers a rehash counts tombstones -- it has
// to, since probe length depends on occupied slots, not live ones -- so a churning
// table hits the threshold repeatedly.
//
// The bug was what happened next: the rehash always DOUBLED. A workload that
// inserts and erases the same 100 keys forever therefore doubled the table
// forever, growing without bound while holding 100 entries, purely to make room
// for the corpses. Memory grew, cache locality collapsed, and lookups got slower
// the longer the table ran -- the classic open-addressing failure mode.
//
// The fix is to ask what actually filled the table. The trigger fires on
// count + tombstones; the decision is made on count alone:
//
//   live entries alone justify a bigger table  ->  grow (double)
//   they do not, so it is tombstones           ->  rehash at the SAME size,
//                                                  which sweeps them away
//
// Rebuilding in place is O(n) and happens at most once per O(size) deletions, so
// it is O(1) amortised -- the same argument that justifies doubling. The
// alternative, deleting by backward-shift instead of tombstoning, avoids the
// problem entirely but costs an unbounded shift per erase and is far easier to
// get wrong; this is the standard trade. bench/results/hash_churn.csv measures
// the before/after.
//
// Shrinking is deliberately NOT done: a table that halves as soon as it is half
// empty thrashes on a workload that oscillates around the boundary, and nothing
// here holds a table long enough after a bulk delete for the memory to matter.
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

  // Exposed for the churn benchmark and the tests: the invariant worth asserting
  // is that a bounded live set keeps a bounded table, however much it churns.
  size_t bucket_count() const { return slots_.size(); }
  size_t tombstones() const { return tombstones_; }
  size_t rehashes() const { return rehashes_; }
  size_t growths() const { return growths_; }

  // Insert or overwrite. Returns true if the key was newly inserted.
  bool put(const K& key, const V& value) {
    if ((count_ + tombstones_ + 1) * 10 >= slots_.size() * 7) maybe_rehash();
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
  size_t rehashes_ = 0, growths_ = 0;

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

  // See the header note. Grow only if the LIVE entries need a bigger table;
  // otherwise rebuild at the same size to sweep out tombstones.
  void maybe_rehash() {
    const size_t n = slots_.size();
    const bool live_needs_growth = (count_ + 1) * 10 >= n * 7;
    rehash(live_needs_growth ? n * 2 : n);
    if (live_needs_growth) ++growths_; else ++rehashes_;
  }

  void rehash(size_t new_size) {
    std::vector<Slot> old = std::move(slots_);
    slots_.assign(new_size, Slot{});
    count_ = 0; tombstones_ = 0;
    // Reinsert live entries only; tombstones are simply not carried over, which
    // is the whole point. put() cannot recurse into maybe_rehash() here because
    // the destination is sized for these entries by construction.
    for (auto& s : old) if (s.state == Full) put(s.key, s.value);
  }
};

}  // namespace safetrail::ds
