#pragma once
// sectcrypt.hpp — encrypted-at-rest code section, decrypted in place at
// process start. This is the actual "whole chunk of the binary is ciphertext
// until runtime" technique (see antivm.h-adjacent discussion: Umbrella's
// binary has a flat high-entropy ~14MB block, consistent with something in
// this family — a packed/encrypted section, not just a handful of obfuscated
// string constants like strobf.h). Mark a function PCODE, it ships as
// ciphertext in the final binary; a constructor that runs before anything
// else in this process (priority 101 — lower than any unprioritized
// __attribute__((constructor)), including this project's own ctor() in
// cheat.cpp) decrypts it in place before it's ever called.
//
// WHY A SEPARATE CIPHER FROM chacha20.hpp: that header (used by integrity.h,
// presumably by the CI-generated enc_strings.hpp too) isn't part of this
// source drop — same situation strobf.h already documented. Guessing its
// exact internal convention (nonce size, counter word position, round count)
// and getting it wrong would silently produce garbage keystream with no way
// to catch it without a build+device to test against. This header is a
// self-contained, standard IETF ChaCha20 (RFC 8439: 256-bit key, 96-bit
// nonce, 32-bit counter, 20 rounds) — both sides of the encrypt/decrypt pair
// (this file, and tools/encrypt_section.py) are written from the same spec
// by the same pass, so they're guaranteed to agree with each other, which is
// what actually matters here — it doesn't need to match chacha20.hpp, it
// needs to match its own Python counterpart.
//
// MECHANISM:
//   1. tools/gen_sectkey.py generates sectkey.h (32-byte key + 12-byte
//      nonce, baked in as C arrays) BEFORE compiling. Needs to exist at
//      compile time since the decrypt stub below embeds the key.
//   2. Build normally. Functions marked PCODE land in their own section
//      ("pcode") as plaintext, same as any other code — SECT_PROTECT isn't
//      defined yet, decryptPcode() below doesn't exist, nothing runs it.
//   3. tools/encrypt_section.py runs on the LINKED binary, BEFORE
//      llvm-strip/strip_shdrs.py (it needs section headers to find "pcode"
//      by name — same ordering constraint tools/protect.sh has for a
//      different reason). XORs the section's on-disk bytes with the same
//      keystream sectkey.h's key/nonce produce.
//   4. On a SECT_PROTECT build, decryptPcode() (constructor priority 101)
//      mprotects the section RW, decrypts it back to the plaintext the
//      compiler emitted, invalidates the instruction cache over that range
//      (mandatory on ARM/ARM64 — the CPU can and does execute stale
//      icache-resident instructions after you've only changed what's in
//      memory; __builtin___clear_cache is the portable way to flush it),
//      and mprotects back to RX. PCODE functions are called normally after
//      this point — nothing about calling them differs from any other
//      function once decrypted.
//
// HONEST LIMIT, same tier as everything else in this directory: this stops
// static analysis of the shipped file (the bytes genuinely aren't the real
// code until this constructor runs) and raises the bar for dynamic analysis
// (dump the process AFTER this constructor ran, which needs live access —
// attach timing, a hooked mprotect/clear_cache to catch it mid-decrypt, or a
// memory dump once it's settled — not a `strings`/IDA-on-the-file job
// anymore). It does NOT survive a reverser who dumps the live, decrypted
// process image — at that point they have the same plaintext the compiler
// originally emitted, same as Umbrella's packed block doesn't survive a
// post-unpack memory dump either (see the earlier analysis: "distribute a
// malware" vs "slow down a `strings` pass" are different bars, this clears
// the second one).
//
// UNVERIFIED on real hardware this session (no NDK, no device). Known risk,
// stated plainly: mprotect(..., PROT_READ|PROT_WRITE|PROT_EXEC) on an
// already-mapped file-backed segment is generally tolerated even where
// fresh anonymous RWX (the JIT case) gets blocked by W^X hardening, but
// that's a pattern observed elsewhere, not a guarantee for every
// device/SELinux policy this runs on. Build with SECT_PROTECT=1, test on an
// actual device that the PCODE-marked functions still work, before shipping
// it over a build without this.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

// PCODE: tag a function to live in the "pcode" section. Safe and a no-op
// functionally with no further setup — it only becomes "encrypted at rest"
// once tools/encrypt_section.py has actually run on the linked binary (a
// SECT_PROTECT=1 build). Without that, a PCODE function is identical to an
// untagged one, just grouped into its own named section.
//
// NO leading dot, deliberately: GNU ld only auto-generates the
// __start_SECNAME/__stop_SECNAME symbols decryptPcode() below depends on
// when SECNAME is representable as a C identifier — "__start_.pcode" isn't
// one, so a dotted section name here would silently leave those symbols
// undefined (link error, not a subtle runtime bug, but still wrong to ship).
// "pcode" (no dot) is what encrypt_section.py looks for too — keep both in
// sync if this ever gets renamed.
#define PCODE __attribute__((section("pcode")))

