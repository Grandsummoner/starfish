/* SPDX-License-Identifier: MPL-2.0 */
#ifndef STARFISH_PRESETS_H
#define STARFISH_PRESETS_H
#include <stdint.h>

#define SF_ROM_BANKS 9u
#define SF_ROM_COUNT 1056u
#define SF_BANK_SIZE 128u
#define SF_BANK_I_SIZE 32u
#define SF_BANK_X_SLOTS 128u
#define SF_BANK_X_CHUNK 43u
#define SF_DRUM_KITS 10u
#define SF_DRUM_VOICES 8u

enum {
    SF_OSC_HYPERSAW = 0, SF_OSC_SAW = 1, SF_OSC_PULSE = 2,
    SF_OSC_TRI = 3, SF_OSC_SYNC = 4, SF_OSC_WT = 5, SF_NOSC = 6
};

enum {
    SF_FLT_LP = 0, SF_FLT_BP = 1, SF_FLT_HP = 2, SF_FLT_NOTCH = 3
};

enum {
    SF_ROUTE_SER = 0, SF_ROUTE_PAR = 1, SF_ROUTE_SPL = 2
};

enum {
    SF_SAT_SOFT = 0, SF_SAT_TUBE = 1, SF_SAT_HARD = 2, SF_SAT_OFF = 3
};

enum {
    SF_LFO_CUT = 0, SF_LFO_F2 = 1, SF_LFO_RESO = 2, SF_LFO_PITCH = 3,
    SF_LFO_PAN = 4, SF_LFO_SPREAD = 5, SF_LFO_AMP = 6, SF_LFO_CHORUS = 7,
    SF_LFO_PHASER = 8, SF_LFO_DELAY = 9, SF_LFO_REVERB = 10, SF_NLFO_DEST = 11
};

typedef struct {
    char name[16];          /* 0..15 */
    uint8_t cat;            /* 16: 0..9 */
    uint8_t osc_meta;       /* 17: 0..2 osc_mode, 3 sync, 4..5 sub_wave */
    uint8_t pw;             /* 18: 0..127 (PW or WT pos) */
    int8_t osc2_semi;       /* 19: -24..+24 */
    uint8_t osc2_detune;    /* 20: 0..127 */
    uint8_t osc_bal;        /* 21: 0..100 */
    uint8_t sub_vol;        /* 22: 0..100 */
    uint8_t fm_amt;         /* 23: 0..100 */
    uint8_t noise_vol;      /* 24: 0..100 */
    uint8_t ring_vol;       /* 25: 0..100 */
    uint8_t unison_poly;    /* 26: 0..2 (unison-1: 0..4), 3..5 (poly-1: 0..7) */
    uint8_t spread;         /* 27: 0..100 */
    uint8_t uni_pan;        /* 28: 0..100 */
    int8_t oct;             /* 29: -2..+2 */
    uint8_t f1_cut;         /* 30: 0..127 */
    uint8_t f1_res;         /* 31: 0..127 */
    uint8_t f2_cut;         /* 32: 0..127 */
    uint8_t f2_res;         /* 33: 0..127 */
    int8_t f1_env_s;        /* 34: -100..+100 */
    uint8_t f_bal;          /* 35: 0..100 */
    uint8_t f_keyfollow;    /* 36: 0..100 */
    uint8_t f2_type_route;  /* 37: 0..1 f1_type, 2..3 f2_type, 4..5 routing, 6..7 sat */
    uint8_t a_atk;          /* 38: 0..127 */
    uint8_t a_dec;          /* 39: 0..127 */
    uint8_t a_sus;          /* 40: 0..127 */
    uint8_t a_rel;          /* 41: 0..127 */
    uint8_t f_atk;          /* 42: 0..127 */
    uint8_t f_dec;          /* 43: 0..127 */
    uint8_t f_sus;          /* 44: 0..127 */
    uint8_t f_rel;          /* 45: 0..127 */
    uint8_t lfo1_rate;      /* 46: 0..127 */
    uint8_t lfo1_depth;     /* 47: 0..100 */
    uint8_t lfo1_meta;      /* 48: 0..3 dest, 4..5 shape */
    uint8_t lfo2_rate;      /* 49: 0..127 */
    uint8_t lfo2_depth;     /* 50: 0..100 */
    uint8_t lfo2_meta;      /* 51: 0..3 dest, 4..5 shape */
    uint8_t swarm_depth;    /* 52: 0..100 */
    uint8_t swarm_rate;     /* 53: 1..80 (x0.05 Hz -> 0.05..4.0Hz) */
    uint8_t glide;          /* 54: 0..85 */
    uint8_t fx_chorus;      /* 55: 0..100 */
    uint8_t fx_phaser;      /* 56: 0..100 */
    uint8_t fx_phaser_rate; /* 57: 1..60 (x0.1 Hz -> 0.1..6.0Hz) */
    uint8_t fx_del_mix;     /* 58: 0..100 */
    uint8_t fx_del_time;    /* 59: 5..100 (x0.01s -> 0.05..1.00s) */
    uint8_t fx_del_fb;      /* 60: 0..95 */
    uint8_t fx_reverb;      /* 61: 0..100 */
    uint8_t fx_drive;       /* 62: 0..100 */
    uint8_t arp_meta;       /* 63: 0..1 mode, 2..3 rate, 4..5 octs-1, 6 latch, 7 enabled */
} sf_preset_t;              /* exact 64 bytes! */

_Static_assert(sizeof(sf_preset_t) == 64u, "sf_preset_t size must remain 64B");

static inline __attribute__((always_inline)) uint8_t sf_osc_mode(const sf_preset_t *p) { return p->osc_meta & 7u; }
static inline __attribute__((always_inline)) uint8_t sf_osc_sync(const sf_preset_t *p) { return (p->osc_meta >> 3) & 1u; }
static inline __attribute__((always_inline)) uint8_t sf_sub_wave(const sf_preset_t *p) { return (p->osc_meta >> 4) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_unison(const sf_preset_t *p) { return (p->unison_poly & 7u) + 1u; }
static inline __attribute__((always_inline)) uint8_t sf_polyphony(const sf_preset_t *p) { return ((p->unison_poly >> 3) & 7u) + 1u; }

static inline __attribute__((always_inline)) uint8_t sf_f1_type(const sf_preset_t *p) { return p->f2_type_route & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_f2_type(const sf_preset_t *p) { return (p->f2_type_route >> 2) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_f_route(const sf_preset_t *p) { return (p->f2_type_route >> 4) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_sat_mode(const sf_preset_t *p) { return (p->f2_type_route >> 6) & 3u; }

static inline __attribute__((always_inline)) uint8_t sf_lfo1_dest(const sf_preset_t *p) { return p->lfo1_meta & 15u; }
static inline __attribute__((always_inline)) uint8_t sf_lfo1_shape(const sf_preset_t *p) { return (p->lfo1_meta >> 4) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_lfo2_dest(const sf_preset_t *p) { return p->lfo2_meta & 15u; }
static inline __attribute__((always_inline)) uint8_t sf_lfo2_shape(const sf_preset_t *p) { return (p->lfo2_meta >> 4) & 3u; }

static inline __attribute__((always_inline)) uint8_t sf_arp_mode(const sf_preset_t *p) { return p->arp_meta & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_rate(const sf_preset_t *p) { return (p->arp_meta >> 2) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_octs(const sf_preset_t *p) { return ((p->arp_meta >> 4) & 3u) + 1u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_latch(const sf_preset_t *p) { return (p->arp_meta >> 6) & 1u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_on(const sf_preset_t *p) { return (p->arp_meta >> 7) & 1u; }

#endif
