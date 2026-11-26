/* SPDX-License-Identifier: MPL-2.0 */
#ifndef STARFISH_PRESETS_H
#define STARFISH_PRESETS_H
#include <stdint.h>

#define SF_ROM_BANKS 9u
#define SF_ROM_COUNT 288u
#define SF_BANK_SIZE 32u
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
    uint8_t fx_del_time;    /* 59: 5..100 (0.05..1.00s) or 101..106 (BPM sync) */
    uint8_t fx_del_fb;      /* 60: 0..95 */
    uint8_t fx_reverb;      /* 61: 0..100 */
    uint8_t fx_drive;       /* 62: 0..100 */
    uint8_t arp_meta;       /* 63: 0..1 mode, 2..3 rate, 4..5 octs-1, 6 latch, 7 enabled */
} sf_preset_t;              /* exact 64 bytes! */

_Static_assert(sizeof(sf_preset_t) == 64u, "sf_preset_t size must remain 64B");

static inline __attribute__((always_inline)) uint8_t sf_cat(const sf_preset_t *p) { return p->cat & 0x0Fu; }
static inline __attribute__((always_inline)) int8_t sf_tilt_eq(const sf_preset_t *p)
{
    uint8_t idx = (uint8_t)(((p->cat >> 4) & 0x0Fu) ^ 7u);
    if (idx > 14u) idx = 14u;
    return (int8_t)((int32_t)idx - 7);
}
static inline __attribute__((always_inline)) void sf_set_tilt_eq(sf_preset_t *p, int32_t v)
{
    int32_t cl = v < -7 ? -7 : (v > 7 ? 7 : v);
    uint8_t raw = (uint8_t)(((uint32_t)(cl + 7) ^ 7u) & 0x0Fu);
    p->cat = (uint8_t)((p->cat & 0x0Fu) | (raw << 4));
}

static inline __attribute__((always_inline)) uint8_t sf_ms_width_idx(const sf_preset_t *p)
{
    uint8_t raw = (uint8_t)(((p->osc_meta >> 6) & 3u) | (((p->unison_poly >> 6) & 3u) << 2));
    return (uint8_t)((raw ^ 8u) & 0x0Fu);
}
static inline __attribute__((always_inline)) uint16_t sf_ms_width_pct(const sf_preset_t *p)
{
    static const uint8_t LUT[16] = {0u, 15u, 30u, 45u, 60u, 75u, 85u, 95u, 100u, 115u, 130u, 145u, 160u, 175u, 190u, 200u};
    return (uint16_t)LUT[sf_ms_width_idx(p) & 0x0Fu];
}
static inline __attribute__((always_inline)) void sf_set_ms_width_idx(sf_preset_t *p, int32_t idx)
{
    int32_t cl = idx < 0 ? 0 : (idx > 15 ? 15 : idx);
    uint8_t raw = (uint8_t)(((uint32_t)cl ^ 8u) & 0x0Fu);
    p->osc_meta = (uint8_t)((p->osc_meta & 0x3Fu) | ((raw & 3u) << 6));
    p->unison_poly = (uint8_t)((p->unison_poly & 0x3Fu) | (((raw >> 2) & 3u) << 6));
}

static inline __attribute__((always_inline)) uint8_t sf_punch_idx(const sf_preset_t *p)
{
    return (uint8_t)(((p->lfo1_meta >> 6) & 3u) | (((p->lfo2_meta >> 6) & 3u) << 2));
}
static inline __attribute__((always_inline)) uint8_t sf_punch_pct(const sf_preset_t *p)
{
    uint32_t raw = sf_punch_idx(p);
    return (uint8_t)((raw * 100u + 7u) / 15u);
}
static inline __attribute__((always_inline)) void sf_set_punch_idx(sf_preset_t *p, int32_t idx)
{
    int32_t cl = idx < 0 ? 0 : (idx > 15 ? 15 : idx);
    uint8_t raw = (uint8_t)((uint32_t)cl & 0x0Fu);
    p->lfo1_meta = (uint8_t)((p->lfo1_meta & 0x3Fu) | ((raw & 3u) << 6));
    p->lfo2_meta = (uint8_t)((p->lfo2_meta & 0x3Fu) | (((raw >> 2) & 3u) << 6));
}

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

