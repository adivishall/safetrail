#pragma once
// Tourist digital ID -- QR payload encode/decode + fully offline verification.  [GAP 9]
//
// The identity story existing implementations reach for is an Ethereum smart
// contract: an on-chain record, checked by calling out to a node. That requires
// exactly the connectivity this problem statement does not have.
//
// What actually solves it is already built: MerkleLog (merkle_log.hpp). A
// tourist's identity record is appended once at registration; the QR code prints
// on their permit encodes just enough to prove that record is in the log --
// against a ROOT the responder's device already has cached, with zero access to
// the log itself. That is genuinely fully offline verification, not "offline
// with a cached copy of the database."
//
// Scope note: this module produces and verifies the QR PAYLOAD -- the string a
// QR code would encode, and the logic that checks it. Rendering that string as a
// scannable bitmap (finder patterns, Reed-Solomon error correction, module
// placement) is a barcode-symbology problem, not a data-structures one, and pulls
// in scope well outside the course; it is not attempted here. The payload is
// plain, inspectable text for exactly that reason -- any QR library encodes it
// as-is in one line, on either end.
#include <cstdint>
#include <string>
#include <vector>
#include "safetrail/evidence/merkle_log.hpp"

namespace safetrail::evidence {

// The identity record committed to the log: id string + issue time. Kept as a
// simple length-prefixed encoding so it round-trips exactly (a raw digital id can
// contain any bytes).
std::vector<uint8_t> make_identity_record(const std::string& tourist_digital_id, int64_t issued_ms);
bool parse_identity_record(const std::vector<uint8_t>& record, std::string* digital_id, int64_t* issued_ms);

struct QrPayload {
  uint64_t index = 0;         // leaf position
  uint64_t size  = 0;         // log size the proof was issued against
  std::vector<Hash> path;     // inclusion proof, leaf -> root
};

// Build the QR payload for an already-appended record (see MerkleLog::append).
QrPayload encode_qr(const MerkleLog& log, uint64_t index);

// Compact text encoding/decoding -- what the QR image actually carries.
//   "SFTL-ID1|<index>|<size>|<hex>|<hex>|..."
std::string to_string(const QrPayload& qr);
bool from_string(const std::string& s, QrPayload* out);

// Fully offline verification: the responder holds only `cached_root` (published
// periodically, e.g. printed on a noticeboard or synced whenever signal was last
// available) and whatever record the tourist presents (their permit's printed id
// + issue time, or the QR's own encoded record if carried alongside). No log
// object, no network. Returns false with *error set on any mismatch.
bool verify_qr(const QrPayload& qr, const std::vector<uint8_t>& presented_record,
               const Hash& cached_root, std::string* error = nullptr);

}  // namespace safetrail::evidence
