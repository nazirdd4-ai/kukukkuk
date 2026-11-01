#!/usr/bin/env bash
# make_launcher.sh — copies the binary as a .sh file (bare ELF, no dropper wrapper)
# Usage: make_launcher.sh <binary-in> <launcher-out>
set -euo pipefail

BIN="${1:?usage: make_launcher.sh <binary-in> <launcher-out>}"
LAUNCHER="${2:?usage: make_launcher.sh <binary-in> <launcher-out>}"
[ -f "$BIN" ] || { echo "ERROR: make_launcher.sh: no such file: $BIN" >&2; exit 1; }

cp "$BIN" "$LAUNCHER"
chmod 755 "$LAUNCHER"

echo "[+] done: $LAUNCHER ($(wc -c < "$LAUNCHER") bytes) — bare ELF, push and run directly"
