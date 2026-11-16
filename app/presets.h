/* SPDX-License-Identifier: MPL-2.0 */
#ifndef STARFISH_PRESETS_H
#define STARFISH_PRESETS_H
#include <stdint.h>

#define SF_ROM_BANKS 9u
#define SF_ROM_COUNT 1056u
#define SF_BANK_SIZE 128u
#define SF_BANK_I_SIZE 32u
#define SF_BANK_X_SLOTS 32u
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
    char name[16];
    uint8_t cat;
    uint8_t osc_mode;
    uint8_t pw;
    int8_t osc2_semi;
    uint8_t osc2_detune;
    uint8_t osc_bal;
    uint8_t sub_vol;
    uint8_t sub_wave;
    uint8_t fm_amt;
    uint8_t noise_vol;
    uint8_t ring_vol;
    uint8_t unison;
    uint8_t spread;
    int8_t oct;
    uint8_t f1_cut;
    uint8_t f1_res;
    uint8_t f1_type;
    uint8_t f1_env;
    uint8_t f2_cut;
    uint8_t f2_type_route;  /* 0..1: f2_type, 2..3: routing, 4: sync, 5..6: sat */
    uint8_t a_atk;
    uint8_t a_dec;
    uint8_t a_sus;
    uint8_t a_rel;
    uint8_t f_atk;
    uint8_t f_dec;
    uint8_t f_sus;
    uint8_t f_rel;
    uint8_t lfo1_rate;
    uint8_t lfo1_depth;
    uint8_t lfo1_meta;      /* 0..3: dest, 4..5: shape, 6: arp */
    uint8_t lfo2_meta;      /* 0..3: dest, 4..7: rate_q */
    uint8_t fx_chorus;
    uint8_t fx_phaser;
    uint8_t fx_delay;       /* 0..3: time_q, 4..7: fb_q */
    uint8_t fx_rvb_drv;     /* 0..3: reverb_q, 4..7: drive_q */
} sf_preset_t;

static inline uint8_t sf_f2_type(const sf_preset_t *p) { return p->f2_type_route & 3u; }
static inline uint8_t sf_f_route(const sf_preset_t *p) { return (p->f2_type_route >> 2) & 3u; }
static inline uint8_t sf_osc_sync(const sf_preset_t *p) { return (p->f2_type_route >> 4) & 1u; }
static inline uint8_t sf_sat_mode(const sf_preset_t *p) { return (p->f2_type_route >> 5) & 3u; }
static inline uint8_t sf_lfo1_dest(const sf_preset_t *p) { return p->lfo1_meta & 15u; }
static inline uint8_t sf_lfo1_shape(const sf_preset_t *p) { return (p->lfo1_meta >> 4) & 3u; }
static inline uint8_t sf_arp_on(const sf_preset_t *p) { return (p->lfo1_meta >> 6) & 1u; }
static inline uint8_t sf_lfo2_dest(const sf_preset_t *p) { return p->lfo2_meta & 15u; }
static inline uint8_t sf_lfo2_rate_q(const sf_preset_t *p) { return (p->lfo2_meta >> 4) & 15u; }
static inline uint8_t sf_del_time_q(const sf_preset_t *p) { return p->fx_delay & 15u; }
static inline uint8_t sf_del_fb_q(const sf_preset_t *p) { return (p->fx_delay >> 4) & 15u; }
static inline uint8_t sf_reverb_q(const sf_preset_t *p) { return p->fx_rvb_drv & 15u; }
static inline uint8_t sf_drive_q(const sf_preset_t *p) { return (p->fx_rvb_drv >> 4) & 15u; }

#endif
