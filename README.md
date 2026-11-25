# STARFISH for M-VAVE FM-1

**An 8-Voice Virtual Analog & Hypersaw Synthesizer · Dual Multimode Filters · 4-Icon FX Rack · 10-Kit Drum Sequencer · 288 Curated Presets + 128 User Flash Slots**

STARFISH transforms the **M-VAVE FM-1** into a portable synthesizer and groovebox, featuring an expanded sound engine, expressive effects, extensive preset management and an integrated drum sequencer.

---

## 🚀 Explore STARFISH

**Everything is accessible directly through your browser.**

| Resource | Description |
|:---|:---|
| 🌐 **[STARFISH Web Installer](https://grandsummoner.github.io/starfish/)** | Install STARFISH firmware over USB-MIDI, restore stock firmware and inspect device crash reports. |
| 📖 **[Interactive User Guide](https://grandsummoner.github.io/starfish/guide.html)** | Explore hardware controls, OLED simulations, the drum sequencer, all 288 presets and MIDI routing. |
| 🎨 **[Promo & EDM Showcase](https://grandsummoner.github.io/starfish/edm.html)** | Explore promotional screenshots, generate PNG posters and widescreen banners, and create announcement materials. |

All three applications share a common navigation bar for easy switching between the installer, guide and promotional showcase.

### Project Preview

![STARFISH Project Preview](docs/img/starfish-overview.png)

[🖼️ View Full-Size STARFISH Preview](docs/img/starfish-overview.png)

---

## 🎹 Synthesizer & Sound Engine

**Core implementation:** `app/dsp.c`, `app/presets.h`, `app/presets_rom.h`

- **8-Voice Dual Oscillator Engine:** HYPERSAW, SAW, PULSE, TRIANGLE, HARD SYNC and WAVETABLE, with Sub-Oscillator (SQR/SAW/SIN/TRI), Noise, Ring Modulation, FM cross-modulation and up to 5-voice Unison spread.
- **Dual Resonant Filters:** LP, BP, HP and NOTCH modes with Serial, Parallel and Split routing, including real-time OLED frequency-response plotting.
- **Per-Patch Processing (HOME 2/3):** Auto-saved PATCH GAIN (-12 to +12 dB), TILT EQ (-7 to +7), Mid/Side WIDTH (0–200%) and transient PUNCH (0–100%).

### 4-Icon Effects Rack

The FX interface features four animated OLED visualisation boxes, inspired by [Vinish's Free VFX sprite collection](https://vinish.dev/free-vfx).

Visual themes include water ripples, force-field arcs, comet echoes, magic aura embers, beam splitters, spiral portal vortexes, lightning crests and flame cores.

Available effects:

- BBD Chorus
- Phaser
- BPM-synchronised Stereo Delay
- Reverb
- Analog Saturation (SOFT / TUBE / HARD / OFF)

### Modulation

- **Dual ADSR Envelopes:** Dedicated Amp and Filter envelopes with live trigger tracing.
- **Dual Independent LFOs:** CUT1, CUT2, RESO, PITCH, PAN, SPREAD, AMP, CHORUS, PHASER, DELAY and REVERB destinations.
- **Automatic Protection:** FX and amplifier headroom protection.

---

## 💾 Preset Management & Memory

### Factory ROM — 288 Curated Presets

Nine read-only banks organised by sound category:

| Bank | Sound Category |
|:---:|:---|
| A | Pads |
| B | Leads |
| C | Bass |
| D | Plucks |
| E | Keys / Organs |
| F | Arpeggios |
| G | Wobbles |
| H | FX / Drones |
| I | STARFISH Originals |

Factory ROM banks remain read-only. Saving edited sounds routes them into the user flash bank instead.

### User Flash Bank X

- 128 writable patch slots: `X000–X127`
- Stores custom patches and randomised patches.
- Supports sequence edits.
- Per-patch gain settings are automatically saved without overwriting factory synth parameters.

---

## 🥁 Drum Machine & Sequencer

### 10 Integrated Drum Kits

`TR-808` · `TR-909` · `FM ACOUSTIC` · `CRISP DIGI` · `GLITCH` · `INDUSTRIAL` · `ANALOG` · `ACOUSTIC` · `8-BIT` · `STARFISH`

### 27-Key Touch Sequencer

The touch interface provides dedicated controls for drum selection and step programming.

- Upper 8 black keys: Select drum voices (BD–RS).
- Bottom 16 white keys: Toggle sequencer steps 1–16.
- Supports 1–4 bars.
- Top-right key: Track mute / clear functions.
- Synth mode: Recorded notes illuminate corresponding capsule keys during playback.

### Global Mixing (HOME 3/3)

- SYNTH LVL
- KICK DUCK sidechain
- DUCK SRC (KICK / KICK+SD / ALL DRM)
- DRM LVL

---

## 🔊 Audio, MIDI & Connectivity

### USB Audio

- Full 44.1 kHz USB Audio output.
- Independent hardware speaker/headphone level control.
- Output levels: OFF, QUIET, NORM, +3dB and +6dB.

### Bluetooth MIDI

- Short-press REC to scan for BLE MIDI devices.
- Hold REC for 1 second to toggle Bluetooth radio power.

---

## ✨ STARFISH Boot Experience & Easter Egg

The STARFISH boot sequence features an OLED banner across six boot scenes.

An additional hidden feature is accessible through the OCT- and OCT+ buttons:

- Press both buttons together.
- Plays a chorus-enhanced three-voice Hypersaw Cmaj9 shimmer.
- Opens the About App overview page.

Implementation: `sf_dsp_start_easter_egg`

---

## 🛠️ Build & Deployment

STARFISH uses GitHub Actions for automated firmware compilation and GitHub Pages deployment.

With GitHub Pages configured under:

**Settings → Pages → Build and deployment → Source: GitHub Actions**

The workflow `.github/workflows/release.yml` automatically compiles the firmware and publishes the web applications.

Published resources include:

- Web MIDI firmware installer
- Interactive HTML user guide
- Promotional and EDM showcase
- Email announcement generator

The web applications are served through GitHub Pages and can be accessed independently through the links above.

---

## 📜 Credits & License

**License:** [Mozilla Public License 2.0 (MPL-2.0)](LICENSE)

STARFISH builds upon the following projects:

- [FM1 Base Firmware](https://github.com/charlesvestal/fm1-base)
- [Felucca Board Support Package](https://github.com/hugelton/felucca-bsp)