#ifdef SECT_PROTECT
#include "sectkey.h"  // generated by tools/gen_sectkey.py — SECT_KEY[32], SECT_NONCE[12]

namespace sectcrypt {

inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline void chacha20Block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                           uint8_t out[64]) {
    uint32_t s[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,  // "expand 32-byte k", IETF/RFC 8439 constants
    };
    for (int i = 0; i < 8; ++i)
        s[4 + i] = (uint32_t)key[4*i] | ((uint32_t)key[4*i+1] << 8) |
                   ((uint32_t)key[4*i+2] << 16) | ((uint32_t)key[4*i+3] << 24);
    s[12] = counter;
    for (int i = 0; i < 3; ++i)
        s[13 + i] = (uint32_t)nonce[4*i] | ((uint32_t)nonce[4*i+1] << 8) |
                    ((uint32_t)nonce[4*i+2] << 16) | ((uint32_t)nonce[4*i+3] << 24);

    uint32_t x[16];
    for (int i = 0; i < 16; ++i) x[i] = s[i];

    auto qr = [&](int a, int b, int c, int d) {
        x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl32(x[d], 16);
        x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl32(x[b], 12);
        x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl32(x[d], 8);
        x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl32(x[b], 7);
    };
    for (int round = 0; round < 10; ++round) {
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15);
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14);
    }
    for (int i = 0; i < 16; ++i) x[i] += s[i];
    for (int i = 0; i < 16; ++i) {
        out[4*i]   = (uint8_t)(x[i]);
        out[4*i+1] = (uint8_t)(x[i] >> 8);
        out[4*i+2] = (uint8_t)(x[i] >> 16);
        out[4*i+3] = (uint8_t)(x[i] >> 24);
    }
}

// XORs `len` bytes at `data` with the ChaCha20 keystream for (key, nonce,
// counter). Symmetric: the same call decrypts what it encrypted.
inline void chacha20Xor(uint8_t* data, size_t len, const uint8_t key[32],
                         const uint8_t nonce[12], uint32_t counter) {
    uint8_t block[64];
    size_t off = 0;
    while (off < len) {
        chacha20Block(key, nonce, counter++, block);
        size_t n = len - off < 64 ? len - off : 64;
        for (size_t i = 0; i < n; ++i) data[off + i] ^= block[i];
        off += n;
    }
}

} // namespace sectcrypt

extern "C" {
extern char __start_pcode[];
extern char __stop_pcode[];
}

// Constructor priority 101: the lowest number user code may use (0-100 are
// reserved for the implementation/runtime), so this runs before any other
// explicitly-prioritized constructor in this TU, and well before an
// unprioritized one like cheat.cpp's own `__attribute__((constructor))
// ctor()` — unprioritized constructors run last, after every prioritized
// one. PCODE functions are decrypted and ready before anything in this
// process can reach them.
__attribute__((constructor(101))) inline void decryptPcode() {
    uintptr_t start = (uintptr_t)__start_pcode;
    uintptr_t end = (uintptr_t)__stop_pcode;
    if (end <= start) return;  // no PCODE-tagged function in this TU — nothing to do

    long pagesz = sysconf(_SC_PAGESIZE);
    if (pagesz <= 0) pagesz = 4096;  // sysconf shouldn't fail here, but don't divide-by-zero a weird return
    uintptr_t pstart = start & ~(uintptr_t)(pagesz - 1);
    uintptr_t pend = (end + (uintptr_t)pagesz - 1) & ~(uintptr_t)(pagesz - 1);
    size_t plen = (size_t)(pend - pstart);

    // RW+X, not RW-then-separately-X: see this file's header on why a
    // second mprotect() call (RW now, X added back after) is the safer
    // split on hardened W^X configs — kept as ONE call here because this is
    // a file-backed segment the loader already mapped executable, not fresh
    // anonymous memory (the case W^X policies actually target); if this
    // mprotect() fails on some device, PCODE functions stay encrypted and
    // whatever calls them next will crash on garbage instructions — not a
    // silent miscompile, a loud one, this session's best honest guess at
    // the right trade-off without hardware to verify it against.
    if (mprotect((void*)pstart, plen, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return;

    sectcrypt::chacha20Xor((uint8_t*)start, (size_t)(end - start), SECT_KEY, SECT_NONCE, 1);

    __builtin___clear_cache((char*)pstart, (char*)pend);  // mandatory on ARM/ARM64: flush stale icache lines, or the CPU may execute the old (encrypted) bytes

    mprotect((void*)pstart, plen, PROT_READ | PROT_EXEC);  // best-effort tighten back to RX; a failure here is a hardening miss, not a correctness one — the decrypted code is already correct either way
}

#endif // SECT_PROTECT
