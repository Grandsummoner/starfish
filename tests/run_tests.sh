#!/usr/bin/env sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "[starfish] Checking firmware package..."
test -f app/build/starfish.fwsc
grep -q "FELUCCA-LOADER-1" app/build/starfish.fwsc
test -f firmware.json || test -f app/build/firmware.json

echo "[starfish] Building host simulator..."
python3 base/tools/build.py app --host

echo "[starfish] Running host verification suite..."
./app/build/host/starfish_host tests/basic.txt app/build/host

echo "[starfish] All firmware tests passed."
