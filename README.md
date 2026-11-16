# Starfish FM-1

**Starfish** is a pocket synthesizer simulator and sound-design workspace inspired by the **M-VAVE FM-1** and classic virtual-analog synthesizers.

## Features

- **Virtual-Analog + Hypersaw Synth Engine**
  - Dual oscillators with Hypersaw, Saw, Pulse, Wavetable, Sync, and Triangle modes, Sub-oscillator, Ring Mod, and Unison spread.
  - Dual resonant multimode filters (Serial, Parallel, Split) with saturation curves and dedicated Filter + Amp ADSR envelopes.
  - Dual LFOs (`LFO 1` & `LFO 2`) with routable destinations including Filters, Pitch, Pan, Spread, Amp Tremolo, and FX modulation.
  - Studio-grade Chorus, Phaser, Stereo Delay, and Diffuser Reverb.
  - **12 Musical Scales** + Chord Memory + **Arpeggiator** with **Latch** (`ARP + OCT±` or menu) and **Euclidean** rhythm options.
- **10-Kit Drum Machine & 64-Step Sequencer (`SEQ` Mode)**
  - Dedicated drum machine mode with 8 voices on top-row keys, 16-step bottom capsule entry (up to 64 steps), per-track Mute/Clear, Euclidean pulse generator, and global kit/bus processing.
- **Sound Banks (`A`–`I` & `X`) + Safe Patch Randomizer**
  - Factory ROM banks (`BANK A`–`BANK I`, including the 32-sound Starfish `BANK I`) plus user-writable **`BANK X` (`X000–X127`)**.
  - Styled patch randomizer with unsaved-patch confirmation prompt (`SAVE BEFORE LEAVE?`) and Bank X slot picker.
- **240×240 Hardware OLED Interface**
  - Six randomized animated Starfish boot scenes & six non-repeating boot jingles auditioned through your last 6 used presets.
  - Web MIDI input/output support.

## Quick Start

```bash
npm install
npm run dev
```

Open `http://localhost:3000` in your browser.

To create a production build:

```bash
npm run build
```

## Repository Structure

- `src/App.tsx` — Main application shell & sequencer/synth state
- `src/components/Fm1BasePanel.tsx` — 1:1 FM-1 hardware panel, OLED display renderer & menus
- `src/audio/` — Web Audio API synthesizer/drum DSP engine & Web MIDI manager
- `src/data/` — ROM preset banks (`A`–`I`), `BANK X` storage helpers, FM algorithms & firmware metadata
- `src/utils/` — Scale/chord/Euclidean helpers & `.fwsc` container builder
- `src/types/` — Shared TypeScript interfaces

## Hardware Export Status

This app runs in the browser using Web Audio and HTML5 Canvas. While the top bar includes a `.fwsc` template export, flashing a physical M-VAVE FM-1 requires compiling C firmware locally against Charles Vestal's `fm1-base` / `felucca-bsp` SDK and the JieLi AC791N toolchain.
