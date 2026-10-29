#!/usr/bin/env python3
"""encrypt_section.py — XORs the "pcode" section's on-disk bytes with the
ChaCha20 keystream sectcrypt.hpp's decryptPcode() will XOR them back with at
process start. Part of the SECT_PROTECT build path (see sectcrypt.hpp's
header for the full mechanism); this is the build-time half, encrypt_section
for short.

Must run on the LINKED binary, BEFORE llvm-strip/strip_shdrs.py: it finds
the "pcode" section by walking the real ELF section header table, which
strip_shdrs.py (deliberately) destroys for every later stage of the
pipeline. Same ordering constraint tools/protect.sh has for a different
reason (it needs the post-strip file; this needs the pre-strip one) — the
two never run on the same build stage.

Usage: python3 tools/encrypt_section.py <binary> <SECT_KEY hex, 64 chars> <SECT_NONCE hex, 24 chars>

Writes the XOR'd bytes back in place (same file, same size — this never
changes the file's length, only the content of the "pcode" section's
bytes), so running this on an unbuilt or already-encrypted file is a
one-way trip: re-running it a second time on the same output decrypts it
right back (ChaCha20 keystream XOR is its own inverse, used as both the
build-time encrypt and the decryptPcode() runtime decrypt). Keep a copy of
the pre-encrypt binary if you need to re-run this step.
"""
import struct
import sys

ELF64_EHDR = struct.Struct("<16sHHIQQQIHHHHHH")
#                            e_ident e_type e_machine e_version e_entry e_phoff e_shoff
#                            e_flags e_ehsize e_phentsize e_phnum e_shentsize e_shnum e_shstrndx
ELF64_SHDR = struct.Struct("<IIQQQQIIQQ")
#                            sh_name sh_type sh_flags sh_addr sh_offset sh_size sh_link sh_info sh_addralign sh_entsize


def find_section(data, name):
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if data[4] != 2:
        raise ValueError("not a 64-bit ELF (only arm64 builds are handled here)")

    (e_ident, e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
     e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = ELF64_EHDR.unpack_from(data, 0)

    if e_shoff == 0 or e_shnum == 0:
        raise ValueError(
            "no section headers in this file — did strip_shdrs.py already run? "
            "encrypt_section.py must run BEFORE it, see this file's header"
        )

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
         sh_link, sh_info, sh_addralign, sh_entsize) = ELF64_SHDR.unpack_from(data, off)
        sections.append(dict(name_off=sh_name, type=sh_type, addr=sh_addr,
                              offset=sh_offset, size=sh_size))

    if e_shstrndx >= len(sections):
        raise ValueError("e_shstrndx out of range — malformed section header table")
    strtab = sections[e_shstrndx]
    strtab_bytes = data[strtab["offset"]: strtab["offset"] + strtab["size"]]

    def sect_name(sec):
        start = sec["name_off"]
        end = strtab_bytes.find(b"\x00", start)
        return strtab_bytes[start:end].decode("ascii", "replace")

    for sec in sections:
        if sect_name(sec) == name:
            return sec

    raise ValueError(
        f'no section named "{name}" — is at least one PCODE-tagged function '
        f"actually linked in? an unused PCODE function the linker's "
        f"--gc-sections dropped would also produce this"
    )


def chacha20_xor(data: bytearray, key: bytes, nonce: bytes, counter: int):
    def rotl32(v, n):
        v &= 0xFFFFFFFF
        return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF

    def block(ctr):
        s = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574]
        s += list(struct.unpack("<8I", key))
        s += [ctr]
        s += list(struct.unpack("<3I", nonce))
        x = list(s)

        def qr(a, b, c, d):
            x[a] = (x[a] + x[b]) & 0xFFFFFFFF; x[d] ^= x[a]; x[d] = rotl32(x[d], 16)
            x[c] = (x[c] + x[d]) & 0xFFFFFFFF; x[b] ^= x[c]; x[b] = rotl32(x[b], 12)
            x[a] = (x[a] + x[b]) & 0xFFFFFFFF; x[d] ^= x[a]; x[d] = rotl32(x[d], 8)
            x[c] = (x[c] + x[d]) & 0xFFFFFFFF; x[b] ^= x[c]; x[b] = rotl32(x[b], 7)

        for _ in range(10):
            qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
            qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
        return struct.pack("<16I", *[(x[i] + s[i]) & 0xFFFFFFFF for i in range(16)])

    off = 0
    n = len(data)
    while off < n:
        ks = block(counter)
        counter += 1
        chunk = min(64, n - off)
        for i in range(chunk):
            data[off + i] ^= ks[i]
        off += chunk


def main():
    if len(sys.argv) != 4:
        print("usage: encrypt_section.py <binary> <SECT_KEY hex> <SECT_NONCE hex>", file=sys.stderr)
        sys.exit(1)
    path, key_hex, nonce_hex = sys.argv[1], sys.argv[2], sys.argv[3]

    key = bytes.fromhex(key_hex)
    nonce = bytes.fromhex(nonce_hex)
    if len(key) != 32:
        print(f"ERROR: SECT_KEY must be 32 bytes (64 hex chars), got {len(key)}", file=sys.stderr)
        sys.exit(1)
    if len(nonce) != 12:
        print(f"ERROR: SECT_NONCE must be 12 bytes (24 hex chars), got {len(nonce)}", file=sys.stderr)
        sys.exit(1)

    with open(path, "rb") as f:
        data = f.read()

    sec = find_section(data, "pcode")
    if sec["size"] == 0:
        print('ERROR: "pcode" section is empty — nothing to encrypt', file=sys.stderr)
        sys.exit(1)

    buf = bytearray(data)
    region = bytearray(buf[sec["offset"]: sec["offset"] + sec["size"]])
    # counter starts at 1 — must match decryptPcode()'s chacha20Xor(..., 1) call in sectcrypt.hpp
    chacha20_xor(region, key, nonce, 1)
    buf[sec["offset"]: sec["offset"] + sec["size"]] = region

    with open(path, "wb") as f:
        f.write(buf)

    print(f'[+] encrypted "pcode" section: {sec["size"]} bytes at file offset 0x{sec["offset"]:x}', file=sys.stderr)


if __name__ == "__main__":
    main()
