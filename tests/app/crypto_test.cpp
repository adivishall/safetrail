// HMAC-SHA256 and PBKDF2-HMAC-SHA256 against their published test vectors, and
// the stored-password format end to end.
#include "safetrail/app/crypto.hpp"
#include "test_harness.hpp"

using namespace safetrail;

int main() {
  // RFC 4231 test case 1.
  {
    std::vector<uint8_t> key(20, 0x0b);
    const std::string msg = "Hi There";
    const auto h = app::hmac_sha256(key.data(), key.size(),
                                    reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    t::ok(app::hex(h.data(), h.size()) ==
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
          "HMAC-SHA256, RFC 4231 case 1");
  }
  // RFC 4231 test case 2 (key shorter than the block).
  {
    const std::string key = "Jefe", msg = "what do ya want for nothing?";
    const auto h = app::hmac_sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                    reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    t::ok(app::hex(h.data(), h.size()) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
          "HMAC-SHA256, RFC 4231 case 2");
  }
  // RFC 4231 test case 6 (key longer than the block is hashed first).
  {
    std::vector<uint8_t> key(131, 0xaa);
    const std::string msg = "Test Using Larger Than Block-Size Key - Hash Key First";
    const auto h = app::hmac_sha256(key.data(), key.size(),
                                    reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    t::ok(app::hex(h.data(), h.size()) ==
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
          "HMAC-SHA256, RFC 4231 case 6 (long key)");
  }
  // RFC 7914 section 11, PBKDF2-HMAC-SHA256.
  {
    const std::string salt = "salt";
    const auto dk = app::pbkdf2_sha256("passwd", std::vector<uint8_t>(salt.begin(), salt.end()), 1, 64);
    t::ok(app::hex(dk) ==
              "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
              "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
          "PBKDF2-HMAC-SHA256, RFC 7914 c=1");
  }
  {
    const std::string salt = "NaCl";
    const auto dk = app::pbkdf2_sha256("Password", std::vector<uint8_t>(salt.begin(), salt.end()), 80000, 64);
    t::ok(app::hex(dk) ==
              "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
              "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
          "PBKDF2-HMAC-SHA256, RFC 7914 c=80000");
  }

  // Stored format.
  {
    const std::string stored = app::hash_password("correct horse battery", 1000);
    t::ok(stored.rfind("pbkdf2-sha256$1000$", 0) == 0, "stored hash names its scheme and cost");
    t::ok(app::verify_password("correct horse battery", stored), "right password verifies");
    t::ok(!app::verify_password("correct horse batterz", stored), "wrong password fails");
    t::ok(!app::verify_password("", stored), "empty password fails");
    const std::string again = app::hash_password("correct horse battery", 1000);
    t::ok(again != stored, "salt is random: the same password hashes differently");
    t::ok(!app::verify_password("x", "plaintext"), "malformed record fails");
    t::ok(!app::verify_password("x", "pbkdf2-sha256$abc$00$00"), "non-numeric cost fails");
    t::ok(!app::verify_password("x", "pbkdf2-sha256$1$zz$00"), "bad salt hex fails");
    t::ok(stored.find("correct") == std::string::npos, "the password does not appear in the record");
  }

  // Helpers.
  {
    t::ok(app::constant_time_equal(std::string("abc"), std::string("abc")), "equal strings");
    t::ok(!app::constant_time_equal(std::string("abc"), std::string("abd")), "unequal strings");
    t::ok(!app::constant_time_equal(std::string("abc"), std::string("ab")), "unequal lengths");
    std::vector<uint8_t> a(32), b(32);
    t::ok(app::random_bytes(a.data(), a.size()) && app::random_bytes(b.data(), b.size()),
          "OS randomness available");
    t::ok(a != b, "two random draws differ");
    std::vector<uint8_t> back;
    t::ok(app::from_hex(app::hex(a), &back) && back == a, "hex round trip");
    t::ok(!app::from_hex("abc", &back), "odd-length hex refused");
  }
  return t::report("app/crypto");
}
