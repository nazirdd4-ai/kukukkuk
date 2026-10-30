#pragma once
// ed25519_verify.h — contract for a vendored, verify-only Ed25519 primitive.
//
// WHY THIS REPLACES hmac.hpp FOR LICENSE-SIG VERIFICATION:
// hmac.hpp/verifyLicenseSig() used HMAC-SHA256 under STR_SIG_KEY, a SYMMETRIC
// secret — the same 32 bytes the Worker uses to sign. That key must still be
// present client-side to verify, and ChaCha20/strobf obfuscation around it is
// (as this project's own integrity.h and hmac.hpp headers already say) cost
// to extract, not a wall — it ships in the binary either way. Once extracted
// ONE time from ONE build, STR_SIG_KEY lets an attacker forge {"valid":true,
// "sig":...} offline forever, for every key, with no further contact with
// the server: total, permanent compromise of the license check from a single
// successful extraction.
//
// Ed25519 removes that failure mode structurally, not by raising extraction
// cost: the client only ever needs the PUBLIC key. A public key is not a
// secret — leaking ED25519_PUB (in fact, shipping it in the clear) gives an
// attacker nothing: Ed25519 signature forgery without the private key is a
// discrete-log problem on edwards25519, not a reverse-engineering problem.
// The private key lives ONLY as a Cloudflare Worker secret and is never
// built into any client binary, so no client-side extraction (static or
// dynamic, this build or any build) can ever produce a working forge.
//
// VENDORING — this header is a contract, not an implementation. Drop in
// orlp/ed25519 (zlib license, SUPERCOP ref10-derived, no dependencies):
//   https://github.com/orlp/ed25519  — pin a commit, record it in
//   THIRD_PARTY.sha256 the same way imgui/AndroidImgui already are.
// From its src/ you need exactly: ed25519.h, ge.h/ge.c, fe.h/fe.c, sc.h/sc.c,
// sha512.h/sha512.c, verify.h/verify.c (ed25519_verify() has no dependency
// on ed25519_sign()/create_keypair() — the signing-side files are not needed
// client-side and should not be compiled into PAYLOAD/HOST at all: this is a
// verify-only client, the signing key must never be reachable from it even
// as dead code).
//
// FILL: vendor the files above into src/include/ed25519/, add their hashes
// to THIRD_PARTY.sha256, and point CMake/build.sh's include path at that
// directory. Until then this header alone does not compile a working
// verifier — ed25519Verify() below is declared, not defined.
//
// EXPECTED SIGNATURE (matches orlp/ed25519's ed25519_verify() exactly, so
// the vendored .c files need zero adaptation glue):
//   int ed25519_verify(const unsigned char *signature,
//                       const unsigned char *message, size_t message_len,
//                       const unsigned char *public_key);
//   returns 1 on a valid signature, 0 otherwise.

#include <cstddef>
#include <cstdint>

extern "C" int ed25519_verify(const unsigned char* signature,
                               const unsigned char* message,
                               unsigned long long message_len,
                               const unsigned char* public_key);
