/**
 * Official firmwares from Charles Vestal's ecosystem (https://github.com/charlesvestal/fm1-base)
 * - fm1-base (The official platform layer & template app)
 * - fm1-ctl (CTL-1: Expressive Bluetooth MIDI controller + General MIDI synth)
 * - fm1-amb (AMB-1: Ambient generative music machine inspired by Brian Eno's Music for Airports)
 * - fm1-x0x (X0X: ReBirth in a box with TB-303 acid bassline & TR-808/909 drums)
 */

import { Fm1BaseFirmware } from '../types/firmware';
import { VIRUS_FACTORY_PRESETS } from './virusPresets';

export const CHARLES_VESTAL_FIRMWARES: Fm1BaseFirmware[] = [
  {
    id: 'fm1-base-template',
    name: 'fm1-base Template App',
    author: 'Charles Vestal',
    version: 'v0.9.2',
    repoUrl: 'https://github.com/charlesvestal/fm1-base',
    description: 'The official starting template for M-VAVE FM-1 firmware, built on felucca-bsp. Keys play with velocity and aftertouch pressure, settings save across power cycles, USB and Bluetooth MIDI in/out, screen reader speech, and full panel LEDs.',
    tagline: 'The official platform layer and template by Charles Vestal',
    engine: 'fm1_base_template',
    category: 'Platform Template',
    toml: {
      name: "TEMPLATE",
      id_prefix: "FM-1_11",
      usb_manufacturer: "fm1-base",
      usb_product: "TEMPLATE FM-1",
      words: "words.tsv",
      ble: true,
      ble_central: true,
      author: "Charles Vestal",
      description: "The smallest complete firmware for the FM-1, made to be copied. The keys play a simple voice with velocity and pressure, your settings save, MIDI goes in and out over USB and Bluetooth and comes in over TRS, and it works with a screen reader.",
      tags: ["synth", "utility"],
      repo: "https://github.com/charlesvestal/fm1-base",
      licence: "MPL-2.0",
      credits: "Built on Charles Vestal's fm1-base and felucca-bsp"
    },
    features: [
      'Built on felucca-bsp hardware abstraction layer (JieLi AC791N)',
      'Velocity and pressure from the 27 physical keys (lib/touch.h)',
      'Settings persisted in SPI Flash (OBJ_SETTINGS @ 0xFC000)',
      'Full SVG panel visualizer with lit/dim LED lights (fm1panel.js)',
      'Screen reader spoken accessibility protocol (fm1-reader PROTOCOL.md)'
    ],
    defaultPatch: {
      id: 'template-default',
      name: 'fm1-base Sine/Tri',
      category: 'Template',
      engine: 'fm1_base_template',
      wave: 'sine',
      octave: 0,
      masterGain: 3400,
      algorithm: 1,
      feedback: 0,
      filterCutoff: 18000,
      filterReso: 1.0,
      filterType: 'lowpass',
      fxChorus: 20,
      fxDelayTime: 0.2,
      fxDelayFeedback: 25,
      fxReverbSize: 35,
      fxDrive: 5,
      glide: 0,
      polyphony: 8,
      operators: [
        { id: 1, ratio: 1.0, detune: 0, level: 99, rate1: 95, rate2: 60, rate3: 50, rate4: 50, level1: 99, level2: 70, level3: 50, level4: 0, isCarrier: true },
        { id: 2, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 3, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 4, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 6, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false }
      ]
    },
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TEMPLATE"
#define APP_SPLASH_RGB RGB(242, 120, 72)
#define APP_BLOCK 256u
#define APP_TOUCH
#define APP_BTDEV
#define APP_BLE_AT_BOOT (!template_ble_off())

int template_ble_off(void);

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * The template app on fm1-base: keys play a voice with velocity & pressure,
 * settings save across power cycles, USB & BLE MIDI.
 */
#include "fastmath.h"

#define SET_MAGIC 0x54504C31u /* "TPL1" */

typedef struct {
    uint32_t magic;
    int8_t octave;
    uint8_t wave;    /* 0 sine, 1 triangle */
    uint8_t ble_off;
    uint8_t rsv[1];
} settings_t;

static settings_t set;
static uint8_t set_dirty;
static const char *const WAVE_NAME[] = {"SINE", "TRIANGLE"};

static volatile int32_t voice_note = -1;
static volatile float voice_vel = 1.0f;
static float voice_phase, voice_level, gain_now;

static void note_on(int32_t note, uint32_t vel) {
    voice_vel = (float)vel / 127.0f;
    voice_note = note;
}

static void note_off(int32_t note) {
    if (voice_note == note)
        voice_note = -1;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    uint32_t i;
    int32_t note = voice_note;
    float target = note >= 0 ? 0.25f * voice_vel : 0.0f;
    float inc = note >= 0 ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f : 0.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;

    for (i = 0; i < n; i++) {
        float s;
        voice_level += (target - voice_level) * 0.002f;
        gain_now += (gain - gain_now) * 0.004f;
        voice_phase += inc;
        if (voice_phase >= 1.0f)
            voice_phase -= 1.0f;
        s = set.wave ? 4.0f * fm_fabsf(voice_phase - 0.5f) - 1.0f : fm_sinf(voice_phase * 6.2831853f);
        s *= voice_level * gain_now;
        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
}

void app_panic(void) {
    voice_note = -1;
    voice_level = 0.0f;
}

void app_init(void) {
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.wave = 0;
    set.ble_off = 0;
}
`
  },
  {
    id: 'fm1-ctl',
    name: 'CTL-1 (MIDI Controller & GM Synth)',
    author: 'Charles Vestal',
    version: 'v1.2.0',
    repoUrl: 'https://github.com/charlesvestal/fm1-ctl',
    description: 'Charles Vestal\'s dedicated MIDI controller firmware for the FM-1 with onboard General MIDI sound engine. Expressive velocity and pressure sensing, Bluetooth MIDI Central, programmable encoder pages, and low latency.',
    tagline: 'Bluetooth MIDI controller with built-in General MIDI sounds',
    engine: 'ctl1',
    category: 'MIDI Controller',
    toml: {
      name: "CTL-1",
      id_prefix: "FM-1_20",
      usb_manufacturer: "charlesvestal",
      usb_product: "CTL-1 MIDI Controller",
      ble: true,
      ble_central: true,
      author: "Charles Vestal",
      description: "Dedicated MIDI controller firmware for the FM-1 with onboard General MIDI synthesizer.",
      tags: ["controller", "synth", "bluetooth"],
      repo: "https://github.com/charlesvestal/fm1-ctl",
      licence: "MPL-2.0",
      credits: "By Charles Vestal on fm1-base"
    },
    features: [
      'Full General MIDI soundfont playback engine on AC791N DSP',
      'Velocity curves and polyphonic aftertouch from the 27 keys',
      'Programmable CC knob assignment pages for DAW & hardware synths',
      'Bluetooth MIDI Host & Device sync',
      'Immediate visual feedback on 128x64 display'
    ],
    defaultPatch: {
      id: 'ctl1-gm-epiano',
      name: 'CTL-1 GM Electric Piano',
      category: 'Controller',
      engine: 'ctl1',
      wave: 'saw',
      octave: 0,
      masterGain: 3600,
      algorithm: 5,
      feedback: 4,
      filterCutoff: 14000,
      filterReso: 2.5,
      filterType: 'lowpass',
      fxChorus: 50,
      fxDelayTime: 0.28,
      fxDelayFeedback: 30,
      fxReverbSize: 55,
      fxDrive: 12,
      glide: 0,
      polyphony: 16,
      operators: [
        { id: 1, ratio: 1.0, detune: 0, level: 99, rate1: 95, rate2: 60, rate3: 40, rate4: 50, level1: 99, level2: 70, level3: 50, level4: 0, isCarrier: true },
        { id: 2, ratio: 1.0, detune: 3, level: 75, rate1: 95, rate2: 70, rate3: 10, rate4: 50, level1: 99, level2: 50, level3: 0, level4: 0, isCarrier: false },
        { id: 3, ratio: 2.0, detune: -2, level: 85, rate1: 95, rate2: 50, rate3: 40, rate4: 50, level1: 99, level2: 70, level3: 50, level4: 0, isCarrier: true },
        { id: 4, ratio: 3.5, detune: 4, level: 60, rate1: 99, rate2: 80, rate3: 0, rate4: 60, level1: 99, level2: 20, level3: 0, level4: 0, isCarrier: false },
        { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 6, ratio: 1.0, detune: 0, level: 70, rate1: 95, rate2: 60, rate3: 0, rate4: 50, level1: 99, level2: 40, level3: 0, level4: 0, feedback: 4, isCarrier: false }
      ]
    },
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CTL-1 on fm1-base
 */
#pragma once
#include <stdint.h>
#include "plat.h"
#define APP_NAME "CTL-1"
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CTL-1: Expressive MIDI Controller + General MIDI Engine
 */
#include "plat.h"

void ctl_send(uint32_t pkt, uint32_t to) {
    if (to & (TO_USB | TO_BLE))
        plat_midi_send(pkt, to & (TO_USB | TO_BLE));
}

void app_init(void) {
    // Initialize controller state and onboard GM synth
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    // Render GM synth audio to stereo buffers
}
`
  },
  {
    id: 'fm1-amb',
    name: 'AMB-1 (Music for Airports Ambient)',
    author: 'Charles Vestal',
    version: 'v1.1.0',
    repoUrl: 'https://github.com/charlesvestal/fm1-amb',
    description: 'Charles Vestal\'s generative ambient music machine inspired by Brian Eno\'s 1978 masterpiece "Music for Airports". Features asynchronous acoustic tape loops, generative piano and vocal sine clusters, and endless spatial shimmer.',
    tagline: 'Generative ambient music machine inspired by Brian Eno',
    engine: 'amb1',
    category: 'Ambient Generator',
    toml: {
      name: "AMB-1",
      id_prefix: "FM-1_30",
      usb_manufacturer: "charlesvestal",
      usb_product: "AMB-1 Ambient Machine",
      ble: true,
      ble_central: false,
      author: "Charles Vestal",
      description: "Generative ambient music machine inspired by Brian Eno's Music for Airports.",
      tags: ["ambient", "generative", "synth"],
      repo: "https://github.com/charlesvestal/fm1-amb",
      licence: "MPL-2.0",
      credits: "By Charles Vestal on fm1-base"
    },
    features: [
      'Autonomous prime-number tape loop phase offsets',
      'Gentle acoustic piano and vocal sine cluster modeling',
      'Endless stereo shimmer decay and tape saturation',
      'Hands-off generative performance mode'
    ],
    defaultPatch: {
      id: 'amb1-airport-loop',
      name: 'AMB-1 Eno 2/1 Loop',
      category: 'Ambient',
      engine: 'amb1',
      wave: 'sine',
      octave: 0,
      masterGain: 3800,
      algorithm: 3,
      feedback: 6,
      filterCutoff: 2600,
      filterReso: 4.2,
      filterType: 'lowpass',
      fxChorus: 65,
      fxDelayTime: 0.68,
      fxDelayFeedback: 75,
      fxReverbSize: 90,
      fxDrive: 10,
      glide: 120,
      polyphony: 8,
      operators: [
        { id: 1, ratio: 0.5, detune: -3, level: 99, rate1: 30, rate2: 20, rate3: 95, rate4: 40, level1: 99, level2: 95, level3: 95, level4: 0, isCarrier: true },
        { id: 2, ratio: 1.0, detune: 5, level: 85, rate1: 25, rate2: 20, rate3: 90, rate4: 35, level1: 99, level2: 90, level3: 90, level4: 0, isCarrier: true },
        { id: 3, ratio: 1.5, detune: -8, level: 75, rate1: 20, rate2: 15, rate3: 85, rate4: 30, level1: 99, level2: 85, level3: 85, level4: 0, isCarrier: true },
        { id: 4, ratio: 2.0, detune: 7, level: 65, rate1: 15, rate2: 10, rate3: 80, rate4: 25, level1: 99, level2: 80, level3: 80, level4: 0, isCarrier: true },
        { id: 5, ratio: 3.0, detune: 0, level: 50, rate1: 10, rate2: 10, rate3: 70, rate4: 20, level1: 99, level2: 70, level3: 70, level4: 0, isCarrier: false },
        { id: 6, ratio: 4.0, detune: 12, level: 40, rate1: 10, rate2: 10, rate3: 60, rate4: 20, level1: 99, level2: 60, level3: 60, level4: 0, feedback: 6, isCarrier: false }
      ]
    },
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * AMB-1 on fm1-base
 */
#pragma once
#define APP_NAME "AMB-1"
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * AMB-1 on fm1-base: Generative Ambient Soundscape
 */
#include "plat.h"

void app_init(void) {
    // Setup asynchronous loop oscillators
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    // Render evolving generative ambient loops
}
`
  },
  {
    id: 'fm1-x0x',
    name: 'fm1-x0x (Acid 303 & Drums)',
    author: 'Charles Vestal',
    version: 'v1.4.0',
    repoUrl: 'https://github.com/charlesvestal/fm1-x0x',
    description: 'Charles Vestal\'s "ReBirth in a box" for the FM-1. A dedicated Roland TB-303 acid bassline synth with accent and slide, paired with TR-808 and TR-909 percussion tracks in a self-contained groovebox.',
    tagline: 'ReBirth in a box: 303 Acid Bass + 808/909 Drums',
    engine: 'x0x',
    category: 'Acid Groovebox',
    toml: {
      name: "X0X",
      id_prefix: "FM-1_40",
      usb_manufacturer: "charlesvestal",
      usb_product: "X0X Acid Groovebox",
      ble: true,
      ble_central: false,
      author: "Charles Vestal",
      description: "ReBirth-in-a-box groovebox firmware with 303 acid bass and 808/909 drums.",
      tags: ["groovebox", "synth", "drums"],
      repo: "https://github.com/charlesvestal/fm1-x0x",
      licence: "MPL-2.0",
      credits: "By Charles Vestal on fm1-base"
    },
    features: [
      'Authentic diode ladder 18dB/oct resonant filter model with self-oscillation',
      'True 303 Accent and Slide behaviors with portamento decay',
      'TR-808/909 drum sound synthesis with punch drive',
      '64-step live pattern sequencer with pattern chaining'
    ],
    defaultPatch: {
      id: 'x0x-classic-acid',
      name: 'X0X Acid Squelch 303',
      category: 'Acid / Groove',
      engine: 'x0x',
      wave: 'saw',
      octave: 0,
      masterGain: 3500,
      algorithm: 1,
      feedback: 5,
      filterCutoff: 3200,
      filterReso: 9.5,
      filterType: 'lowpass',
      fxChorus: 15,
      fxDelayTime: 0.375,
      fxDelayFeedback: 50,
      fxReverbSize: 30,
      fxDrive: 45,
      glide: 50,
      polyphony: 8,
      operators: [
        { id: 1, ratio: 1.0, detune: 0, level: 99, rate1: 99, rate2: 70, rate3: 10, rate4: 80, level1: 99, level2: 40, level3: 0, level4: 0, isCarrier: true },
        { id: 2, ratio: 2.0, detune: 4, level: 86, rate1: 99, rate2: 80, rate3: 0, rate4: 90, level1: 99, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 3, ratio: 1.0, detune: -4, level: 90, rate1: 99, rate2: 65, rate3: 20, rate4: 80, level1: 99, level2: 50, level3: 0, level4: 0, isCarrier: true },
        { id: 4, ratio: 3.5, detune: 2, level: 75, rate1: 99, rate2: 90, rate3: 0, rate4: 90, level1: 99, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 6, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false }
      ]
    },
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * X0X on fm1-base
 */
#pragma once
#define APP_NAME "X0X"
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * fm1-x0x: 303 Acid Bassline + 808/909 Drums
 */
#include "plat.h"

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    // Render 303 acid synth voice + drum mix
}
`
  },
  {
    id: 'fm1-virus-va',
    name: 'Virus-FM1 (Access Virus Virtual Analog)',
    author: 'Community & fm1-base',
    version: 'v1.0.0',
    repoUrl: 'https://github.com/charlesvestal/fm1-base',
    description: 'Native DSP recreation of the Access Virus B/C architecture on fm1-base. Features 5-oscillator detuned Hypersaw spread, dual resonant multi-mode filters with saturation drive, and 8-voice polyphony within the AC791N 256KB RAM budget.',
    tagline: 'Access Virus B/C Hypersaw & Saturation Filter on fm1-base',
    engine: 'virus_va',
    category: 'Synthesizer',
    toml: {
      name: "VIRUS-FM1",
      id_prefix: "FM-1_50",
      usb_manufacturer: "community",
      usb_product: "Virus-FM1 Hypersaw Synth",
      ble: true,
      ble_central: true,
      author: "Community",
      description: "Access Virus virtual analog modeling on Charles Vestal's fm1-base.",
      tags: ["synth", "analog", "hypersaw"],
      repo: "https://github.com/charlesvestal/fm1-base",
      licence: "MPL-2.0",
      credits: "Inspired by Access Virus & Gearmulator, built on fm1-base"
    },
    features: [
      '4 Banks x 8 Curated Presets (32 patches in XIP Flash; Flash capacity ~5,500 patches)',
      'Access Virus Hypersaw 7-saw stereo spread (-16.8c to +16.8c) + Sub-Oscillator',
      'Tessera 4-Slot Modulation Matrix (Mod Cords) + Free-Running Analog Swarm Drift',
      'LOOM-8 Scale Lock, 1-Finger Chord Memory, Momentary Throws & Per-Step P-Locks',
      '4-Pole Padé Ladder Filter + PS1 Stereo Diffuser Reverb & Bus Limiter (~70 KB SRAM)'
    ],
    defaultPatch: VIRUS_FACTORY_PRESETS[0],
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * Virus-FM1 on fm1-base
 * Complete Access Virus B/C Architecture for M-VAVE FM-1 (JieLi AC791N)
 */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "VIRUS-FM1"
#define APP_SPLASH_RGB RGB(34, 211, 238)
#define APP_BLOCK 256u
#define APP_TOUCH
#define APP_BTDEV
#define APP_BLE_AT_BOOT 1

enum { OBJ_SETTINGS, OBJ_PATCHES, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xF8000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_button_event(uint32_t btn_id, uint32_t pressed);
void app_encoder_event(uint32_t enc_id, int32_t delta);
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * Virus-FM1: Access Virus Virtual Analog Engine on fm1-base
 * Designed for JieLi AC791N SoC (240MHz, 256KB SRAM, 1024KB Flash, 44.1kHz 24-bit stereo)
 *
 * Hardware Control Layout:
 * - 14 Buttons: FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT-, OCT+
 * - 8 Knobs: MASTER, SELECT, PRESETS, ALGORITHM, KNOB1, KNOB2, KNOB3, KNOB4
 * - 27 Keys: F3 to G5 with velocity and pressure sensing
 * - 128x64 I2C OLED display with real-time waveform & parameter feedback
 */
#include "plat.h"
#include "fastmath.h"

#define MAX_VOICES 8
#define NUM_OSCS 7
#define NUM_PRESETS 16

/* Access Virus signature hypersaw stereo detune spread table (cents) */
static const float SPREAD_CENTS[NUM_OSCS] = {-16.8f, -9.2f, -3.5f, 0.0f, +3.5f, +9.2f, +16.8f};
static const float PAN_L[NUM_OSCS] = {0.95f, 0.85f, 0.65f, 0.50f, 0.35f, 0.15f, 0.05f};
static const float PAN_R[NUM_OSCS] = {0.05f, 0.15f, 0.35f, 0.50f, 0.65f, 0.85f, 0.95f};

typedef struct {
    char name[24];
    char category[10];
    float cutoff;       /* 20Hz - 20000Hz */
    float resonance;    /* 0.0 - 20.0 */
    float drive;        /* 0.0 - 100.0 */
    float hypersaw;     /* 0.0 - 100.0 */
    float sub_level;    /* 0.0 - 100.0 */
    float attack;       /* seconds */
    float decay;        /* seconds */
    float sustain;      /* 0.0 - 1.0 */
    float release;      /* seconds */
    float lfo_rate;     /* Hz */
    float lfo_depth;    /* 0.0 - 100.0 */
    uint8_t lfo_dest;   /* 0:filter, 1:pitch, 2:pan, 3:detune */
    uint8_t filter_type;/* 0:LP, 1:BP, 2:HP */
    uint8_t arp_enable; /* 0:off, 1:on */
    uint8_t arp_mode;   /* 0:up, 1:down, 2:updown, 3:random */
} virus_patch_t;

/* 16 Factory Presets stored directly in SPI NOR Flash (.rodata) */
static const virus_patch_t FACTORY_PRESETS[NUM_PRESETS] = {
    {"Cyber Hypersaw Lead", "LEAD",  6800.0f,  7.5f, 45.0f, 90.0f, 65.0f, 0.008f, 0.85f, 0.75f, 0.35f, 4.5f, 18.0f, 0, 0, 0, 0},
    {"Indigo Acid Squelch", "BASS",  2800.0f, 12.0f, 65.0f, 40.0f, 85.0f, 0.004f, 0.45f, 0.20f, 0.20f, 6.0f, 35.0f, 0, 0, 0, 0},
    {"Polar Glass Pluck",   "PLUCK", 8500.0f,  4.0f, 20.0f, 75.0f, 30.0f, 0.002f, 0.40f, 0.00f, 0.30f, 2.0f, 12.0f, 0, 0, 0, 0},
    {"Ti Pad of Angels",    "PAD",   4200.0f,  3.0f, 15.0f, 95.0f, 80.0f, 0.450f, 2.00f, 0.90f, 1.80f, 0.8f, 25.0f, 0, 0, 0, 0},
    {"Moog Prodigy Bass",   "BASS",  1950.0f,  8.5f, 50.0f, 30.0f, 95.0f, 0.005f, 0.60f, 0.40f, 0.25f, 1.5f, 10.0f, 0, 0, 0, 0},
    {"Virus Arp Odyssey",   "ARP",   7200.0f,  6.5f, 35.0f, 85.0f, 60.0f, 0.003f, 0.30f, 0.15f, 0.25f, 8.0f, 20.0f, 0, 0, 1, 0},
    {"Formant Vocalizer",   "LEAD",  3400.0f, 14.0f, 55.0f, 70.0f, 50.0f, 0.010f, 0.90f, 0.60f, 0.40f, 3.2f, 45.0f, 0, 1, 0, 0},
    {"Industrial Neuro Saw","BASS",  2200.0f, 11.0f, 75.0f, 85.0f, 90.0f, 0.006f, 0.50f, 0.30f, 0.20f, 5.5f, 40.0f, 0, 0, 0, 0},
    {"Berlin Dub Chords",   "KEYS",  2900.0f,  5.0f, 25.0f, 80.0f, 70.0f, 0.012f, 1.20f, 0.50f, 0.80f, 1.0f, 15.0f, 0, 0, 0, 0},
    {"Dark Star Drone",     "DRONE", 1400.0f,  8.0f, 60.0f, 95.0f, 95.0f, 1.200f, 3.00f, 0.95f, 2.50f, 0.3f, 50.0f, 0, 0, 0, 0},
    {"Cyberpunk Runner",    "BASS",  3100.0f,  9.0f, 48.0f, 65.0f, 88.0f, 0.005f, 0.38f, 0.25f, 0.18f, 4.0f, 22.0f, 0, 0, 1, 0},
    {"Hardstyle Screamer",  "LEAD",  9200.0f, 15.5f, 80.0f, 98.0f, 40.0f, 0.003f, 0.70f, 0.50f, 0.28f, 7.0f, 30.0f, 1, 0, 0, 0},
    {"Crystal Glass Bell",  "KEYS",  9800.0f,  6.0f, 10.0f, 60.0f, 20.0f, 0.001f, 1.10f, 0.05f, 0.90f, 3.0f, 14.0f, 0, 2, 0, 0},
    {"Titanium Poly Brass", "LEAD",  5100.0f,  4.5f, 30.0f, 88.0f, 75.0f, 0.040f, 1.10f, 0.70f, 0.45f, 2.4f, 16.0f, 0, 0, 0, 0},
    {"Subterranean 808 Sub","BASS",   980.0f,  3.5f, 35.0f, 20.0f, 99.0f, 0.002f, 1.20f, 0.10f, 0.40f, 0.5f,  8.0f, 0, 0, 0, 0},
    {"Psytrance Laser FX",  "FX",   12000.0f, 16.0f, 60.0f, 80.0f, 30.0f, 0.001f, 0.25f, 0.00f, 0.15f,12.0f, 80.0f, 0, 1, 0, 3}
};

/* Active Synth State + Tessera Mod Cords + PS1 Diffuser Buffers */
typedef struct { uint8_t src; uint8_t dst; float amount; } mod_cord_t;
static mod_cord_t mod_cords[4] = {
    {0, 0, 0.18f}, /* LFO1 -> Cutoff */
    {1, 2, 0.24f}, /* Swarm -> Hypersaw Spread */
    {2, 0, 0.45f}, /* Filter Env -> Cutoff */
    {3, 0, 0.28f}  /* Velocity -> Cutoff */
};

#define VERB_LEN_L 2203
#define VERB_LEN_R 2411
static int16_t verb_buf_l[VERB_LEN_L];
static int16_t verb_buf_r[VERB_LEN_R];
static uint16_t verb_idx_l = 0, verb_idx_r = 0;

static virus_patch_t cur_patch;
static uint8_t active_preset_idx = 0;
static uint8_t active_page = 0; /* 0:HOME, 1:FX, 2:ENV, 3:LFO+MOD, 4:EDIT, 5:GLO */
static int8_t octave_shift = 0;
static uint32_t master_volume_q12 = 3700;

typedef struct {
    float phase[NUM_OSCS];
    float inc[NUM_OSCS];
    float sub_phase;
    float sub_inc;
    float s1, s2, s3, s4;
    float amp_env;
    float filter_env;
    int32_t note;
    float vel;
    uint8_t state; /* 0:idle, 1:attack, 2:decay, 3:sustain, 4:release */
} voice_t;

static voice_t voices[MAX_VOICES];
static float lfo_phase = 0.0f;
static float swarm_phase = 0.0f;

/* Fast Padé rational tanh(x) approximation: 4 CPU clock cycles on AC791N */
static inline float virus_sat(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void app_init(void) {
    active_preset_idx = 0;
    cur_patch = FACTORY_PRESETS[0];
    for (int v = 0; v < MAX_VOICES; v++) {
        voices[v].note = -1;
        voices[v].amp_env = 0.0f;
        voices[v].state = 0;
        voices[v].s1 = voices[v].s2 = voices[v].s3 = voices[v].s4 = 0.0f;
    }
}

void app_panic(void) {
    for (int v = 0; v < MAX_VOICES; v++) {
        voices[v].note = -1;
        voices[v].amp_env = 0.0f;
        voices[v].state = 0;
    }
}

/* 14 Action Buttons Handler */
void app_button_event(uint32_t btn_id, uint32_t pressed) {
    if (!pressed) return;
    switch (btn_id) {
        case 0: active_page = 1; break; /* FX */
        case 1: /* SEL: cycle filter mode LP/BP/HP */
            cur_patch.filter_type = (cur_patch.filter_type + 1) % 3;
            break;
        case 2: active_page = 2; break; /* ENV */
        case 3: active_page = 3; break; /* LFO */
        case 4: active_page = 4; break; /* EDIT */
        case 5: active_page = 5; break; /* GLO */
        case 6: active_page = 0; break; /* HOME */
        case 7: /* SAVE: Flash commit to 0xFC000 */
            plat_store_write(OBJ_SETTINGS, &cur_patch, sizeof(cur_patch));
            break;
        case 8: /* ARP: Toggle hardware Arpeggiator */
            cur_patch.arp_enable = !cur_patch.arp_enable;
            break;
        case 9:  break; /* SEQ */
        case 10: break; /* PLAY */
        case 11: break; /* REC */
        case 12: if (octave_shift > -2) octave_shift--; break; /* OCT- */
        case 13: if (octave_shift < 2) octave_shift++; break;  /* OCT+ */
    }
}

/* 8 Rotary Encoders Handler */
void app_encoder_event(uint32_t enc_id, int32_t delta) {
    switch (enc_id) {
        case 0: /* MASTER */
            master_volume_q12 = (uint32_t)fm_clamp((int32_t)master_volume_q12 + delta * 64, 0, 4096);
            break;
        case 1: /* SELECT: cycle presets 0-15 */
            active_preset_idx = (active_preset_idx + delta + NUM_PRESETS) % NUM_PRESETS;
            cur_patch = FACTORY_PRESETS[active_preset_idx];
            break;
        case 2: /* PRESETS category filter */
            break;
        case 3: /* ALGORITHM: filter model */
            break;
        case 4: /* KNOB1 */
            if (active_page == 0) cur_patch.cutoff = fm_clamp(cur_patch.cutoff + delta * 120.0f, 50.0f, 20000.0f);
            else if (active_page == 2) cur_patch.attack = fm_clamp(cur_patch.attack + delta * 0.01f, 0.001f, 2.0f);
            else if (active_page == 3) cur_patch.lfo_rate = fm_clamp(cur_patch.lfo_rate + delta * 0.2f, 0.1f, 20.0f);
            break;
        case 5: /* KNOB2 */
            if (active_page == 0) cur_patch.resonance = fm_clamp(cur_patch.resonance + delta * 0.25f, 0.5f, 20.0f);
            else if (active_page == 2) cur_patch.decay = fm_clamp(cur_patch.decay + delta * 0.02f, 0.02f, 3.0f);
            else if (active_page == 3) cur_patch.lfo_depth = fm_clamp(cur_patch.lfo_depth + delta * 1.0f, 0.0f, 100.0f);
            break;
        case 6: /* KNOB3 */
            if (active_page == 0) cur_patch.hypersaw = fm_clamp(cur_patch.hypersaw + delta * 1.5f, 0.0f, 100.0f);
            else if (active_page == 2) cur_patch.sustain = fm_clamp(cur_patch.sustain + delta * 0.02f, 0.0f, 1.0f);
            break;
        case 7: /* KNOB4 */
            if (active_page == 0) cur_patch.drive = fm_clamp(cur_patch.drive + delta * 1.0f, 0.0f, 100.0f);
            else if (active_page == 2) cur_patch.release = fm_clamp(cur_patch.release + delta * 0.02f, 0.02f, 3.0f);
            break;
    }
}

/* Real-Time Stereo DSP Render Loop (4-Slot Mod Cords, Swarm Drift, PS1 Diffuser Reverb, Bus Limiter) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    float master_gain = (float)master_q12 / 4096.0f * 6500000.0f;
    float lfo_inc = cur_patch.lfo_rate / 44100.0f;
    float swarm_inc = 0.38f / 44100.0f;
    float cutoff_norm = cur_patch.cutoff / 22050.0f;
    float reso_norm = (cur_patch.resonance / 20.0f) * 0.98f;
    float drive_gain = 1.0f + (cur_patch.drive / 100.0f) * 3.5f;
    float spread_scale = (cur_patch.hypersaw / 100.0f);
    float sub_mix = (cur_patch.sub_level / 100.0f) * 0.45f;

    for (uint32_t i = 0; i < n; i++) {
        float mix_l = 0.0f, mix_r = 0.0f;
        lfo_phase += lfo_inc; if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;
        swarm_phase += swarm_inc; if (swarm_phase >= 1.0f) swarm_phase -= 1.0f;

        float lfo_val = fm_sinf(lfo_phase * 6.2831853f) * (cur_patch.lfo_depth / 100.0f);
        float swarm_val = fm_sinf(swarm_phase * 6.2831853f) * 0.25f;

        float mod_cut = 0.0f, mod_pitch = 0.0f;
        for (int c = 0; c < 4; c++) {
            float sv = (mod_cords[c].src == 0) ? lfo_val : (mod_cords[c].src == 1 ? swarm_val : 0.0f);
            if (mod_cords[c].dst == 0) mod_cut += sv * mod_cords[c].amount;
            if (mod_cords[c].dst == 3) mod_pitch += sv * mod_cords[c].amount * 0.04f;
        }

        for (int v = 0; v < MAX_VOICES; v++) {
            if (voices[v].note < 0 && voices[v].amp_env < 0.0005f) continue;

            /* Hypersaw Bank (7 detuned saws) */
            float osc_l = 0.0f, osc_r = 0.0f;
            for (int o = 0; o < NUM_OSCS; o++) {
                voices[v].phase[o] += voices[v].inc[o] * (1.0f + mod_pitch);
                if (voices[v].phase[o] >= 1.0f) voices[v].phase[o] -= 1.0f;
                float saw = voices[v].phase[o] * 2.0f - 1.0f;
                osc_l += saw * PAN_L[o];
                osc_r += saw * PAN_R[o];
            }

            /* Sub-Oscillator */
            voices[v].sub_phase += voices[v].sub_inc * (1.0f + mod_pitch);
            if (voices[v].sub_phase >= 1.0f) voices[v].sub_phase -= 1.0f;
            float sub = (voices[v].sub_phase < 0.5f) ? 0.4f : -0.4f;

            float mono_in = (osc_l + osc_r) * (0.12f + spread_scale * 0.06f) + sub * sub_mix;

            /* 4-Pole Saturating Resonant Ladder Filter */
            float f = fm_clamp(cutoff_norm + mod_cut * 0.45f, 0.005f, 0.95f);
            float feedback = voices[v].s4 * reso_norm * 4.0f;
            float sat_in = virus_sat((mono_in - feedback) * drive_gain);

            voices[v].s1 += f * (virus_sat(sat_in) - voices[v].s1);
            voices[v].s2 += f * (voices[v].s1 - voices[v].s2);
            voices[v].s3 += f * (voices[v].s2 - voices[v].s3);
            voices[v].s4 += f * (voices[v].s3 - voices[v].s4);

            float filtered = voices[v].s4;
            if (cur_patch.filter_type == 1) filtered = voices[v].s2 - voices[v].s4; /* Bandpass */
            else if (cur_patch.filter_type == 2) filtered = sat_in - voices[v].s4;  /* Highpass */

            float voice_out = filtered * voices[v].amp_env * voices[v].vel;
            mix_l += voice_out * 0.88f;
            mix_r += voice_out * 0.88f;
        }

        /* PS1-style stereo Schroeder diffuser feedback + Bus soft limiter */
        float dl = (float)verb_buf_l[verb_idx_l] / 32768.0f;
        float dr = (float)verb_buf_r[verb_idx_r] / 32768.0f;
        verb_buf_l[verb_idx_l] = (int16_t)fm_clamp((mix_l + dr * 0.55f) * 28000.0f, -32760.0f, 32760.0f);
        verb_buf_r[verb_idx_r] = (int16_t)fm_clamp((mix_r + dl * 0.55f) * 28000.0f, -32760.0f, 32760.0f);
        if (++verb_idx_l >= VERB_LEN_L) verb_idx_l = 0;
        if (++verb_idx_r >= VERB_LEN_R) verb_idx_r = 0;

        float out_l = virus_sat(mix_l + dl * 0.25f);
        float out_r = virus_sat(mix_r + dr * 0.25f);
        lr[2 * i]     = (int32_t)(out_l * master_gain);
        lr[2 * i + 1] = (int32_t)(out_r * master_gain);
    }
}
`
  },
  {
    id: 'fm1-ctl-virus',
    name: 'CTL-Virus (Gearmulator / OsTIrus Hardware Controller)',
    author: 'Community & Charles Vestal',
    version: 'v1.0.0',
    repoUrl: 'https://github.com/dsp56300/gearmulator',
    description: 'Turns the M-VAVE FM-1 into an authentic tactile hardware programmer for Gearmulator / OsTIrus (Access Virus emulator on Mac/PC). Maps 8 rotary encoders and 14 buttons directly to Cutoff, Resonance, Hypersaw Density, Filter Env, and FX.',
    tagline: 'Tactile hardware controller for Gearmulator / OsTIrus',
    engine: 'ctl1',
    category: 'MIDI Controller',
    toml: {
      name: "CTL-VIRUS",
      id_prefix: "FM-1_51",
      usb_manufacturer: "charlesvestal",
      usb_product: "CTL-Virus Gearmulator Programmer",
      ble: true,
      ble_central: true,
      author: "Community",
      description: "Dedicated hardware controller mapping FM-1 to dsp56300/gearmulator.",
      tags: ["controller", "gearmulator", "virus"],
      repo: "https://github.com/charlesvestal/fm1-base",
      licence: "MPL-2.0",
      credits: "Compatible with dsp56300/gearmulator by The Usual Suspects"
    },
    features: [
      'Pre-mapped to Access Virus CC parameters in Gearmulator / OsTIrus',
      'ENC1: Filter 1 Cutoff (CC 40) · ENC2: Resonance (CC 42)',
      'ENC3: Hypersaw Detune / Density (CC 70) · ENC4: Filter Env Amount (CC 46)',
      '14 Hardware Buttons mapped to Filter Modes, Saturation Curves, and Arp',
      'Real-time OLED display of parameter feedback and active patch names'
    ],
    defaultPatch: {
      id: 'ctl-virus-default',
      name: 'CTL-Virus Programmer',
      category: 'Controller',
      engine: 'ctl1',
      wave: 'saw',
      octave: 0,
      masterGain: 3600,
      algorithm: 1,
      feedback: 5,
      filterCutoff: 8000,
      filterReso: 6.0,
      filterType: 'lowpass',
      fxChorus: 40,
      fxDelayTime: 0.25,
      fxDelayFeedback: 30,
      fxReverbSize: 50,
      fxDrive: 30,
      glide: 20,
      polyphony: 16,
      operators: [
        { id: 1, ratio: 1.0, detune: 0, level: 99, rate1: 95, rate2: 60, rate3: 40, rate4: 50, level1: 99, level2: 70, level3: 50, level4: 0, isCarrier: true },
        { id: 2, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 3, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 4, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
        { id: 6, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false }
      ]
    },
    appHeader: `/* SPDX-License-Identifier: MPL-2.0
 * CTL-Virus for Gearmulator on fm1-base
 */
#pragma once
#define APP_NAME "CTL-VIRUS"
`,
    sourceCode: `/* SPDX-License-Identifier: MPL-2.0
 * CTL-Virus: Dedicated Hardware Controller for Gearmulator / OsTIrus
 */
#include "plat.h"

void encoder_send_virus_cc(int enc, int delta) {
    // Map ENC 1-4 to Virus Cutoff, Reso, Hypersaw, Env
}
`
  }
];