static inline __attribute__((always_inline)) uint8_t sf_f_keyfollow(const sf_preset_t *p) { return p->f_keyfollow & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_f_keyfollow(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 100u ? 100u : v;
    p->f_keyfollow = (uint8_t)((p->f_keyfollow & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_f_bal(const sf_preset_t *p) { return p->f_bal & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_f_bal(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 100u ? 100u : v;
    p->f_bal = (uint8_t)((p->f_bal & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_glide(const sf_preset_t *p) { return p->glide & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_glide(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 85u ? 85u : v;
    p->glide = (uint8_t)((p->glide & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_swarm_rate(const sf_preset_t *p)
{
    uint8_t v = p->swarm_rate & 0x7Fu;
    return v < 1u ? 1u : (v > 80u ? 80u : v);
}
static inline __attribute__((always_inline)) void sf_set_swarm_rate(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v < 1u ? 1u : (v > 80u ? 80u : v);
    p->swarm_rate = (uint8_t)((p->swarm_rate & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_fx_del_fb(const sf_preset_t *p) { return p->fx_del_fb & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_fx_del_fb(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 95u ? 95u : v;
    p->fx_del_fb = (uint8_t)((p->fx_del_fb & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_fx_phaser_rate(const sf_preset_t *p)
{
    uint8_t v = p->fx_phaser_rate & 0x3Fu;
    return v < 1u ? 1u : (v > 60u ? 60u : v);
}
static inline __attribute__((always_inline)) void sf_set_fx_phaser_rate(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v < 1u ? 1u : (v > 60u ? 60u : v);
    p->fx_phaser_rate = (uint8_t)((p->fx_phaser_rate & 0xC0u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_sub_vol(const sf_preset_t *p) { return p->sub_vol & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_sub_vol(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 100u ? 100u : v;
    p->sub_vol = (uint8_t)((p->sub_vol & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_fm_amt(const sf_preset_t *p) { return p->fm_amt & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_fm_amt(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 100u ? 100u : v;
    p->fm_amt = (uint8_t)((p->fm_amt & 0x80u) | cl);
}

static inline __attribute__((always_inline)) uint8_t sf_noise_vol(const sf_preset_t *p) { return p->noise_vol & 0x7Fu; }
static inline __attribute__((always_inline)) void sf_set_noise_vol(sf_preset_t *p, uint8_t v)
{
    uint8_t cl = v > 100u ? 100u : v;
    p->noise_vol = (uint8_t)((p->noise_vol & 0x80u) | cl);
}

/* Packed 5-bit Filter 2 Envelope depth (-100..+100% step 10%; ROM 0-bits -> idx 10 -> 0%) */
static inline __attribute__((always_inline)) int8_t sf_f2_env(const sf_preset_t *p)
{
    uint8_t raw = (uint8_t)(((p->f_keyfollow >> 7) & 1u)
                          | (((p->f_bal >> 7) & 1u) << 1)
                          | (((p->glide >> 7) & 1u) << 2)
                          | (((p->swarm_rate >> 7) & 1u) << 3)
                          | (((p->fx_del_fb >> 7) & 1u) << 4));
    uint8_t idx = (uint8_t)((raw ^ 10u) & 0x1Fu);
    if (idx > 20u) idx = 20u;
    return (int8_t)(((int32_t)idx - 10) * 10);
}
static inline __attribute__((always_inline)) void sf_set_f2_env(sf_preset_t *p, int32_t val)
{
    int32_t cl = val < -100 ? -100 : (val > 100 ? 100 : val);
    uint8_t idx = (uint8_t)((cl + 105) / 10);
    if (idx > 20u) idx = 20u;
    uint8_t raw = (uint8_t)((idx ^ 10u) & 0x1Fu);
    p->f_keyfollow = (uint8_t)((p->f_keyfollow & 0x7Fu) | ((raw & 1u) << 7));
    p->f_bal       = (uint8_t)((p->f_bal & 0x7Fu)       | (((raw >> 1) & 1u) << 7));
    p->glide       = (uint8_t)((p->glide & 0x7Fu)       | (((raw >> 2) & 1u) << 7));
    p->swarm_rate  = (uint8_t)((p->swarm_rate & 0x7Fu)  | (((raw >> 3) & 1u) << 7));
    p->fx_del_fb   = (uint8_t)((p->fx_del_fb & 0x7Fu)   | (((raw >> 4) & 1u) << 7));
}

/* Packed 5-bit Velocity Sensitivity (0..100% step 5%; ROM 0-bits -> idx 10 -> 50%) */
static inline __attribute__((always_inline)) uint8_t sf_vel_sens(const sf_preset_t *p)
{
    uint8_t raw = (uint8_t)(((p->fx_phaser_rate >> 6) & 3u)
                          | (((p->sub_vol >> 7) & 1u) << 2)
                          | (((p->fm_amt >> 7) & 1u) << 3)
                          | (((p->noise_vol >> 7) & 1u) << 4));
    uint8_t idx = (uint8_t)((raw ^ 10u) & 0x1Fu);
    if (idx > 20u) idx = 20u;
    return (uint8_t)(idx * 5u);
}
static inline __attribute__((always_inline)) void sf_set_vel_sens(sf_preset_t *p, int32_t val)
{
    int32_t cl = val < 0 ? 0 : (val > 100 ? 100 : val);
    uint8_t idx = (uint8_t)((cl + 2) / 5);
    if (idx > 20u) idx = 20u;
    uint8_t raw = (uint8_t)((idx ^ 10u) & 0x1Fu);
    p->fx_phaser_rate = (uint8_t)((p->fx_phaser_rate & 0x3Fu) | ((raw & 3u) << 6));
    p->sub_vol        = (uint8_t)((p->sub_vol & 0x7Fu)        | (((raw >> 2) & 1u) << 7));
    p->fm_amt         = (uint8_t)((p->fm_amt & 0x7Fu)         | (((raw >> 3) & 1u) << 7));
    p->noise_vol      = (uint8_t)((p->noise_vol & 0x7Fu)      | (((raw >> 4) & 1u) << 7));
}

static inline __attribute__((always_inline)) uint8_t sf_arp_mode(const sf_preset_t *p) { return p->arp_meta & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_rate(const sf_preset_t *p) { return (p->arp_meta >> 2) & 3u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_octs(const sf_preset_t *p) { return ((p->arp_meta >> 4) & 3u) + 1u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_latch(const sf_preset_t *p) { return (p->arp_meta >> 6) & 1u; }
static inline __attribute__((always_inline)) uint8_t sf_arp_on(const sf_preset_t *p) { return (p->arp_meta >> 7) & 1u; }

#endif
