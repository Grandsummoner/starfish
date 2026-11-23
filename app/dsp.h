/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#include "presets.h"

#define SF_SAMPLE_RATE 44100.0f
#define SF_MAX_VOICES 8u
#define SF_SEQ_STEPS 64u
#define SF_SYNTH_MAX_STEPS 256u

enum {
    SF_ARP_UP = 0, SF_ARP_DN = 1, SF_ARP_UPDN = 2, SF_ARP_RND = 3,
    SF_ARP_RND_WALK = 4, SF_ARP_ORDER = 5,
    SF_ARP_ROT_S = 6, SF_ARP_ROT_M = 7, SF_ARP_ROT_F = 8,
    SF_NARP_MODE = 9
};
#define SF_NARP_RATE 10u

/* Matches simulator SCALE_MODE_ORDER */
enum {
    SF_SCALE_CHR = 0, SF_SCALE_MAJ = 1, SF_SCALE_MIN = 2, SF_SCALE_HMIN = 3,
    SF_SCALE_MMIN = 4, SF_SCALE_DOR = 5, SF_SCALE_PHR = 6, SF_SCALE_LYD = 7,
    SF_SCALE_MIX = 8, SF_SCALE_LOC = 9, SF_SCALE_PMAJ = 10, SF_SCALE_PMIN = 11,
    SF_NSCALE = 12
};

enum {
    SF_CHORD_OFF = 0, SF_CHORD_OCT = 1, SF_CHORD_5TH = 2, SF_CHORD_MIN7 = 3,
    SF_CHORD_MAJ7 = 4, SF_NCHORD = 5
};

/* Matches simulator RANDOM_PATCH_STYLES */
enum {
    SF_RAND_ALL = 0, SF_RAND_PAD = 1, SF_RAND_LEAD = 2, SF_RAND_BASS = 3,
    SF_RAND_PLUCK = 4, SF_RAND_ARP = 5, SF_RAND_KEYS = 6, SF_RAND_FX = 7,
    SF_NRAND_STYLE = 8
};

/* Matches simulator DRUM_BUS_MODES */
enum {
    SF_BUS_CLEAN = 0, SF_BUS_WARM = 1, SF_BUS_COMP = 2, SF_BUS_SUB = 3,
    SF_BUS_ROOM = 4, SF_BUS_SPARK = 5, SF_NBUS = 6
};

typedef struct {
    sf_preset_t patch;
    uint16_t bpm;              /* 40..240 */
    uint8_t swing;             /* 50..75 (%) */
    uint16_t seq_length;       /* 1..256 */
    uint8_t drum_kit;          /* 0..9 */
    uint8_t synth_vol;         /* 0..100 */
    int8_t patch_gain_db;      /* -12..+12 dB per-preset gain trim */
    uint8_t drum_vol;          /* 0..100 */
    uint8_t duck_amt;          /* 0..100 */
    uint8_t duck_src;          /* 0..7 drum voice index */
    uint8_t audio_out_mode;    /* 0=AUTO/HP, 1=LINE -6dB, 2=HI +3dB */
    uint8_t hp_plugged;        /* 0=unplugged, 1=headphones, 255=unknown */
    uint8_t drum_bus;          /* 0..5 */
    uint8_t scale;             /* 0..11 */
    uint8_t root;              /* 0..11 */
    uint8_t chord_mode;        /* 0..4 */
    uint8_t arp_mode;          /* 0..5 */
    uint8_t arp_rate;          /* 0..9 */
    uint8_t arp_octs;          /* 1..3 */
    uint8_t arp_latch;         /* 0..1 */
    uint8_t arp_gate;          /* 20..95 */
    uint8_t arp_density;       /* 10..100 (%) */
    uint8_t arp_accent;        /* 0..100 (%) */
    uint8_t rand_chaos;        /* 20..100 */
    uint8_t rand_style;        /* 0..7 */
    uint8_t seq_pat[SF_DRUM_VOICES][SF_SEQ_STEPS]; /* 0=off,1=hit,2=accent */
    uint8_t drum_mute;         /* bitmask 0..7 */
    int8_t drum_tune[SF_DRUM_VOICES];   /* -12..+12 st */
    uint8_t drum_decay[SF_DRUM_VOICES]; /* 25..200 (%) */
    uint8_t drum_tone[SF_DRUM_VOICES];  /* 0..100 (%) */
    uint8_t drum_level[SF_DRUM_VOICES]; /* 0..100 (%) */
} sf_engine_cfg_t;

typedef struct {
    int8_t scope[128];
    uint8_t bands[32];
    uint8_t rms_pct;
    uint8_t duck_pct;
    uint32_t note_trig_seq;
    uint32_t drum_trig_seq;
    uint8_t last_drum_idx;
} sf_dsp_vis_t;

void sf_dsp_init(sf_engine_cfg_t *cfg);
void sf_dsp_panic(void);
void sf_dsp_set_cfg(sf_engine_cfg_t *cfg);
void sf_dsp_note_on(uint8_t note, uint8_t vel);
void sf_dsp_note_off(uint8_t note);
void sf_dsp_all_notes_off(void);
void sf_dsp_aftertouch(uint8_t note, uint8_t prs);
void sf_dsp_set_pitch_bend(int16_t bend_q14);
void sf_dsp_set_mod_wheel(uint8_t val);
void sf_dsp_trigger_drum(uint8_t voice, uint8_t vel);
void sf_dsp_start_jingle(uint8_t jingle_idx, const sf_preset_t *jingle_patch);
void sf_dsp_stop_jingle(void);
void sf_dsp_click(uint8_t accent);
void sf_dsp_render(int32_t *lr, uint32_t n, uint32_t master_q12);
int32_t sf_dsp_last_note(void);
uint32_t sf_dsp_active_voices(void);
void sf_dsp_get_vis(sf_dsp_vis_t *out);
