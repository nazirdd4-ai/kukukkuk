#!/usr/bin/env bash
# protect.sh — the "protect" step build.sh/make_launcher.sh's comments already
# refer to ("CI 'protect' step after strip/upx") but that isn't part of this
# source drop — same situation as enc_strings.hpp/chacha20.hpp, generated/run
# outside this repo. This fills that gap with a local, opt-in equivalent:
# UPX-compresses an already-stripped/shdr-dropped binary, AFTER which the
# integrity hash must be re-patched (UPX rewrites the file — anything hashed
# before this step no longer matches what's on disk, same reasoning as the
# "integrity self-hash: must run on this exact file, after strip, before
# embedding" comment in build.sh).
#
# WHY THIS IS SEPARATE FROM build.sh, NOT WIRED IN BY DEFAULT: UPX on Android
# arm64 PIE executables is a known rough edge, not a sure thing —
#   - some Android versions/SELinux policies block the RWX page UPX's
#     decompression stub needs at process start (execmem denial); whether
#     yours does is a per-device/per-ROM question, not answerable from here
#   - UPX's ARM64 ELF+PIE support varies by UPX version; older ones mishandle
#     the PT_INTERP/PT_DYNAMIC layout Android's linker64 expects
#   - this was never run in this session (no NDK, no device, no real UPX
#     binary available to test against) — UNVERIFIED, same honesty bar as
#     antidebug.h's armSelfTrace() note
# Treat a packed build as experimental until you've pushed it to an actual
# device and confirmed `@fyxod_cpp` still launches and the menu still comes
# up. Keep the unpacked build as the fallback you ship if it doesn't.
#
# Usage: tools/protect.sh <binary> <ENC_KEY-hex-64-chars>
# Run AFTER build.sh has already produced+stripped+shdr-dropped the binary,
# BEFORE make_launcher.sh (so the launcher embeds the packed, hash-patched
# bytes, not the pre-pack ones — the same ordering build.sh already uses for
# strip -> shdr-drop -> integrity-patch on both PAYLOAD and HOST).
set -euo pipefail

BIN="${1:?usage: protect.sh <binary> <ENC_KEY-hex>}"
ENC_KEY_HEX="${2:?usage: protect.sh <binary> <ENC_KEY-hex>}"
SRC="$(cd "$(dirname "$0")/../src" && pwd)"

[ -f "$BIN" ] || { echo "ERROR: protect.sh: no such file: $BIN" >&2; exit 1; }

UPX_BIN="${UPX_BIN:-upx}"
if ! command -v "$UPX_BIN" >/dev/null 2>&1; then
    echo "ERROR: protect.sh: '$UPX_BIN' not found. Install UPX (or set UPX_BIN=/path/to/upx) — not auto-fetched here, build.sh's no-network guard applies to this script too." >&2
    exit 1
fi

echo "[*] protect: packing $BIN with $($UPX_BIN --version | head -n1)..."
cp "$BIN" "$BIN.prepack"   # keep the unpacked build around — see header on why this is still experimental
"$UPX_BIN" --best --lzma --force-pie -q "$BIN"

echo "[*] protect: re-patching integrity hash post-pack (UPX rewrote the file, the old hash no longer matches)..."
python3 "$(dirname "$0")/patch_integrity.py" "$BIN" "$ENC_KEY_HEX"

echo "[+] protect: done. packed: $BIN ($(wc -c < "$BIN") bytes), unpacked fallback kept at $BIN.prepack"
echo "[!] UNVERIFIED on real hardware this session — test on-device before shipping this over the unpacked build."
