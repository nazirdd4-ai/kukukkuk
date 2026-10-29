#!/usr/bin/env python3
"""strip_shdrs.py — post-process an already-stripped ELF: drop the section header
table entirely (e_shoff/e_shnum/e_shstrndx -> 0, section-header bytes truncated
off the end of the file).

Why beyond `llvm-strip --strip-all`: strip removes .symtab/.strtab/debug info but
leaves the *allocated* section headers in place (.text, .rodata, .dynsym, .dynstr,
.dynamic, .rela.dyn, .hash, ...) — readelf -S / objdump -T / a disassembler's
"jump to .text" still work fine on a strip --strip-all binary, usually 15-25
section headers survive. The Android loader (linker64) never reads the section
header table at all — everything it needs (PT_DYNAMIC, PT_LOAD, DT_SYMTAB,
DT_STRTAB, DT_HASH, ...) comes from the program headers and the DYNAMIC segment,
both of which this script does NOT touch. So the binary loads and runs
identically; what changes is that static tools expecting a section table (classic
readelf -S / nm / objdump -T workflows, and the "point IDA/Ghidra at a section to
start" reflex) get nothing and have to fall back to manually walking program
headers + PT_DYNAMIC the way tools/dynparse-style scripts do. Observed in the
wild on at least one competing Android cheat binary (umbrella): only
.dynstr/.dynamic/.shstrtab survived there — i.e. even less than strip alone
leaves; this script goes one step further and drops the table completely, which
is strictly harder to point a section-based tool at and doesn't depend on
guessing which 3 section headers they specifically chose to keep.

LIMIT (same honesty standard as antidebug.h / integrity.h): this does not hide
or encrypt a single byte of code or data — every instruction, string, and
.rodata byte is exactly where it was, at the exact same file offsets, readable
by anyone willing to parse program headers instead of section headers (which is
free in Ghidra/IDA — both happily load a PIE ELF with no section table at all,
they just show less automatic naming). This raises the bar for grep-and-go
tooling and script kiddies; it does nothing against a reverser who opens this in
a real disassembler.

Usage: python3 tools/strip_shdrs.py <binary-in-place>
Run AFTER llvm-strip, AFTER tools/patch_integrity.py (that script searches by
byte value, not by section, so order with this one doesn't matter for it
specifically — but do this one last regardless, since it's the one most likely
to confuse another tool if something downstream still expects section headers).
"""
import struct
import sys


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <binary>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    with open(path, "r+b") as f:
        data = bytearray(f.read())

        if data[:4] != b"\x7fELF":
            print(f"ERROR: {path} is not an ELF file", file=sys.stderr)
            sys.exit(1)
        ei_class = data[4]
        if ei_class != 2:
            print(f"ERROR: {path} is not ELF64 (strip_shdrs.py only handles ELF64)", file=sys.stderr)
            sys.exit(1)

        # ELF64 header: e_shoff @ 0x28 (u64), e_shentsize @ 0x3a (u16),
        # e_shnum @ 0x3c (u16), e_shstrndx @ 0x3e (u16)
        e_shoff, = struct.unpack_from("<Q", data, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3a)

        if e_shoff == 0 and e_shnum == 0:
            print(f"[*] {path}: already has no section header table, nothing to do")
            return

        shtab_size = e_shentsize * e_shnum
        print(f"[*] {path}: e_shoff=0x{e_shoff:x} e_shnum={e_shnum} e_shentsize={e_shentsize} "
              f"(table = {shtab_size} bytes)")

        # zero the header fields first
        struct.pack_into("<Q", data, 0x28, 0)     # e_shoff
        struct.pack_into("<H", data, 0x3a, 0)     # e_shentsize
        struct.pack_into("<H", data, 0x3c, 0)     # e_shnum
        struct.pack_into("<H", data, 0x3e, 0)     # e_shstrndx

        # if the section header table is a contiguous block at the end of the file (the normal
        # case — compilers/linkers emit it last), drop those bytes too instead of just
        # orphaning them; a scanner that ignores the header fields and looks for ELF-shaped
        # trailing data would otherwise still find it.
        if e_shoff > 0 and e_shoff + shtab_size == len(data):
            del data[e_shoff:]
            print(f"[*] truncated {shtab_size} bytes of section header table off the end of the file")
        elif e_shoff > 0:
            print("[*] section header table is not a trailing block (unusual layout) — "
                  "header fields zeroed, but raw bytes left in place; inspect manually if that matters")

        f.seek(0)
        f.truncate()
        f.write(data)

    print(f"[+] done: {path} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
