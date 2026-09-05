// GAP 9: tamper-evident append-only log. The properties that matter for evidence:
//   - a valid inclusion proof verifies; a tampered entry does NOT
//   - the proof is self-contained (root + proof only, no log access)
//   - consistency proofs show the log was APPENDED to, never rewritten
#include "../test_harness.hpp"
#include "safetrail/evidence/merkle_log.hpp"
#include <string>
#include <vector>
using namespace safetrail::evidence;

static Hash lh(const std::string& s) {
  return leaf_hash((const uint8_t*)s.data(), s.size());
}

int main() {
  t::ok(to_hex(sha256((const uint8_t*)"abc",3)) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256(\"abc\") matches NIST vector");

  // ── The empty input, which is a value this code depends on ─────────────────
  //
  // RFC 6962 §2.1 defines MTH({}) = SHA256(), so the root of an empty log IS the
  // empty-input digest -- it is not an edge case to be tolerated, it is part of
  // the specification. It is also how a null pointer reaches sha256(), which was
  // undefined behaviour (`data + 0` and `memcpy(dst, nullptr, 0)` are both UB on
  // a null pointer even at length zero). It worked on every machine anyone ran
  // it on until a gating UBSan build said otherwise. Pinning the VALUE, not just
  // the absence of a crash, is what makes the fix checkable.
  {
    const std::string empty_digest =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    t::ok(to_hex(sha256(nullptr, 0)) == empty_digest,
          "sha256 of the empty input, via a null pointer, matches the NIST vector");
    const uint8_t nothing = 0;
    t::ok(to_hex(sha256(&nothing, 0)) == empty_digest,
          "...and via a valid pointer with zero length, identically");
    MerkleLog empty;
    t::ok(empty.size() == 0, "an empty log has no entries");
    t::ok(to_hex(empty.root()) == empty_digest,
          "MTH({}) = SHA256() -- the empty log's root is the empty digest (RFC 6962)");
  }

  MerkleLog log;
  std::vector<std::string> ids;
  for (int i = 0; i < 13; ++i) ids.push_back("TID-" + std::to_string(1000 + i));
  for (auto& id : ids) log.append(id);
  t::ok(log.size() == 13, "13 entries appended");

  const Hash root = log.root();
  int verified = 0;
  for (uint64_t i = 0; i < log.size(); ++i)
    if (log.prove(i).verify(lh(ids[i]), root)) ++verified;
  t::ok(verified == 13, "every genuine entry verifies against the root");
  t::ok(log.prove(7).path.size() <= 4, "inclusion path is O(log n) (<=4 for n=13)");

  InclusionProof p5 = log.prove(5);
  t::ok(p5.verify(lh(ids[5]), root),        "real entry 5 verifies");
  t::ok(!p5.verify(lh("TID-FORGED"), root), "a forged entry 5 does NOT verify");
  t::ok(!p5.verify(lh(ids[6]), root),       "entry 6's hash at position 5 does NOT verify");
  Hash bad_root = root; bad_root[0] ^= 0xFF;
  t::ok(!p5.verify(lh(ids[5]), bad_root),   "real entry fails against a wrong root");

  MerkleLog log2;
  for (int i = 0; i < 6; ++i) log2.append("e" + std::to_string(i));
  const Hash old_root = log2.root();
  const uint64_t old_size = log2.size();
  for (int i = 6; i < 20; ++i) log2.append("e" + std::to_string(i));
  const Hash new_root = log2.root();
  auto cproof = log2.prove_consistency(old_size);
  t::ok(MerkleLog::verify_consistency(old_size, old_root, log2.size(), new_root, cproof),
        "consistency: 20-entry log extends the 6-entry log");

  MerkleLog forged;
  for (int i = 0; i < 6; ++i) forged.append(i == 3 ? "TAMPERED" : "e" + std::to_string(i));
  for (int i = 6; i < 20; ++i) forged.append("e" + std::to_string(i));
  t::ok(!MerkleLog::verify_consistency(old_size, old_root, forged.size(),
                                       forged.root(), forged.prove_consistency(old_size)),
        "a rewritten past entry FAILS consistency against the old root");

  // consistency across many (old,new) pairs, incl. power-of-two boundaries
  // (old_size = 1,2,4,8 exercise the "old root is an implicit node" branch)
  {
    MerkleLog L;
    std::vector<Hash> roots; roots.push_back(L.root());
    for (int i = 0; i < 17; ++i) { L.append("x" + std::to_string(i)); roots.push_back(L.root()); }
    int good = 0, pairs = 0;
    for (uint64_t o = 1; o < 17; ++o)
      for (uint64_t n = o + 1; n <= 17; ++n) {
        // rebuild a log of size n, prove consistency with prefix o
        MerkleLog Ln;
        for (uint64_t i = 0; i < n; ++i) Ln.append("x" + std::to_string(i));
        auto pr = Ln.prove_consistency(o);
        ++pairs;
        if (MerkleLog::verify_consistency(o, roots[o], n, Ln.root(), pr)) ++good;
      }
    t::ok(good == pairs, "consistency holds for ALL (old,new) prefix pairs (" +
          std::to_string(good) + "/" + std::to_string(pairs) + ")");
  }

  const std::string path = "/tmp/safetrail_merkle_test.bin";
  t::ok(log.save(path), "log saves to disk");
  MerkleLog loaded;
  t::ok(loaded.load(path), "log loads from disk");
  t::ok(loaded.root() == root, "loaded log reproduces the same root");

  return t::report("evidence/merkle_log");
}
