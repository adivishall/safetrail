// Tourist digital ID: QR payload encode/decode + fully offline verification.  [GAP 9]
//
// The property under test is the one that actually matters: a responder with
// ONLY a cached root and the QR payload -- no log object, no network -- can
// verify a genuine ID and reject a tampered one.
#include <string>
#include <vector>
#include "safetrail/evidence/digital_id.hpp"
#include "../test_harness.hpp"

using namespace safetrail::evidence;

int main() {
  // ── Identity record round-trips exactly ─────────────────────────────────────
  {
    auto rec = make_identity_record("TID-04217", 1'724'000'000'000);
    std::string id; int64_t issued = 0;
    t::ok(parse_identity_record(rec, &id, &issued), "record parses");
    t::ok(id == "TID-04217", "digital id round-trips");
    t::ok(issued == 1'724'000'000'000, "issue time round-trips");
  }
  // A digital id containing the delimiter-like bytes still round-trips (length-
  // prefixed, not delimiter-based).
  {
    auto rec = make_identity_record(std::string("A|B\0C", 5), -1);
    std::string id; int64_t issued = 0;
    t::ok(parse_identity_record(rec, &id, &issued) && id.size() == 5,
          "length-prefixed record survives embedded odd bytes");
  }

  // ── Register a batch of tourists, build the log ─────────────────────────────
  MerkleLog log;
  std::vector<std::vector<uint8_t>> records;
  for (int i = 0; i < 40; ++i) {
    auto rec = make_identity_record("TID-" + std::to_string(10000 + i), 1000 * i);
    log.append(rec);
    records.push_back(rec);
  }
  const Hash cached_root = log.root();   // what a responder's device has, offline

  // ── Every genuine QR verifies fully offline ─────────────────────────────────
  int ok_count = 0;
  for (uint64_t i = 0; i < log.size(); ++i) {
    const QrPayload qr = encode_qr(log, i);
    std::string err;
    if (verify_qr(qr, records[size_t(i)], cached_root, &err)) ++ok_count;
  }
  t::ok(ok_count == 40, "every genuine tourist QR verifies against the cached root");

  // ── Text payload round-trips (this is literally what the QR encodes) ────────
  {
    const QrPayload qr = encode_qr(log, 17);
    const std::string text = to_string(qr);
    t::ok(text.rfind("SFTL-ID1|17|40|", 0) == 0, "payload text has the expected header");
    QrPayload back;
    t::ok(from_string(text, &back), "payload text parses back");
    t::ok(back.index == qr.index && back.size == qr.size && back.path == qr.path,
          "decoded payload matches the original exactly");
    std::string err;
    t::ok(verify_qr(back, records[17], cached_root, &err),
          "verification works from the round-tripped TEXT payload, not just the struct");
  }

  // ── Tamper detection: a forged/altered id fails ─────────────────────────────
  {
    const QrPayload qr = encode_qr(log, 5);
    auto forged = make_identity_record("TID-99999", 5000);   // wrong id entirely
    std::string err;
    t::ok(!verify_qr(qr, forged, cached_root, &err), "forged record is rejected");
    t::ok(!err.empty(), "rejection carries an error message");
  }
  {
    // Presenting the RIGHT id but claiming a DIFFERENT tourist's QR proof.
    const QrPayload qr_for_5 = encode_qr(log, 5);
    t::ok(!verify_qr(qr_for_5, records[6], cached_root, nullptr),
          "record 6 does not verify under record 5's proof");
  }
  {
    // A single flipped byte in the proof path is rejected.
    QrPayload qr = encode_qr(log, 5);
    if (!qr.path.empty()) qr.path[0][0] ^= 0xFF;
    t::ok(!verify_qr(qr, records[5], cached_root, nullptr), "corrupted proof path is rejected");
  }
  {
    // A stale/wrong cached root is rejected even for a genuine record+proof.
    const QrPayload qr = encode_qr(log, 5);
    Hash wrong_root = cached_root;
    wrong_root[0] ^= 0xFF;
    t::ok(!verify_qr(qr, records[5], wrong_root, nullptr), "wrong cached root is rejected");
  }

  // ── Garbage text payloads are rejected, not crashed on ──────────────────────
  {
    QrPayload out;
    t::ok(!from_string("not a payload", &out), "non-payload text is rejected");
    t::ok(!from_string("SFTL-ID1|abc|40", &out), "non-numeric index is rejected");
    t::ok(!from_string("SFTL-ID1|5|40|zz", &out), "invalid hex in the proof path is rejected");
  }

  return t::report("evidence/digital_id");
}
