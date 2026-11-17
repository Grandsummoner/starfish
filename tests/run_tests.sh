#!/usr/bin/env sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "[starfish] Checking firmware package..."
test -s app/build/starfish.fwsc
python3 -c 'data = open("app/build/starfish.fwsc", "rb").read(); assert b"FELUCCA-LOADER-1" in data, "Missing FELUCCA-LOADER-1"'
test -f firmware.json || test -f app/build/firmware.json

echo "[starfish] Building host simulator..."
python3 base/tools/build.py app --host

echo "[starfish] Running host verification suite..."
./app/build/host/starfish_host tests/basic.txt app/build/host

echo "[starfish] All firmware tests passed."
