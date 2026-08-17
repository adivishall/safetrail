#pragma once
//
// Append-only tamper-evident log.  [GAP 9]
//
// Existing implementations reach for Ethereum via Hardhat to prove an identity
// record was not altered. That imports gas costs, block latency, and a hard
// dependency on network connectivity — into a system whose defining constraint is
// that connectivity is absent. The requirement was never "a blockchain". It was
// tamper-evidence.
//
// A Merkle tree over an append-only log delivers exactly that:
//
//   - O(log n) inclusion proof — "entry 4,192 is in a log with this root"
//   - verifiable entirely offline, with no network and no chain
//   - O(1) amortised append
//   - about 200 lines, all of it ours
//
// Two uses:
//   1. Tourist digital IDs. QR encodes (entry index, proof path). A responder
//      with no signal can still verify the ID against a cached root.
//   2. The incident event stream, so an investigation can show the log was not
//      edited after the fact — which is the actual forensic requirement.
//
// What this does NOT give you: distributed consensus. Nobody in this problem
// needs it. A single authority appending to a log it publishes roots for is the
// correct trust model, and saying so plainly is a better answer than a chain.
//
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace safetrail::evidence {

using Hash = std::array<uint8_t, 32>;

// SHA-256, implemented in src/evidence/sha256.cpp. No OpenSSL — it is ~150 lines
// and importing a crypto library to hash 32 bytes undercuts the point.
Hash sha256(const uint8_t* data, size_t len);
Hash sha256_pair(const Hash& l, const Hash& r);
std::string to_hex(const Hash& h);

// ─── Inclusion proof ────────────────────────────────────────────────────────
struct InclusionProof {
  uint64_t          index = 0;      // leaf position
  uint64_t          size  = 0;      // log size the proof was issued against
  std::vector<Hash> path;           // sibling hashes, leaf → root. O(log n).

  // Self-contained: needs only the proof and the published root. No log access,
  // no network. This is the property that makes it work offline.
  bool verify(const Hash& leaf, const Hash& root) const;
};

// ─── The log ────────────────────────────────────────────────────────────────
class MerkleLog {
 public:
  uint64_t append(const std::vector<uint8_t>& entry);   // returns index
  uint64_t append(const std::string& entry);

  Hash root() const;                                    // O(log n)
  uint64_t size() const { return leaves_.size(); }

  InclusionProof prove(uint64_t index) const;           // O(log n)
  bool get(uint64_t index, std::vector<uint8_t>& out) const;

  // Consistency proof: old_root at old_size is a prefix of the current log.
  // Proves the authority APPENDED rather than rewrote history — the property
  // that actually matters for evidence, and the one a naive hash-chain
  // implementation usually gets wrong.
  std::vector<Hash> prove_consistency(uint64_t old_size) const;
  static bool verify_consistency(uint64_t old_size, const Hash& old_root,
                                 uint64_t new_size, const Hash& new_root,
                                 const std::vector<Hash>& proof);

  bool save(const std::string& path) const;
  bool load(const std::string& path);

 private:
  std::vector<Hash>                 leaves_;
  std::vector<std::vector<uint8_t>> entries_;
  mutable std::vector<Hash>         cached_level_;   // incremental root cache
};

}  // namespace safetrail::evidence
