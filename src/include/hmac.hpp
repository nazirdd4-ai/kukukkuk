// hmac.hpp — HMAC-SHA256 (RFC 2104), self-contained, depends only on sha256.hpp.
// Used by licenseCheck() in cheat.cpp to verify the worker signed the license
// response, not just that it contains the substring "valid":true. Closes the
// gap documented at the SSL_CTX_set_verify(VERIFY_NONE) call: without the
// signature check, anyone who can either MITM the TLS connection or hook
// SSL_read() in-process (Frida: no patching of this binary needed at all)
// could return a fake 200 OK with "valid":true and pass. With it, forging a
// pass additionally requires STR_SIG_KEY, which never crosses the wire —
// same "ships in the binary, documented limit" posture as ENC_KEY elsewhere
// in this project (see integrity.h's header), but it moves the attack from
// "read/replay network traffic" to "extract a secret from the APK's native
// lib," which is a materially higher bar and matches what a signed-response
// scheme buys you in practice.
#pragma once
#include "sha256.hpp"
#include <cstdint>
#include <cstring>

namespace hmac {

inline void sha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* msg, size_t msgLen,
                    uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (keyLen > 64) {
        ::sha256::hash(key, keyLen, k);   // keys longer than the block size are hashed down first, per RFC 2104
    } else {
        memcpy(k, key, keyLen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    ::sha256::Ctx c;
    ::sha256::init(c);
    ::sha256::update(c, ipad, 64);
    ::sha256::update(c, msg, msgLen);
    uint8_t inner[32];
    ::sha256::final(c, inner);

    ::sha256::init(c);
    ::sha256::update(c, opad, 64);
    ::sha256::update(c, inner, 32);
    ::sha256::final(c, out);
}

// Constant-time compare — a timing-variable memcmp here would let an attacker
// learn the correct signature byte-by-byte against a live server/hooked
// SSL_read, same reasoning as any MAC-verification compare.
inline bool constTimeEq(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

} // namespace hmac
