#include "safetrail/evidence/merkle_log.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

#include "safetrail/util/bytes.hpp"

// RFC 6962 Merkle tree (the Certificate Transparency construction). The reason to
// follow it exactly rather than invent a hash chain: it gives CONSISTENCY proofs
// -- proof that the current log is an append-only extension of an earlier one --
// which is the property evidence actually needs and the one naive chains get
// wrong. Domain separation (0x00 leaves, 0x01 nodes) lives in sha256.cpp.
namespace safetrail::evidence {

Hash leaf_hash(const uint8_t* data, size_t len) {
  std::vector<uint8_t> buf(len + 1);
  buf[0] = 0x00;
  if (len) std::memcpy(buf.data() + 1, data, len);
  return sha256(buf.data(), buf.size());
}

namespace {
// Merkle Tree Hash over leaves[lo, hi). RFC 6962 §2.1.
Hash mth(const std::vector<Hash>& leaves, size_t lo, size_t hi) {
  const size_t n = hi - lo;
  if (n == 0) return sha256(nullptr, 0);   // MTH({}) = SHA256()
  if (n == 1) return leaves[lo];           // leaves are already leaf-hashed
  size_t k = 1;                            // largest power of two < n
  while (k << 1 < n) k <<= 1;
  return sha256_pair(mth(leaves, lo, lo + k), mth(leaves, lo + k, hi));
}

// Inclusion path for leaf m within leaves[lo, hi). RFC 6962 §2.1.1.
void path(const std::vector<Hash>& lv, size_t m, size_t lo, size_t hi,
          std::vector<Hash>& out) {
  const size_t n = hi - lo;
  if (n <= 1) return;
  size_t k = 1; while (k << 1 < n) k <<= 1;
  if (m < k) { path(lv, m, lo, lo + k, out); out.push_back(mth(lv, lo + k, hi)); }
  else       { path(lv, m - k, lo + k, hi, out); out.push_back(mth(lv, lo, lo + k)); }
}

// Consistency subproof. RFC 6962 §2.1.2.
void subproof(const std::vector<Hash>& lv, size_t m, size_t lo, size_t hi,
              bool b, std::vector<Hash>& out) {
  const size_t n = hi - lo;
  if (m == n) { if (!b) out.push_back(mth(lv, lo, hi)); return; }
  size_t k = 1; while (k << 1 < n) k <<= 1;
  if (m <= k) { subproof(lv, m, lo, lo + k, b, out); out.push_back(mth(lv, lo + k, hi)); }
  else        { subproof(lv, m - k, lo + k, hi, false, out); out.push_back(mth(lv, lo, lo + k)); }
}
}  // namespace

uint64_t MerkleLog::append(const std::vector<uint8_t>& entry) {
  leaves_.push_back(leaf_hash(entry.data(), entry.size()));
  entries_.push_back(entry);
  return leaves_.size() - 1;
}
uint64_t MerkleLog::append(const std::string& e) {
  return append(std::vector<uint8_t>(e.begin(), e.end()));
}

Hash MerkleLog::root() const { return mth(leaves_, 0, leaves_.size()); }

InclusionProof MerkleLog::prove(uint64_t index) const {
  InclusionProof p;
  p.index = index;
  p.size = leaves_.size();
  if (index < leaves_.size()) path(leaves_, index, 0, leaves_.size(), p.path);
  return p;
}

bool MerkleLog::get(uint64_t index, std::vector<uint8_t>& out) const {
  if (index >= entries_.size()) return false;
  out = entries_[index];
  return true;
}

// Recompute the root from a leaf hash + sibling path, mirroring how `path` was
// generated: at each level the sibling is on the left or right depending on the
// position bit. Self-contained -- no log access, works offline.
bool InclusionProof::verify(const Hash& leaf, const Hash& root) const {
  if (index >= size) return false;
  // RFC 6962 §2.1.1 verification. The proof is ordered leaf -> root, so we rebuild
  // bottom-up: fn tracks our node's index, sn the last index, at each level.
  uint64_t fn = index, sn = size - 1;
  Hash h = leaf;
  for (const Hash& p : path) {
    if (sn == 0) return false;                 // more proof nodes than the tree has
    if ((fn & 1) || fn == sn) {
      h = sha256_pair(p, h);                   // sibling on the left
      if (!(fn & 1)) while (!(fn & 1)) { fn >>= 1; sn >>= 1; }
    } else {
      h = sha256_pair(h, p);                   // sibling on the right
    }
    fn >>= 1; sn >>= 1;
  }
  return sn == 0 && h == root;
}

std::vector<Hash> MerkleLog::prove_consistency(uint64_t old_size) const {
  std::vector<Hash> proof;
  if (old_size == 0 || old_size >= leaves_.size()) return proof;
  subproof(leaves_, old_size, 0, leaves_.size(), true, proof);
  return proof;
}

// Verify that (old_size, old_root) is a prefix of (new_size, new_root). RFC 6962
// §2.1.2 verification. This is the forensically important check: it proves the
// authority APPENDED and never rewrote history.
bool MerkleLog::verify_consistency(uint64_t old_size, const Hash& old_root,
                                   uint64_t new_size, const Hash& new_root,
                                   const std::vector<Hash>& proof) {
  if (old_size > new_size) return false;
  if (old_size == new_size) return proof.empty() && old_root == new_root;
  if (old_size == 0) return true;               // empty is a prefix of anything

  // Canonical Certificate-Transparency consistency verification.
  uint64_t node = old_size - 1, last = new_size - 1;
  while (node & 1) { node >>= 1; last >>= 1; }   // rebase past right children

  size_t it = 0;
  Hash h1{}, h2{};
  if (node) {
    if (it >= proof.size()) return false;
    h1 = h2 = proof[it++];
  } else {
    h1 = h2 = old_root;                          // old_size is a power of two
  }

  while (node) {
    if (node & 1) {                              // right child
      if (it >= proof.size()) return false;
      h1 = sha256_pair(proof[it], h1);
      h2 = sha256_pair(proof[it], h2);
      ++it;
    } else if (node < last) {                    // left child with a right sibling
      if (it >= proof.size()) return false;
      h2 = sha256_pair(h2, proof[it]);
      ++it;
    }                                            // else: left child, no sibling
    node >>= 1; last >>= 1;
  }
  while (last) {                                 // ascend snapshot2's right siblings
    if (it >= proof.size()) return false;
    h2 = sha256_pair(h2, proof[it]);
    ++it;
    last >>= 1;
  }
  return h1 == old_root && h2 == new_root && it == proof.size();
}

// ── On-disk format ────────────────────────────────────────────────────────────
//   ["MKL1" magic u32][count u64] then count x { len u64, len bytes }
//
// Explicitly little-endian (util/bytes.hpp) and validated on the way in. The old
// version wrote native integers and then did `std::vector<uint8_t> e(len)` on an
// unvalidated 64-bit length, so a corrupt or hostile file was an out-of-memory
// abort rather than a parse error -- in the module whose entire purpose is
// tamper EVIDENCE, which makes "reads a tampered file safely" part of the job
// rather than a nicety.
//
// Load rebuilds into a temporary log and commits only on success, so a truncated
// file cannot leave a half-loaded log whose root looks plausible.
namespace {
constexpr uint32_t kLogMagic = 0x314C4B4D;         // "MKL1"
constexpr uint64_t kMaxEntryBytes = 1u << 24;      // 16 MB, far past any event record
constexpr uint64_t kMaxEntries    = 1u << 26;      // 67M entries
}  // namespace

bool MerkleLog::save(const std::string& path) const {
  std::vector<uint8_t> buf;
  util::put_u32(buf, kLogMagic);
  util::put_u64(buf, uint64_t(entries_.size()));
  for (const auto& e : entries_) {
    util::put_u64(buf, uint64_t(e.size()));
    buf.insert(buf.end(), e.begin(), e.end());
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  if (!buf.empty())
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
  return bool(f);
}

bool MerkleLog::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string blob = ss.str();
  util::Reader r(reinterpret_cast<const uint8_t*>(blob.data()), blob.size());

  uint32_t magic = 0;
  uint64_t n = 0;
  if (!r.u32(&magic) || magic != kLogMagic) return false;
  if (!r.u64(&n) || n > kMaxEntries) return false;

  MerkleLog loaded;
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t len = 0;
    if (!r.u64(&len)) return false;
    if (len > kMaxEntryBytes) return false;        // refuse before allocating
    std::vector<uint8_t> e;
    if (len && !r.bytes(size_t(len), &e)) return false;
    loaded.append(e);
  }
  if (!r.at_end()) return false;                   // trailing garbage

  leaves_ = std::move(loaded.leaves_);
  entries_ = std::move(loaded.entries_);
  return true;
}

}  // namespace safetrail::evidence
