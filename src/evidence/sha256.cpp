#include "safetrail/evidence/merkle_log.hpp"
#include <cstring>

// SHA-256, FIPS 180-4, from scratch. No OpenSSL: importing a crypto library to
// hash 32 bytes would undercut the whole "we build it ourselves" premise, and the
// algorithm is ~120 lines. This is the primitive the Merkle log is built on.
namespace safetrail::evidence {

namespace {
inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

void process(uint32_t st[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|
           (uint32_t(block[i*4+2])<<8)|uint32_t(block[i*4+3]);
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
    uint32_t s1 = rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
    w[i] = w[i-16]+s0+w[i-7]+s1;
  }
  uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
    uint32_t ch = (e&f)^((~e)&g);
    uint32_t t1 = h+S1+ch+K[i]+w[i];
    uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
    uint32_t maj = (a&b)^(a&c)^(b&c);
    uint32_t t2 = S0+maj;
    h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
  }
  st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}
}  // namespace

Hash sha256(const uint8_t* data, size_t len) {
  uint32_t st[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  size_t full = len / 64;
  for (size_t i = 0; i < full; ++i) process(st, data + i*64);

  // final block(s) with padding
  uint8_t tail[128];
  size_t rem = len - full*64;
  // Guarded, and not merely to keep a sanitizer quiet. SHA-256 of the EMPTY
  // input is a real value the code depends on -- RFC 6962 defines the Merkle
  // root of an empty log as exactly this -- and MerkleLog::root() reaches it via
  // sha256(nullptr, 0). Both `data + full*64` and `memcpy(dst, nullptr, 0)` are
  // undefined behaviour on a null pointer even though the length is zero:
  // memcpy's second parameter is declared nonnull, and pointer arithmetic on a
  // null pointer is UB regardless of the offset. It happens to work everywhere,
  // which is precisely why it survived until a gating UBSan build on g++ said so.
  if (rem) std::memcpy(tail, data + full*64, rem);
  tail[rem] = 0x80;
  size_t padlen = (rem < 56) ? 64 : 128;
  std::memset(tail + rem + 1, 0, padlen - rem - 1 - 8);
  uint64_t bits = uint64_t(len) * 8;
  for (int i = 0; i < 8; ++i) tail[padlen-1-i] = uint8_t(bits >> (8*i));
  process(st, tail);
  if (padlen == 128) process(st, tail + 64);

  Hash out{};
  for (int i = 0; i < 8; ++i) {
    out[i*4]   = uint8_t(st[i]>>24); out[i*4+1] = uint8_t(st[i]>>16);
    out[i*4+2] = uint8_t(st[i]>>8);  out[i*4+3] = uint8_t(st[i]);
  }
  return out;
}

// RFC 6962 node hash: SHA256(0x01 || left || right). The 0x01 domain-separation
// prefix (vs 0x00 for leaves) is what stops a second-preimage attack that swaps
// an internal node for a leaf.
Hash sha256_pair(const Hash& l, const Hash& r) {
  uint8_t buf[65];
  buf[0] = 0x01;
  std::memcpy(buf + 1, l.data(), 32);
  std::memcpy(buf + 33, r.data(), 32);
  return sha256(buf, 65);
}

std::string to_hex(const Hash& h) {
  static const char* d = "0123456789abcdef";
  std::string s; s.reserve(64);
  for (uint8_t b : h) { s += d[b>>4]; s += d[b&15]; }
  return s;
}

}  // namespace safetrail::evidence
