# STARFISH for M-VAVE FM-1

**8-Voice Virtual Analog & Hypersaw Synthesizer · Dual Multimode Filters · 4-Icon FX Rack · 10-Kit Drum Sequencer · 288 Curated Presets + 128 Bank X Flash Slots**

STARFISH turns the **M-VAVE FM-1** into a complete portable groovebox.

---

## Web Installer, User Guide & Promo Showcase (GitHub Pages)

> **Primary README link:** [`https://grandsummoner.github.io/starfish/`](https://grandsummoner.github.io/starfish/) (`index.html` — Web MIDI Firmware Installer)

- **[`index.html`](https://grandsummoner.github.io/starfish/)** (and `web-installer/index.html`) — **Web MIDI Firmware Installer** *(Main Landing Page)*. Open in Chrome or Edge to install `starfish.fwsc` directly over USB-MIDI, restore M-VAVE stock firmware, or read device crash reports.
- **[`guide.html`](https://grandsummoner.github.io/starfish/guide.html)** — **Interactive HTML User Guide**. Explore all hardware buttons and live 240×240 OLED visualizations (including the 4-box FX icons), test the 27-key drum sequencer layout, search all **288 ROM presets**, and review USB/Bluetooth MIDI routing.
- **[`edm.html`](https://grandsummoner.github.io/starfish/edm.html)** — **Multi-Screenshot Promo & EDM Showcase**. Generates multi-screenshot PNG posters, widescreen banners, and `starfish-edm-mailer.html` for email/social announcements.

All three pages share top-bar navigation (`Web Installer` · `User Guide` · `Promo & EDM`). With **Settings → Pages → Source: GitHub Actions** enabled, `.github/workflows/release.yml` automatically compiles `starfish.fwsc` and publishes `index.html`, `guide.html`, `edm.html`, and `starfish-edm-mailer.html` together.

---

## Synth, Effects & Sequencer Architecture

### Sound Engine (`app/dsp.c`, `app/presets.h`, `app/presets_rom.h`)
- **Dual Oscillators (8 Voices):** `HYPERSAW`, `SAW`, `PULSE`, `TRIANGLE`, `HARD SYNC`, and `WAVETABLE`, plus Sub-Oscillator (`SQR/SAW/SIN/TRI`), Noise, Ring Modulation, FM cross-mod, and up to 5-voice Unison spread.
- **Dual Resonant Filters:** `LP`, `BP`, `HP`, `NOTCH` arranged in **Serial**, **Parallel**, or **Split** routing with real-time OLED frequency-response plotting.
- **Per-Patch Stage (`HOME 2/3`):** Auto-saved `PATCH GAIN` (`-12..+12 dB`), pivoting `TILT EQ` (`-7..+7`), Mid/Side `M/S WIDTH` (`0..200%`), and transient `PUNCH` (`0..100%`).
- **4-Icon FX Rack (`FX 1/2` & `FX 2/2`):** Horizontal row of 4 calm animated OLED boxes inspired by [`vinish.dev/free-vfx`](https://vinish.dev/free-vfx) sprites (water ripples, force-field arc, comet echoes, magic aura embers, beam splitter, spiral portal vortex, lightning crest, and flame/ember core) for BBD Chorus, Phaser, BPM-synced Stereo Delay, Reverb, and Analog Saturation (`SOFT / TUBE / HARD / OFF`).
- **Dual ADSR Envelopes & Dual LFOs:** Dedicated Amp + Filter ADSR curves with live trigger tracer, plus 2 independent LFOs (`CUT1`, `CUT2`, `RESO`, `PITCH`, `PAN`, `SPREAD`, `AMP`, `CHORUS`, `PHASER`, `DELAY`, `REVERB`) with automatic FX & amp headroom protection.

### Preset Memory & Read-Only ROM Protection
- **ROM Banks `A–I` (288 Curated Presets):** Organized by role (`A` Pads, `B` Leads, `C` Bass, `D` Plucks, `E` Keys/Organs, `F` Arps, `G` Wobbles, `H` FX/Drones, `I` Starfish Originals). Factory ROM banks stay read-only when saving (`SAVE` routes edits into User `BANK X`).
- **User Flash Bank `X` (`X000–X127`):** Stores custom patches, randomized patches, and sequence edits on flash.
- **Per-Patch Auto-Saved Gain:** Turning `PATCH GAIN` on `HOME 2/3` auto-saves per preset without overwriting factory synth parameters.

### 8-Track Drum Machine, Sequencer, Combos & Boot/About (`SEQ`, `ARP`, `HOME 3/3`)
- **STARFISH Bootup & `OCT- + OCT+` About App:** Features a **STARFISH** OLED banner across 6 boot scenes; pressing `OCT-` and `OCT+` together plays a chorusy 3-voice Hypersaw Cmaj9 shimmer (`sf_dsp_start_easter_egg`) and opens the **About App** overview page.
- **10 Drum Kits:** `TR-808`, `TR-909`, `FM ACOUSTIC`, `CRISP DIGI`, `GLITCH`, `INDUSTRIAL`, `ANALOG`, `ACOUSTIC`, `8-BIT`, and `STARFISH`.
- **27-Key Touch Interface:** In `SEQ` mode, upper black keys `0–7` select drum voices (`BD..RS`), bottom 16 white keys toggle steps `1–16` across `1–4` bars, and the top-right key mutes/clears tracks. In Synth mode, recorded notes illuminate corresponding capsule keys during playback.
- **Global Mix (`HOME 3/3`):** `SYNTH LVL`, sidechain `KICK DUCK`, `DUCK SRC` (`KICK / KICK+SD / ALL DRM`), and `DRM LVL`.
- **Audio & Bluetooth I/O (`GLO 5/5` & `REC`):** USB Audio outputs full 44.1 kHz signal independently from hardware `SPK/HP` output level (`OFF`, `QUIET`, `NORM`, `+3dB`, `+6dB`). Short-press `REC` to scan BLE MIDI devices; hold `REC` 1s to toggle BLE radio power.

---

## License

Licensed under **MPL-2.0** (see `LICENSE`). Built on [`fm1-base`](https://github.com/charlesvestal/fm1-base) and [`felucca-bsp`](https://github.com/hugelton/felucca-bsp).
