#pragma once
// integrity.h — local self-hash tripwire against hex-editor-level binary
// patching (flipping a branch, nopping out a check, swapping a constant).
//
// HONEST LIMIT, read before wiring this in: the check and its expected hash
// both ship inside the file they protect. A reverser who finds this function
// can patch out the call to selfCheck(), or patch the target bytes AND
// recompute+repatch the stored hash with the same algorithm — it's open
// right below. This stops accidental corruption and casual "patch one byte
// and run" tampering. It does NOT stop someone who specifically sets out to
// defeat it. A check a local patch can't also fix needs a remote anchor —
// e.g. the license server this project already calls in licStart/licPoll is
// a far stronger foundation than anything computed purely on-device. Treat
// this as a tripwire that raises casual-tamper cost, not as real protection
// against a targeted attacker.
//
// Mechanism: tools/patch_integrity.py finds the kExpectedHash placeholder
// below in the final STRIPPED binary (by exact byte match — must stay
// unique in the file), computes a keyed hash of the whole file with that
// 32-byte region zeroed, and writes the hash back in place. At startup this
// code re-derives the same hash from its own on-disk file and compares.
// Must run exactly once per final binary, after strip.

#include "chacha20.hpp"
#include "enc_strings.hpp"  // ENC_KEY

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace integrity {

// Placeholder, overwritten in place by tools/patch_integrity.py after link.
// Keep this exact pattern — the patcher locates it by literal byte match,
// not by symbol or offset, and refuses to patch if it isn't unique in the
// file (see that script). Do not hand-edit these bytes.
//
// MUST stay volatile. At -O2, GCC/Clang can prove a plain `const` array is
// never written within the translation unit and pass memcmp() a pointer to
// its own compile-time literal copy of the INITIALIZER instead of this
// symbol's actual (post-link-patched) address — verified via disassembly:
// without volatile, `memcmp(buf, kExpectedHash, 32)` compiles to
// `leaq .LC0(%rip), %rsi` (a synthesized constant), not a reference to the
// kExpectedHash symbol. That makes selfCheck() fail on every legitimately
// patched binary, unconditionally, at exactly the optimization level this
// project builds with. volatile forces a real load from the symbol's
// address on every read and defeats the substitution.
alignas(1) inline volatile const uint8_t kExpectedHash[32] = {
    0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A,
    0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A,
};

// Keyed hash: ChaCha20 keystream over 32-byte chunks of `data` (with the
// placeholder region zeroed by the caller), XOR-folded into one 32-byte
// accumulator. This is a tripwire, not a MAC — folding is linear, so anyone
// who recovers ENC_KEY (trivial: it's a plaintext array in this same
// binary, see enc_strings.hpp) can forge a matching hash for an arbitrary
// patch. Said plainly above; a stronger hash here would not close that gap,
// since the gap is "the key ships with the binary," not the hash algorithm.
inline void hashBuf(const uint8_t* data, size_t len, uint8_t out[32]) {
    static const uint8_t nonce[12] = {'I', 'N', 'T', 'E', 'G', 'R', 'I', 'T', 'Y', 0, 0, 0};
    uint8_t acc[32] = {0};
    uint8_t block[32];
    size_t pos = 0;
    uint32_t counter = 0;
    while (pos < len) {
        size_t n = len - pos < sizeof(block) ? len - pos : sizeof(block);
        memcpy(block, data + pos, n);
        if (n < sizeof(block)) memset(block + n, 0, sizeof(block) - n);
        cc20::chacha20_xor(block, sizeof(block), ENC_KEY, nonce, counter++);
        for (int i = 0; i < 32; ++i) acc[i] ^= block[i];
        pos += n;
    }
    memcpy(out, acc, 32);
}

// Reads `path` whole, zeroes whichever 32-byte span matches our own loaded
// kExpectedHash constant (that's the patched placeholder, wherever the
// linker put it — searching by value instead of a fixed offset means this
// doesn't depend on predicting layout), hashes the rest, returns via out.
// Bounded to 64 MiB: anything bigger is not this project's binary and
// almost certainly a wrong path, not a real file to hash.
// Copies the volatile placeholder into a plain buffer via explicit
// element-wise volatile reads. Required, not cosmetic: memcmp() cannot take
// a volatile-qualified pointer, and casting the qualifier away on
// kExpectedHash directly would hand the optimizer back exactly the opening
// described above. Snapshot once into a plain array, then compare against
// the snapshot everywhere else.
inline void snapshotExpected(uint8_t out[32]) {
    for (int i = 0; i < 32; ++i) out[i] = kExpectedHash[i];
}

inline bool hashFile(const char* path, uint8_t out[32]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0 || sz > (64 * 1024 * 1024)) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = new uint8_t[(size_t)sz];
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { delete[] buf; return false; }

    uint8_t expected[32];
    snapshotExpected(expected);
    for (long i = 0; i + 32 <= sz; ++i) {
        if (memcmp(buf + i, expected, 32) == 0) {
            memset(buf + i, 0, 32);
            break;
        }
    }
    hashBuf(buf, (size_t)sz, out);
    delete[] buf;
    return true;
}

// ownPath: HOST passes the target of readlink("/proc/self/exe"); PAYLOAD
// passes its own module path from dladdr() on a function inside itself (the
// dropped-to-disk path, not the in-game mapped name, since the on-disk
// bytes are what was hashed at build time). Returns true if the path
// couldn't be read at all — an unreadable path is a packaging problem, not
// evidence of tampering, and should not take down the feature on its own.
inline bool selfCheck(const char* ownPath) {
    uint8_t actual[32];
    if (!hashFile(ownPath, actual)) return true;
    uint8_t expected[32];
    snapshotExpected(expected);
    return memcmp(actual, expected, 32) == 0;
}

} // namespace integrity
