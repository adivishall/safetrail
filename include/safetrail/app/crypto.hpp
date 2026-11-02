#pragma once
// Password hashing, message authentication and session tokens.
//
// Built on the project's own SHA-256 (src/evidence/sha256.cpp) for the same
// reason everything else is hand-written -- but these are STANDARD constructions
// with published test vectors, not inventions, and tests/app/crypto_test.cpp
// checks each one against those vectors:
//
//   hmac_sha256      RFC 2104, vectors from RFC 4231
//   pbkdf2_sha256    RFC 8018 (PKCS #5 v2.1), vectors from RFC 7914 §11
//
// What this is NOT: a memory-hard password hash. PBKDF2 slows an offline
// guesser by its iteration count and nothing more; a GPU still runs it in
// parallel. Argon2id is the production answer and docs/final/SECURITY_AND_PRIVACY.md
// says so. PBKDF2-HMAC-SHA256 is what NIST SP 800-132 still permits, and it is
// small enough to implement and verify here.
//
// Randomness comes from the operating system (/dev/urandom), never from a
// seeded PRNG: a session token predictable from the process start time is a
// session token anyone can forge. If the OS source cannot be read, random_bytes
// returns false and the caller must refuse to continue -- fail closed.
#include <cstdint>
#include <string>
#include <vector>
#include "safetrail/evidence/merkle_log.hpp"

namespace safetrail::app {

using evidence::Hash;

Hash hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len);

// Derives `out_len` bytes. iterations >= 1.
std::vector<uint8_t> pbkdf2_sha256(const std::string& password, const std::vector<uint8_t>& salt,
                                   uint32_t iterations, size_t out_len);

// Comparison whose running time does not depend on WHERE two equal-length
// inputs first differ, so a timing side channel cannot recover a hash or token
// byte by byte. Unequal lengths return false at once (lengths are not secret).
bool constant_time_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
bool constant_time_equal(const std::string& a, const std::string& b);

// Cryptographically secure random bytes from the OS. false = unavailable.
bool random_bytes(uint8_t* out, size_t n);

std::string hex(const uint8_t* data, size_t n);
std::string hex(const std::vector<uint8_t>& v);
bool from_hex(const std::string& s, std::vector<uint8_t>* out);

// A stored password: "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>".
// Self-describing so the iteration count can be raised later without
// invalidating old records.
std::string hash_password(const std::string& password, uint32_t iterations);
bool verify_password(const std::string& password, const std::string& stored);

}  // namespace safetrail::app
