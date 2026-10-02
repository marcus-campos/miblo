#pragma once
#include <stddef.h>
#include <stdint.h>

// SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104), small and portable: the core's BearSSL is not
// otherwise linked, and this one is tested on the host. Used by the address challenge
// (miblo_security.h answerChallenge). The round constants live in flash (MIBLO_ROM).
namespace miblo {

class Sha256 {
 public:
  Sha256() { reset(); }
  void reset();
  void update(const void* data, size_t n);
  void finish(uint8_t out[32]);  // then reset() before reuse

 private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  uint64_t total_ = 0;
  size_t fill_ = 0;
};

// HMAC-SHA256(key, a || b): the message in two parts, as the challenge builds it.
void hmacSha256(const uint8_t* key, size_t keyLen, const void* a, size_t aLen, const void* b, size_t bLen,
                uint8_t out[32]);

}  // namespace miblo
