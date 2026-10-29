#!/usr/bin/env python3
"""Post-link integrity patcher — run once, after strip, on the final binary.

Usage: python3 tools/patch_integrity.py <binary> <ENC_KEY hex, 64 chars>

Finds the kExpectedHash placeholder from integrity.h (32 bytes of alternating
0xA5/0x5A) in the given file, computes the same keyed tripwire hash
integrity.h::selfCheck() recomputes at runtime (placeholder region zeroed
first), and writes the hash back at the same offset.

Must use the SAME ENC_KEY that was baked into enc_strings.hpp for this build
(the CI workflow's "encrypt strings" step secret) — integrity.h reads
ENC_KEY from that header, so a mismatched key here makes every self-check
fail on a perfectly legitimate binary.

Run this exactly once per final binary, after llvm-strip. Re-stripping or
otherwise touching the file after patching invalidates the hash.
"""
import sys
import struct


def rotl32(v, n):
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF


def chacha20_block(state):
    x = list(state)

    def qr(a, b, c, d):
        x[a] = (x[a] + x[b]) & 0xFFFFFFFF; x[d] ^= x[a]; x[d] = rotl32(x[d], 16)
        x[c] = (x[c] + x[d]) & 0xFFFFFFFF; x[b] ^= x[c]; x[b] = rotl32(x[b], 12)
        x[a] = (x[a] + x[b]) & 0xFFFFFFFF; x[d] ^= x[a]; x[d] = rotl32(x[d], 8)
        x[c] = (x[c] + x[d]) & 0xFFFFFFFF; x[b] ^= x[c]; x[b] = rotl32(x[b], 7)

    for _ in range(10):
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
    out = b""
    for i in range(16):
        out += struct.pack("<I", (x[i] + state[i]) & 0xFFFFFFFF)
    return out


def chacha20_xor32(block32, key, nonce, counter):
    state = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574,
             *struct.unpack("<8I", key), counter, *struct.unpack("<3I", nonce)]
    ks = chacha20_block(state)[:32]
    return bytes(a ^ b for a, b in zip(block32, ks))


def hash_buf(data: bytes, key: bytes) -> bytes:
    """Must match integrity.h::hashBuf exactly: 32-byte chunks, zero-padded
    tail, ChaCha20 keystream per chunk (counter increments per chunk),
    XOR-folded into one 32-byte accumulator."""
    nonce = b"INTEGRITY\x00\x00\x00"
    assert len(nonce) == 12
    acc = bytearray(32)
    pos = 0
    counter = 0
    while pos < len(data):
        chunk = data[pos:pos + 32]
        if len(chunk) < 32:
            chunk = chunk + b"\x00" * (32 - len(chunk))
        ks = chacha20_xor32(chunk, key, nonce, counter)
        for i in range(32):
            acc[i] ^= ks[i]
        pos += 32
        counter += 1
    return bytes(acc)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    path, key_hex = sys.argv[1], sys.argv[2]
    if len(key_hex) != 64:
        print("ERROR: key must be 64 hex chars (32 bytes) — pass the build's ENC_KEY", file=sys.stderr)
        sys.exit(1)
    key = bytes.fromhex(key_hex)

    data = bytearray(open(path, "rb").read())
    placeholder = bytes([0xA5, 0x5A] * 16)
    off = data.find(placeholder)
    if off < 0:
        print("ERROR: integrity placeholder not found — was integrity.h compiled in?", file=sys.stderr)
        sys.exit(1)
    if data.find(placeholder, off + 1) != -1:
        print("ERROR: placeholder pattern is not unique in this binary — ambiguous patch target, aborting", file=sys.stderr)
        sys.exit(1)

    data[off:off + 32] = b"\x00" * 32
    h = hash_buf(bytes(data), key)
    data[off:off + 32] = h
    open(path, "wb").write(data)
    print("[+] integrity patched: file=%s offset=0x%x hash=%s" % (path, off, h.hex()))


if __name__ == "__main__":
    main()
