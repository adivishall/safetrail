#include "safetrail/evidence/digital_id.hpp"

#include <cstring>
#include <sstream>

namespace safetrail::evidence {

namespace {
void put_u32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}
bool get_u32(const std::vector<uint8_t>& b, size_t& off, uint32_t& v) {
  if (off + 4 > b.size()) return false;
  v = 0;
  for (int i = 0; i < 4; ++i) v |= uint32_t(b[off + size_t(i)]) << (8 * i);
  off += 4;
  return true;
}
bool get_i64(const std::vector<uint8_t>& b, size_t& off, int64_t& v) {
  if (off + 8 > b.size()) return false;
  uint64_t u = 0;
  for (int i = 0; i < 8; ++i) u |= uint64_t(b[off + size_t(i)]) << (8 * i);
  off += 8;
  v = int64_t(u);
  return true;
}
void put_i64(std::vector<uint8_t>& b, int64_t v) {
  uint64_t u = uint64_t(v);
  for (int i = 0; i < 8; ++i) b.push_back(uint8_t((u >> (8 * i)) & 0xFF));
}

std::string hex_of(const uint8_t* p, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { s += kHex[p[i] >> 4]; s += kHex[p[i] & 0xF]; }
  return s;
}
bool hex_digit(char c, int& v) {
  if (c >= '0' && c <= '9') { v = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
  return false;
}
bool from_hex(const std::string& s, std::vector<uint8_t>& out) {
  if (s.size() % 2) return false;
  out.resize(s.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    int hi, lo;
    if (!hex_digit(s[2 * i], hi) || !hex_digit(s[2 * i + 1], lo)) return false;
    out[i] = uint8_t((hi << 4) | lo);
  }
  return true;
}
}  // namespace

std::vector<uint8_t> make_identity_record(const std::string& tourist_digital_id, int64_t issued_ms) {
  std::vector<uint8_t> r;
  put_u32(r, uint32_t(tourist_digital_id.size()));
  r.insert(r.end(), tourist_digital_id.begin(), tourist_digital_id.end());
  put_i64(r, issued_ms);
  return r;
}

bool parse_identity_record(const std::vector<uint8_t>& record, std::string* digital_id, int64_t* issued_ms) {
  size_t off = 0;
  uint32_t len = 0;
  if (!get_u32(record, off, len)) return false;
  if (off + len > record.size()) return false;
  if (digital_id) digital_id->assign(record.begin() + long(off), record.begin() + long(off + len));
  off += len;
  int64_t t = 0;
  if (!get_i64(record, off, t)) return false;
  if (issued_ms) *issued_ms = t;
  return off == record.size();
}

QrPayload encode_qr(const MerkleLog& log, uint64_t index) {
  QrPayload qr;
  qr.index = index;
  qr.size = log.size();
  const InclusionProof p = log.prove(index);
  qr.path = p.path;
  return qr;
}

std::string to_string(const QrPayload& qr) {
  std::ostringstream os;
  os << "SFTL-ID1|" << qr.index << "|" << qr.size;
  for (const auto& h : qr.path) os << "|" << hex_of(h.data(), h.size());
  return os.str();
}

bool from_string(const std::string& s, QrPayload* out) {
  if (!out) return false;
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == '|') { parts.push_back(s.substr(start, i - start)); start = i + 1; }
  }
  if (parts.size() < 3 || parts[0] != "SFTL-ID1") return false;
  try {
    out->index = std::stoull(parts[1]);
    out->size = std::stoull(parts[2]);
  } catch (...) { return false; }
  out->path.clear();
  for (size_t i = 3; i < parts.size(); ++i) {
    std::vector<uint8_t> raw;
    if (!from_hex(parts[i], raw) || raw.size() != 32) return false;
    Hash h{};
    std::memcpy(h.data(), raw.data(), 32);
    out->path.push_back(h);
  }
  return true;
}

bool verify_qr(const QrPayload& qr, const std::vector<uint8_t>& presented_record,
               const Hash& cached_root, std::string* error) {
  auto fail = [&](const char* m) { if (error) *error = m; return false; };
  if (qr.index >= qr.size) return fail("index out of range for the log size in the payload");

  const Hash leaf = leaf_hash(presented_record.data(), presented_record.size());

  InclusionProof p;
  p.index = qr.index;
  p.size = qr.size;
  p.path = qr.path;
  if (!p.verify(leaf, cached_root)) return fail("inclusion proof does not verify against the cached root");
  return true;
}

}  // namespace safetrail::evidence
