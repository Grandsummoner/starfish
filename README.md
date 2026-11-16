# STARFISH for M-VAVE FM-1

**8-voice Virtual Analog Synthesizer · 10-Kit Drum Machine · 16-Step Sequencer · Arpeggiator · 1,056 Presets + 32 Flash User Slots**

Built on [`fm1-base`](https://github.com/charlesvestal/fm1-base) for M-VAVE FM-1 hardware.

---

## What's Inside

- **`app/`** — STARFISH C99 firmware & single-precision DSP (`app.h`, `app.c`, `dsp.h`, `dsp.c`, `presets.h`, `presets_rom.h`, `fm1.toml`, `words.tsv`, `docs/MANUAL.md`, `site/landing.md`).
- **`web-installer/`** — Standalone, Loom-8-style Web MIDI flasher (`index.html` with `fm1ports.js`, `fm1pkg.js`, and `fm1ota.js` inlined). Open in Chrome or Edge to flash any `.fwsc` binary directly to FM-1 hardware over USB-MIDI, restore M-VAVE stock firmware, or read crash reports.
- **`.github/workflows/release.yml`** — GitHub Actions release pipeline using `zvenson/fm1-build/.github/workflows/build.yml@v1` to compile signed, attested `.fwsc`, `-app.bin`, `.rfm1`, and `firmware.json` artifacts.
- **`tests/`** — Automated host simulator verification (`tests/basic.txt`, `tests/run_tests.sh`).

---

## Building the `.fwsc` Binary

### Option 1 — Automated GitHub Release (Recommended)

1. Push this repository to GitHub.
2. Push a version tag (e.g. `git tag v1.0 && git push origin v1.0`) or trigger **Actions -> Build and attest STARFISH firmware -> Run workflow**.
3. GitHub Actions compiles **`starfish-1.0.fwsc`** (or `starfish.fwsc`), runs the host verification suite, signs build provenance, and attaches the `.fwsc` to the GitHub Release.

### Option 2 — Local Linux / macOS Build

Clone `fm1-base` into `base/` (or `git submodule update --init --recursive`) with the JieLi toolchain + AC79 SDK configured:

```bash
./build.sh --release 1.0
# Output: app/build/starfish-1.0.fwsc
```

Host simulator check (uses local C compiler, no embedded SDK required):

```bash
./build.sh --host
./app/build/host/starfish_host tests/basic.txt app/build/host
```

---

## Flashing to FM-1 Hardware

1. Host `web-installer/index.html` on GitHub Pages or any HTTPS/localhost static server (Web MIDI requires a secure context).
   - If you place `starfish.fwsc` in the same folder as `web-installer/index.html`, the installer loads it automatically.
   - Or select any `starfish-*.fwsc` downloaded from GitHub Releases using the built-in file picker.
2. Connect the FM-1 via USB-C data cable in Chrome or Edge.
3. Click **Install**, confirm the dialog, and keep the cable connected until the status reads **Done**.

---

## License

Licensed under **MPL-2.0** (see `LICENSE`). Built on `fm1-base` and `felucca-bsp`.
