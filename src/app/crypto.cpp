#include "safetrail/app/crypto.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace safetrail::app {

Hash hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len) {
  // Keys longer than the 64-byte block are hashed first; shorter ones are
  // zero-padded. Then H((K ^ opad) || H((K ^ ipad) || m)).
  uint8_t k[64] = {};
  if (key_len > 64) {
    const Hash kh = evidence::sha256(key, key_len);
    std::memcpy(k, kh.data(), kh.size());
  } else if (key_len) {
    std::memcpy(k, key, key_len);
  }
  std::vector<uint8_t> inner(64 + msg_len);
  for (size_t i = 0; i < 64; ++i) inner[i] = uint8_t(k[i] ^ 0x36);
  if (msg_len) std::memcpy(inner.data() + 64, msg, msg_len);
  const Hash ih = evidence::sha256(inner.data(), inner.size());

  uint8_t outer[64 + 32];
  for (size_t i = 0; i < 64; ++i) outer[i] = uint8_t(k[i] ^ 0x5c);
  std::memcpy(outer + 64, ih.data(), ih.size());
  return evidence::sha256(outer, sizeof outer);
}

std::vector<uint8_t> pbkdf2_sha256(const std::string& password, const std::vector<uint8_t>& salt,
                                   uint32_t iterations, size_t out_len) {
  if (iterations == 0) iterations = 1;
  const auto* pw = reinterpret_cast<const uint8_t*>(password.data());
  std::vector<uint8_t> out;
  out.reserve(out_len);
  std::vector<uint8_t> first(salt.size() + 4);
  if (!salt.empty()) std::memcpy(first.data(), salt.data(), salt.size());
  for (uint32_t block = 1; out.size() < out_len; ++block) {
    // U1 = PRF(P, S || INT(i)); T = U1 ^ U2 ^ ... ^ Uc
    first[salt.size() + 0] = uint8_t(block >> 24);
    first[salt.size() + 1] = uint8_t(block >> 16);
    first[salt.size() + 2] = uint8_t(block >> 8);
    first[salt.size() + 3] = uint8_t(block);
    Hash u = hmac_sha256(pw, password.size(), first.data(), first.size());
    Hash t = u;
    for (uint32_t i = 1; i < iterations; ++i) {
      u = hmac_sha256(pw, password.size(), u.data(), u.size());
      for (size_t j = 0; j < t.size(); ++j) t[j] = uint8_t(t[j] ^ u[j]);
    }
    for (size_t j = 0; j < t.size() && out.size() < out_len; ++j) out.push_back(t[j]);
  }
  return out;
}

bool constant_time_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff = uint8_t(diff | (a[i] ^ b[i]));
  return diff == 0;
}

bool constant_time_equal(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(a[i]) ^
                                              static_cast<unsigned char>(b[i])));
  return diff == 0;
}

bool random_bytes(uint8_t* out, size_t n) {
  FILE* f = std::fopen("/dev/urandom", "rb");
  if (!f) return false;
  const size_t got = std::fread(out, 1, n, f);
  std::fclose(f);
  return got == n;
}

std::string hex(const uint8_t* data, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s.push_back(d[data[i] >> 4]);
    s.push_back(d[data[i] & 15]);
  }
  return s;
}
std::string hex(const std::vector<uint8_t>& v) { return hex(v.data(), v.size()); }

bool from_hex(const std::string& s, std::vector<uint8_t>* out) {
  if (s.size() % 2) return false;
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out->clear();
  for (size_t i = 0; i < s.size(); i += 2) {
    const int hi = val(s[i]), lo = val(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out->push_back(uint8_t(hi * 16 + lo));
  }
  return true;
}

std::string hash_password(const std::string& password, uint32_t iterations) {
  std::vector<uint8_t> salt(16);
  if (!random_bytes(salt.data(), salt.size())) std::abort();   // fail closed
  const auto dk = pbkdf2_sha256(password, salt, iterations, 32);
  return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + hex(salt) + "$" + hex(dk);
}

bool verify_password(const std::string& password, const std::string& stored) {
  // Parse "pbkdf2-sha256$<it>$<salt>$<hash>" strictly; anything else fails.
  const std::string prefix = "pbkdf2-sha256$";
  if (stored.compare(0, prefix.size(), prefix) != 0) return false;
  const size_t a = prefix.size();
  const size_t b = stored.find('$', a);
  if (b == std::string::npos) return false;
  const size_t c = stored.find('$', b + 1);
  if (c == std::string::npos) return false;
  const std::string it_s = stored.substr(a, b - a);
  if (it_s.empty() || it_s.size() > 9) return false;
  for (char ch : it_s) if (ch < '0' || ch > '9') return false;
  const auto iterations = uint32_t(std::strtoul(it_s.c_str(), nullptr, 10));
  std::vector<uint8_t> salt, want;
  if (!from_hex(stored.substr(b + 1, c - b - 1), &salt)) return false;
  if (!from_hex(stored.substr(c + 1), &want) || want.empty()) return false;
  return constant_time_equal(pbkdf2_sha256(password, salt, iterations, want.size()), want);
}

}  // namespace safetrail::app
