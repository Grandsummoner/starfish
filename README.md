
# ⭐ STARFISH for M-VAVE FM-1

<p align="center">
  <strong>8-Voice Virtual Analog & Hypersaw Synthesizer</strong><br>
  Dual Multimode Filters · 4-Icon FX Rack · 10-Kit Drum Sequencer<br>
  288 Curated Presets · 128 User Flash Slots
</p>

<p align="center">
  <a href="https://grandsummoner.github.io/starfish/">
    <img src="https://img.shields.io/badge/WEB%20INSTALLER-Launch-00A6A6?style=for-the-badge" alt="Web Installer">
  </a>
  <a href="https://grandsummoner.github.io/starfish/guide.html">
    <img src="https://img.shields.io/badge/USER%20GUIDE-Explore-5271C4?style=for-the-badge" alt="User Guide">
  </a>
  <img src="https://img.shields.io/badge/PLATFORM-M--VAVE%20FM--1-333333?style=for-the-badge" alt="Platform">
  <img src="https://img.shields.io/badge/LICENSE-MPL--2.0-green?style=for-the-badge" alt="License">
</p>

---

## 🌊 Overview

**STARFISH** transforms the **M-VAVE FM-1** into a complete portable synthesizer and groovebox.

Built on [`fm1-base`](https://github.com/charlesvestal/fm1-base) and [`felucca-bsp`](https://github.com/hugelton/felucca-bsp), STARFISH combines an eight-voice virtual analog synthesizer, dual multimode filters, expressive modulation, a creative effects rack, and a fully integrated drum sequencer.

From evolving Hypersaw textures and deep basses to rhythmic drum patterns and experimental sound design, STARFISH brings a complete production environment to a compact hardware instrument.

### At a Glance

| Feature | Specification |
|:--|:--|
| Synth Engine | 8-Voice Virtual Analog |
| Oscillators | Dual Oscillators + Sub-Oscillator |
| Oscillator Types | 6 main waveform modes |
| Filters | Dual Resonant Multimode |
| Filter Routing | Serial / Parallel / Split |
| Effects | Chorus, Phaser, Delay, Reverb, Saturation |
| Modulation | Dual ADSR + Dual LFO |
| Factory Presets | 288 Curated ROM Presets |
| User Presets | 128 Flash Slots (Bank X) |
| Drum Kits | 10 |
| Drum Sequencer | 8 Tracks / 16 Steps / 1–4 Bars |
| Display | 240 × 240 OLED |
| Connectivity | USB MIDI / USB Audio / Bluetooth MIDI |

---

## 🚀 Web Installer & Interactive User Guide

<p align="center">
  <a href="https://grandsummoner.github.io/starfish/">
    <strong>🌐 OPEN STARFISH WEB INSTALLER</strong>
  </a>
  <br>
  <sub>Install firmware, restore stock firmware and access device diagnostics.</sub>
</p>

STARFISH includes two companion web applications hosted on GitHub Pages.

### 1. Web MIDI Firmware Installer

**[Launch Web Installer →](https://grandsummoner.github.io/starfish/)**

Main landing page: `index.html`

Also available at `web-installer/index.html`.

Features:

- Install `starfish.fwsc` directly over USB-MIDI.
- Restore original M-VAVE stock firmware.
- Read device crash reports.
- Access firmware installation tools directly from your browser.

**Recommended browser:** Google Chrome or Microsoft Edge.

### 2. Interactive HTML User Guide

**[Explore the User Guide →](https://grandsummoner.github.io/starfish/guide.html)**

The interactive guide includes:

- Hardware button and control explanations.
- Live 240 × 240 OLED visualizations.
- Four animated FX icon boxes.
- Interactive 27-key drum sequencer layout.
- Searchable database of all 288 ROM presets.
- USB and Bluetooth MIDI routing documentation.

Both pages share the same visual theme, layout, Light/Dark mode toggle and navigation bar.

### Automatic GitHub Pages Deployment

With **Settings → Pages → Source: GitHub Actions** enabled, the workflow:

`.github/workflows/release.yml`

automatically compiles `starfish.fwsc` and publishes `index.html` and `guide.html` together.

---

# 🎹 Synthesizer Architecture

## 1. Sound Engine

<sub>Source: `app/dsp.c`, `app/presets.h`, `app/presets_rom.h`</sub>

### Dual Oscillators · 8 Voices

A versatile oscillator architecture designed for everything from classic analog sounds to complex evolving textures.

**Main oscillator modes:**

- `HYPERSAW`
- `SAW`
- `PULSE`
- `TRIANGLE`
- `HARD SYNC`
- `WAVETABLE`

Additional synthesis capabilities:

- Selectable Sub-Oscillator: `SQR / SAW / SIN / TRI`
- Noise Generator
- Ring Modulation
- FM Cross-Modulation
- Up to 5-Voice Unison with Stereo Spread

### Dual Resonant Multimode Filters

Two independently configurable resonant filters provide flexible sound shaping.

**Filter modes:**

`LP` · `BP` · `HP` · `NOTCH`

**Filter routing:**

| Mode | Description |
|:--|:--|
| Serial | Filter 1 feeds Filter 2 |
| Parallel | Both filters process the signal independently |
| Split | Divided filter routing |

Includes real-time OLED frequency-response plotting.

### Per-Patch Processing

**HOME 2/3** provides four additional per-patch processing controls.

| Parameter | Range | Function |
|:--|:--|:--|
| PATCH GAIN | -12 to +12 dB | Output level compensation |
| TILT EQ | -7 to +7 | Pivoting tonal balance |
| M/S WIDTH | 0–200% | Mid/Side stereo width |
| PUNCH | 0–100% | Transient enhancement |

All four parameters are automatically saved per patch.

---

## 2. Four-Icon Effects Rack

<sub>Controls: `FX 1/2` and `FX 2/2`</sub>

STARFISH features a visual effects interface built around a horizontal row of four animated OLED icon boxes.

Inspired by the visual effects and sprites at [vinish.dev/free-vfx](https://vinish.dev/free-vfx), the interface brings a distinctive visual identity to the FM-1 display.

### Animated Visual Themes

- Water Ripples
- Force-Field Arc
- Comet Echoes
- Magic Aura Embers
- Beam Splitter
- Spiral Portal Vortex
- Lightning Crest
- Flame / Ember Core

### Available Effects

| Effect | Description |
|:--|:--|
| BBD Chorus | Classic analog-style modulation |
| Phaser | Sweeping phase modulation |
| Stereo Delay | BPM-synchronized stereo echoes |
| Reverb | Spatial ambience |
| Analog Saturation | Selectable saturation character |

**Saturation modes:**

`SOFT` · `TUBE` · `HARD` · `OFF`

---

## 3. Modulation System

### Dual ADSR Envelopes

Dedicated Amp and Filter envelopes provide independent modulation.

Features include:

- Visual ADSR curve display.
- Live trigger tracer.
- Independent amplitude and filter shaping.

### Dual LFOs

Two independent LFOs provide extensive modulation destinations.

| Destination | Parameter |
|:--|:--|
| CUT1 | Filter 1 Cutoff |
| CUT2 | Filter 2 Cutoff |
| RESO | Resonance |
| PITCH | Pitch |
| PAN | Stereo Pan |
| SPREAD | Voice Spread |
| AMP | Amplitude |
| CHORUS | Chorus Amount |
| PHASER | Phaser Amount |
| DELAY | Delay Amount |
| REVERB | Reverb Amount |

Automatic FX and amplifier headroom protection helps maintain stable signal levels during modulation.

---

# 💾 Preset Memory System

## 4. Factory ROM — 288 Curated Presets

Factory presets are organized into nine banks, A–I, with 32 presets per bank.

| Bank | Category |
|:--:|:--|
| A | Pads |
| B | Leads |
| C | Bass |
| D | Plucks |
| E | Keys / Organs |
| F | Arps |
| G | Wobbles |
| H | FX / Drones |
| I | STARFISH Originals |

**ROM protection:** Factory presets remain read-only. Saving edits never overwrites the original factory sounds.

The `SAVE` function automatically routes edited patches into User Bank X.

## 5. User Flash Bank X — 128 Presets

User Bank X provides 128 writable flash-memory slots.

| Range | Capacity |
|:--|:--|
| X000–X127 | 128 User Presets |

Supports saving:

- Custom sound patches.
- Randomized patches.
- Sequence edits.

### Automatic Patch Gain Saving

`PATCH GAIN`, available on `HOME 2/3`, is automatically saved per preset without overwriting factory synthesizer parameters.

---

# 🥁 Drum Machine & Sequencer

## 6. Eight-Track Drum Machine

STARFISH includes ten selectable drum kits, covering classic drum-machine sounds, acoustic percussion and experimental digital textures.

### Available Drum Kits

| # | Drum Kit |
|:--:|:--|
| 01 | TR-808 |
| 02 | TR-909 |
| 03 | FM ACOUSTIC |
| 04 | CRISP DIGI |
| 05 | GLITCH |
| 06 | INDUSTRIAL |
| 07 | ANALOG |
| 08 | ACOUSTIC |
| 09 | 8-BIT |
| 10 | STARFISH |

## 7. Interactive 27-Key Drum Sequencer

The FM-1's 27-key touch interface becomes a compact drum programming surface.

### Key Layout

| Key Area | Function |
|:--|:--|
| Upper 8 Black Keys | Select drum voices 0–7 (`BD..RS`) |
| Bottom 16 White Keys | Toggle sequencer steps 1–16 |
| Top-Right Key | Mute / Clear tracks |

### Sequencer Capabilities

- Eight independent drum tracks.
- Sixteen steps per pattern.
- Support for 1–4 bars.
- Direct touch-based step programming.
- Individual drum voice selection.
- Track mute and clear controls.

In Synth mode, recorded notes illuminate their corresponding capsule keys during playback.

---

# 🎛️ Global Controls & Connectivity

## 8. Global Mixer

<sub>Location: `HOME 3/3`</sub>

Control the overall balance between synthesizer and drum-machine audio.

| Parameter | Function |
|:--|:--|
| SYNTH LVL | Synthesizer output level |
| KICK DUCK | Kick-triggered sidechain ducking |
| DUCK SRC | Selects sidechain trigger source |
| DRM LVL | Drum-machine output level |

**DUCK SRC options:**

`KICK` · `KICK+SD` · `ALL DRM`

## 9. USB Audio & Bluetooth MIDI

### USB Audio

STARFISH provides a full 44.1 kHz USB Audio output independent of the hardware `OUT LEVEL` control.

Available hardware output-level settings:

| Mode | Level |
|:--|:--|
| AUTO | 0 dB |
| LINE | -6 dB |
| BOOST | +3 dB |

### Bluetooth MIDI

Access Bluetooth MIDI device scanning by holding:

`GLO + HOME`

Bluetooth MIDI configuration is available under `GLO 4/4`.

Toggle the Bluetooth MIDI role or disable Bluetooth MIDI entirely using `OFF`.

---

# ✨ STARFISH Boot Experience & About App

## 10. Custom Boot Animation

STARFISH introduces a custom OLED boot experience featuring a STARFISH banner across six boot scenes.

### Hidden About App Easter Egg

Press `OCT-` and `OCT+` simultaneously.

This triggers:

- A chorus-rich, three-voice Hypersaw Cmaj9 shimmer.
- The STARFISH audio Easter egg (`sf_dsp_start_easter_egg`).
- The About App overview page.

A small hidden feature that adds a little personality to the instrument.

---

# 🛠️ Building STARFISH Firmware

The firmware package is distributed as:

`starfish.fwsc`

There are two supported build approaches.

## Option 1 — Automated GitHub Actions Build (Recommended)

1. Push the project directory to your GitHub repository.
2. Use the `main` or `master` branch, or create a release tag.
3. Trigger the **Build and Release** workflow.

Example release tag:

```bash
git tag v1.0
git push origin v1.0
```

### Automated Workflow

GitHub Actions automatically:

- Compiles `starfish.fwsc`.
- Runs `tests/run_tests.sh`.
- Publishes the release firmware binary.
- Deploys `index.html` and `guide.html` to GitHub Pages.

Ensure GitHub Pages is configured with:

**Settings → Pages → Source: GitHub Actions**

## Option 2 — Local Linux / macOS Build

### Requirements

- Linux or macOS build environment.
- `fm1-base` cloned into the `base/` directory.
- JieLi `pi32v2` toolchain.
- AC79 SDK configured.

### Compile Release Firmware

```bash
bash ./build.sh --release 1.0
```

Expected output:

```text
app/build/starfish-1.0.fwsc
```

### Host Simulator Build

The host simulator uses local GCC/Clang and does not require the embedded SDK.

Build:

```bash
bash ./build.sh --host
```

Run:

```bash
./app/build/host/starfish_host tests/basic.txt app/build/host
```

---

# 📚 Project Dependencies

STARFISH is built on the following open-source foundations:

| Project | Description |
|:--|:--|
| [`fm1-base`](https://github.com/charlesvestal/fm1-base) | M-VAVE FM-1 firmware foundation |
| [`felucca-bsp`](https://github.com/hugelton/felucca-bsp) | Hardware board support package |
| [`vinish.dev/free-vfx`](https://vinish.dev/free-vfx) | Visual inspiration for animated FX icons |

---

# 📄 License

STARFISH is released under the **Mozilla Public License 2.0 (MPL-2.0)**.

See [`LICENSE`](LICENSE) for the complete license terms.

Built on `fm1-base` and `felucca-bsp`.

---

<p align="center">
  <strong>⭐ STARFISH</strong><br>
  <sub>More than a synthesizer. A portable sound universe.</sub>
  <br><br>
  <a href="https://grandsummoner.github.io/starfish/">
    🌐 Web Installer
  </a>
  &nbsp;·&nbsp;
  <a href="https://grandsummoner.github.io/starfish/guide.html">
    📖 User Guide
  </a>
</p>
