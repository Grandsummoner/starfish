/* SPDX-License-Identifier: MPL-2.0 */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "dsp.h"

#define INV_SR (1.0f / 44100.0f)

/* ------------------------------------------- Inline & SRAM Math Helpers --- */
/* Kept inside .app_ram or always_inline so sf_dsp_render never jumps to XIP */

static inline __attribute__((always_inline)) float sf_fabs(float x)
{
    fm_bits_t b;
    b.f = x;
    b.u &= 0x7FFFFFFFu;
    return b.f;
}

static inline __attribute__((always_inline)) float sf_clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline __attribute__((always_inline)) float sf_floor(float x)
{
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}

static PLAT_RAM_CODE float sf_ram_exp2_nf(int32_t n, float f)
{
    fm_bits_t b;
    float p;
    if (n < -126) return 0.0f;
    p = 0.00015345809f;
    p = p * f + 0.0013399932f;
    p = p * f + 0.0096184890f;
    p = p * f + 0.055503288f;
    p = p * f + 0.24022647f;
    p = p * f + 0.69314721f;
    p = p * f + 1.0f;
    b.f = p;
    b.i += n << 23;
    return b.f;
}

static PLAT_RAM_CODE float sf_ram_exp2f(float x)
{
    float fl;
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    fl = sf_floor(x + 0.5f);
    return sf_ram_exp2_nf((int32_t)fl, x - fl);
}

static PLAT_RAM_CODE float sf_ram_expf(float x)
{
    float n;
    if (x < -87.3f) return 0.0f;
    if (x > 88.7f) x = 88.7f;
    n = sf_floor(x * FM_LOG2E + 0.5f);
    x = (x - n * 0.693145752f) - n * 0.0000014286068f;
    return sf_ram_exp2_nf((int32_t)n, x * FM_LOG2E);
}

static inline __attribute__((always_inline)) float sf_sin_turns_core(float ph)
{
    float x = (ph - sf_floor(ph + 0.5f)) * FM_TWO_PI;
    float x2 = x * x, p;
    p = 0.0000027529467f;
    p = p * x2 - 0.00019840156f;
    p = p * x2 + 0.0083333104f;
    p = p * x2 - 0.16666665f;
    p = p * x2 + 0.99999999f;
    return p * x;
}

static PLAT_RAM_CODE float sf_ram_sin_turns(float ph)
{
    return sf_sin_turns_core(ph);
}

static PLAT_RAM_CODE float sf_ram_sqrtf(float x)
{
    fm_bits_t b;
    float y;
    if (x <= 0.0f) return 0.0f;
    b.f = x;
    b.u = 0x5F375A86u - (b.u >> 1);
    y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

/* Smooth C1 cubic soft clipper: zero divides, zero exp2 */
static inline __attribute__((always_inline)) float sf_soft_clip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x * (1.0f - 0.14814815f * x * x);
}

/* Smooth C1 rational tanh saturator for final output limiting */
static inline __attribute__((always_inline)) float sf_fast_tanh(float x)
{
    float x2;
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    x2 = x * x;
    return (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
}

static uint32_t rng_state = 0x13572468u;
static inline __attribute__((always_inline)) float fast_noise(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return (float)((int32_t)x >> 8) * (1.0f / 8388608.0f);
}

static inline __attribute__((always_inline)) float polyblep(float t, float dt)
{
    if (t < dt) {
        float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

/* Zero int/float conversion phase wrap for dt in (-0.5, 0.5) */
static inline __attribute__((always_inline)) float fast_wrap01(float x)
{
    if (x >= 1.0f) x -= 1.0f;
    if (x >= 1.0f) x -= 1.0f;
    if (x < 0.0f) x += 1.0f;
    return x;
}

static inline __attribute__((always_inline)) float sf_flush(float x)
{
    fm_bits_t b;
    b.f = x;
    return (b.u & 0x7F800000u) == 0u ? 0.0f : x;
}

static inline __attribute__((always_inline)) float byte_to_hz(uint8_t b)
{
    float norm = (float)b * (1.0f / 127.0f);
    return sf_clamp(40.0f * sf_ram_exp2f(norm * 8.287712f), 40.0f, 12500.0f);
}

static inline __attribute__((always_inline)) float byte_to_reso_q(uint8_t b)
{
    float norm = (float)b * (1.0f / 127.0f);
    return 0.7f + norm * sf_ram_sqrtf(norm) * 15.5f;
}

static inline __attribute__((always_inline)) float byte_to_env_sec(uint8_t b, float max_s, float min_s)
{
    float norm = (float)b * (1.0f / 127.0f);
    return min_s + norm * norm * max_s;
}

/* --------------------------------------- Precomputed-Coeff SVF Filter --- */
typedef struct {
    float ic1, ic2;
} svf_t;

typedef struct {
    float a1, a2, a3, k;
    uint8_t type;
} svf_c_t;

static inline __attribute__((always_inline)) void svf_prep(svf_c_t *c, float g, float k, uint8_t type)
{
    float a1 = 1.0f / (1.0f + g * (g + k));
    c->a1 = a1;
    c->a2 = g * a1;
    c->a3 = g * c->a2;
    c->k = k;
    c->type = type;
}

static inline __attribute__((always_inline)) float svf_tick_c(svf_t *s, float in, const svf_c_t *c)
{
    float v3 = in - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = sf_flush(2.0f * v1 - s->ic1);
    s->ic2 = sf_flush(2.0f * v2 - s->ic2);
    if (c->type == SF_FLT_BP) return v1 * (0.85f + 0.75f * c->k);
    if (c->type == SF_FLT_HP) return in - c->k * v1 - v2;
    if (c->type == SF_FLT_NOTCH) return in - c->k * v1;
    return v2;
}

/* ----------------------------------------------------------- VA voices --- */
enum { ENV_OFF = 0, ENV_ATK, ENV_DEC, ENV_SUS, ENV_REL };

typedef struct {
    uint8_t active;
    uint8_t gate;
    int8_t note;
    uint8_t vel;
    uint8_t prs;
    uint8_t is_jingle;
    uint8_t amp_st;
    uint8_t flt_st;
    uint8_t click_rem;
    uint32_t age;
    float phase1;
    float phase2;
    float phase_sub;
    float hyper_ph[4];
    float amp_env;
    float flt_env;
    float pitch_cur;
    float vel_g_cur;
    float punch_env;
    float last_l, last_r;
    float click_l, click_r;
    svf_t f1l;
    svf_t f2l, f2r;
} va_voice_t;

/* ----------------------------------------------------------- Drum voices --- */
typedef struct {
    uint8_t active;
    uint8_t click_rem;
    float ph1, ph2;
    float env, pitch_env, atk_env;
    float last_s, click_s;
    float level;
    float tone, tune, decay;
    svf_t bp;
} drum_voice_t;

/* ----------------------------------------------------------- FX pools --- */
#define DEL_LEN 16384u
#define CHOR_LEN 1024u
#define REV_C0 1553u
#define REV_C1 1617u
#define REV_C2 1491u
#define REV_C3 1422u
#define REV_A0 225u
#define REV_A1 556u
#define REV_PRE 960u

static int16_t del_buf[DEL_LEN * 2u] PLAT_POOL;
static int16_t chor_l[CHOR_LEN] PLAT_POOL;
static int16_t chor_r[CHOR_LEN] PLAT_POOL;
static int16_t rev_c0[REV_C0] PLAT_POOL;
static int16_t rev_c1[REV_C1] PLAT_POOL;
static int16_t rev_c2[REV_C2] PLAT_POOL;
static int16_t rev_c3[REV_C3] PLAT_POOL;
static int16_t rev_a0[REV_A0] PLAT_POOL;
static int16_t rev_a1[REV_A1] PLAT_POOL;
static int16_t rev_pre[REV_PRE] PLAT_POOL;
static float wt_bank[8][64] PLAT_POOL;

static const uint8_t J_NOTES[6][5] = {
    {60u, 64u, 67u, 71u, 74u},
    {62u, 67u, 69u, 74u, 79u},
    {64u, 67u, 69u, 71u, 76u},
    {60u, 67u, 65u, 64u, 72u},
    {57u, 64u, 69u, 71u, 76u},
    {60u, 62u, 64u, 67u, 72u},
};
static const uint16_t J_STEP_SAMP[6] = {5733u, 5512u, 5292u, 5512u, 5953u, 4851u};
static const uint16_t J_HOLD_SAMP[6] = {5045u, 4851u, 4657u, 4851u, 5239u, 4269u};
static const uint16_t J_LAST_SAMP[6] = {13759u, 13229u, 12701u, 13229u, 14287u, 11642u};

static struct {
    sf_engine_cfg_t *cfg;
    va_voice_t voices[SF_MAX_VOICES];
    drum_voice_t drums[SF_DRUM_VOICES];
    uint32_t voice_age;
    int32_t last_note;
    float last_pitch_note;
    float lfo1_ph, lfo2_ph, swarm_ph;
    float chor_ph, phas_ph;
    float phas_st[4][2];
    uint32_t del_wr;
    uint32_t chor_wr;
    uint32_t rev_ci[4];
    uint32_t rev_ai[2];
    uint32_t rev_pre_i;
    float master_now;
    float del_tap_cur;
    float del_lp_l, del_lp_r;
    float del_hpf_xl, del_hpf_yl, del_hpf_xr, del_hpf_yr;
    float del_in_prev_l, del_in_prev_r;
    float del_out_prev_l, del_out_prev_r;
    float del_out_cur_l, del_out_cur_r;
    uint8_t del_tick;
    float rev_clp[4];
    float rev_hpf_x[4], rev_hpf_y[4];
    float rev_mod_ph;
    float phas_energy, chor_energy, del_energy, rev_energy;
    uint8_t chor_cleared;
    float dc_xl, dc_yl, dc_xr, dc_yr;
    float click_amp, click_ph, click_inc;
    float duck_env;
    uint16_t duck_atk_rem;
    uint8_t duck_trig;
    uint32_t del_clr_rem;
    uint8_t rev_cleared;
    uint8_t jingle_active;
    uint8_t jingle_idx;
    uint8_t jingle_step;
    uint32_t jingle_sample_pos;
    uint32_t jingle_rel_at[5];
    uint8_t jingle_rel_done[5];
    sf_preset_t jingle_patch;
    sf_dsp_vis_t vis;
    float vis_bands_f[32];
    float vis_rms_f;
    float pitch_bend_targ, pitch_bend_cur;
    float mod_wheel_targ, mod_wheel_cur;
    float s_lvl_cur;
    float tilt_lp_l, tilt_lp_r;
} dsp;

static void init_wavetable_lut(void)
{
    uint32_t f, s;
    for (f = 0u; f < 8u; f++) {
        float peak = 0.001f;
        for (s = 0u; s < 64u; s++) {
            float t = (float)s * (1.0f / 64.0f);
            float v = 0.0f;
            if (f == 0u) {
                v = sf_sin_turns_core(t);
            } else if (f == 1u) {
                v = sf_sin_turns_core(t) + 0.38f * sf_sin_turns_core(fast_wrap01(t * 3.0f))
                  + 0.16f * sf_sin_turns_core(fast_wrap01(t * 5.0f));
            } else if (f == 2u) {
                v = 0.72f * sf_sin_turns_core(t) + 0.52f * sf_sin_turns_core(fast_wrap01(t * 2.0f))
                  + 0.36f * sf_sin_turns_core(fast_wrap01(t * 3.0f))
                  + 0.22f * sf_sin_turns_core(fast_wrap01(t * 4.0f));
            } else if (f == 3u) {
                v = 0.58f * sf_sin_turns_core(t) + 0.52f * sf_sin_turns_core(fast_wrap01(t * 3.0f))
                  + 0.40f * sf_sin_turns_core(fast_wrap01(t * 5.0f))
                  + 0.24f * sf_sin_turns_core(fast_wrap01(t * 7.0f));
            } else if (f == 4u) {
                v = 0.55f * sf_sin_turns_core(t) + 0.45f * sf_sin_turns_core(fast_wrap01(t * 2.0f))
                  + 0.38f * sf_sin_turns_core(fast_wrap01(t * 5.0f))
                  + 0.28f * sf_sin_turns_core(fast_wrap01(t * 7.0f));
            } else if (f == 5u) {
                float h;
                uint32_t k;
                for (k = 1u; k <= 8u; k++) {
                    h = (float)k;
                    v += sf_sin_turns_core(fast_wrap01(t * h)) / h;
                }
            } else if (f == 6u) {
                v = 0.38f * sf_sin_turns_core(t) + 0.56f * sf_sin_turns_core(fast_wrap01(t * 4.0f))
                  + 0.48f * sf_sin_turns_core(fast_wrap01(t * 6.0f))
                  + 0.28f * sf_sin_turns_core(fast_wrap01(t * 9.0f));
            } else {
                v = 0.35f * sf_sin_turns_core(t) + 0.42f * sf_sin_turns_core(fast_wrap01(t * 5.0f))
                  + 0.38f * sf_sin_turns_core(fast_wrap01(t * 8.0f))
                  + 0.32f * sf_sin_turns_core(fast_wrap01(t * 11.0f));
            }
            wt_bank[f][s] = v;
            if (sf_fabs(v) > peak) peak = sf_fabs(v);
        }
        float inv = 0.94f / peak;
        for (s = 0u; s < 64u; s++)
            wt_bank[f][s] *= inv;
    }
}

void sf_dsp_init(sf_engine_cfg_t *cfg)
{
    uint32_t i;
    dsp.cfg = cfg;
    dsp.last_note = -1;
    dsp.last_pitch_note = 60.0f;
    dsp.voice_age = 1u;
    dsp.master_now = 0.0f;
    dsp.del_tap_cur = 3200.0f;
    dsp.del_lp_l = 0.0f;
    dsp.del_lp_r = 0.0f;
    dsp.del_hpf_xl = dsp.del_hpf_yl = dsp.del_hpf_xr = dsp.del_hpf_yr = 0.0f;
    dsp.del_in_prev_l = dsp.del_in_prev_r = 0.0f;
    dsp.del_out_prev_l = dsp.del_out_prev_r = 0.0f;
    dsp.del_out_cur_l = dsp.del_out_cur_r = 0.0f;
    dsp.del_tick = 0u;
    dsp.rev_pre_i = 0u;
    dsp.rev_mod_ph = 0.0f;
    dsp.phas_energy = dsp.chor_energy = dsp.del_energy = dsp.rev_energy = 0.0f;
    dsp.chor_cleared = 1u;
    for (i = 0u; i < 4u; i++) {
        dsp.rev_clp[i] = 0.0f;
        dsp.rev_hpf_x[i] = dsp.rev_hpf_y[i] = 0.0f;
    }
    dsp.dc_xl = dsp.dc_yl = dsp.dc_xr = dsp.dc_yr = 0.0f;
    dsp.click_amp = 0.0f;
    dsp.click_ph = 0.0f;
    dsp.click_inc = 1175.0f * INV_SR;
    dsp.duck_env = 0.0f;
    dsp.duck_atk_rem = 0u;
    dsp.duck_trig = 0u;
    dsp.vis.duck_pct = 0u;
    dsp.vis.mod_cut1 = 86u;
    dsp.vis.mod_cut2 = 112u;
    dsp.vis.mod_res1_pct = 20u;
    dsp.vis.amp_env_pct = 0u;
    dsp.vis.amp_st = ENV_OFF;
    dsp.vis.flt_env_pct = 0u;
    dsp.vis.flt_st = ENV_OFF;
    dsp.vis.del_lvl_pct = 0u;
    dsp.vis.rev_lvl_pct = 0u;
    dsp.del_clr_rem = 0u;
    dsp.rev_cleared = 1u;
    dsp.jingle_active = 0u;
    init_wavetable_lut();
    for (i = 0; i < SF_MAX_VOICES; i++) {
        dsp.voices[i].active = 0u;
        dsp.voices[i].click_rem = 0u;
        dsp.voices[i].vel_g_cur = -1.0f;
        dsp.voices[i].last_l = dsp.voices[i].last_r = 0.0f;
    }
    for (i = 0; i < SF_DRUM_VOICES; i++) {
        dsp.drums[i].active = 0u;
        dsp.drums[i].click_rem = 0u;
        dsp.drums[i].last_s = 0.0f;
        dsp.drums[i].atk_env = 0.0f;
    }
    for (i = 0; i < 32u; i++) {
        dsp.vis.bands[i] = 0u;
        dsp.vis_bands_f[i] = 0.0f;
    }
    for (i = 0; i < 128u; i++)
        dsp.vis.scope[i] = 0;
    dsp.pitch_bend_targ = dsp.pitch_bend_cur = 0.0f;
    dsp.mod_wheel_targ = dsp.mod_wheel_cur = 0.0f;
    dsp.s_lvl_cur = -1.0f;
    dsp.tilt_lp_l = dsp.tilt_lp_r = 0.0f;
}

void sf_dsp_set_cfg(sf_engine_cfg_t *cfg)
{
    dsp.cfg = cfg;
}

void sf_dsp_set_pitch_bend(int16_t bend_q14)
{
    float st = sf_clamp((float)bend_q14 * (2.0f / 8192.0f), -2.0f, 2.0f);
    uint32_t f = plat_lock();
    dsp.pitch_bend_targ = st;
    plat_unlock(f);
}

void sf_dsp_set_mod_wheel(uint8_t val)
{
    float mw = sf_clamp((float)(val & 127u) * (1.0f / 127.0f), 0.0f, 1.0f);
    uint32_t f = plat_lock();
    dsp.mod_wheel_targ = mw;
    plat_unlock(f);
}

void sf_dsp_click(uint8_t accent)
{
    uint32_t f = plat_lock();
    dsp.click_ph = 0.0f;
    dsp.click_inc = (accent ? 1760.0f : 1175.0f) * INV_SR;
    dsp.click_amp = accent ? 0.52f : 0.36f;
    plat_unlock(f);
}

void sf_dsp_stop_jingle(void)
{
    uint32_t i;
    uint32_t f = plat_lock();
    if (dsp.jingle_active) {
        dsp.jingle_active = 0u;
        for (i = 0; i < SF_MAX_VOICES; i++) {
            if (dsp.voices[i].active && dsp.voices[i].is_jingle) {
                dsp.voices[i].gate = 0u;
                dsp.voices[i].amp_st = ENV_REL;
                dsp.voices[i].flt_st = ENV_REL;
            }
        }
    }
    plat_unlock(f);
}

void sf_dsp_panic(void)
{
    uint32_t i;
    uint32_t f = plat_lock();
    dsp.jingle_active = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        dsp.voices[i].active = 0u;
        dsp.voices[i].gate = 0u;
        dsp.voices[i].amp_env = 0.0f;
        dsp.voices[i].amp_st = ENV_OFF;
        dsp.voices[i].flt_env = 0.0f;
        dsp.voices[i].flt_st = ENV_OFF;
        dsp.voices[i].click_rem = 0u;
        dsp.voices[i].vel_g_cur = -1.0f;
        dsp.voices[i].last_l = dsp.voices[i].last_r = 0.0f;
    }
    for (i = 0; i < SF_DRUM_VOICES; i++) {
        dsp.drums[i].active = 0u;
        dsp.drums[i].click_rem = 0u;
        dsp.drums[i].last_s = 0.0f;
    }
    dsp.last_note = -1;
    dsp.duck_env = 0.0f;
    dsp.duck_atk_rem = 0u;
    dsp.duck_trig = 0u;
    dsp.vis.duck_pct = 0u;
    dsp.vis.amp_env_pct = 0u;
    dsp.vis.amp_st = ENV_OFF;
    dsp.vis.flt_env_pct = 0u;
    dsp.vis.flt_st = ENV_OFF;
    dsp.vis.del_lvl_pct = 0u;
    dsp.vis.rev_lvl_pct = 0u;
    dsp.pitch_bend_targ = dsp.pitch_bend_cur = 0.0f;
    dsp.mod_wheel_targ = dsp.mod_wheel_cur = 0.0f;
    dsp.tilt_lp_l = dsp.tilt_lp_r = 0.0f;
    plat_unlock(f);
}

void sf_dsp_start_jingle(uint8_t jingle_idx, const sf_preset_t *jp)
{
    uint32_t i;
    uint32_t f = plat_lock();
    dsp.jingle_idx = (uint8_t)(jingle_idx % 6u);
    dsp.jingle_step = 0u;
    dsp.jingle_sample_pos = 0u;
    for (i = 0; i < 5u; i++) {
        dsp.jingle_rel_at[i] = 0u;
        dsp.jingle_rel_done[i] = 0u;
    }
    if (jp) {
        const uint8_t *src = (const uint8_t *)jp;
        uint8_t *dst = (uint8_t *)&dsp.jingle_patch;
        for (i = 0; i < (uint32_t)sizeof(sf_preset_t); i++)
            dst[i] = src[i];
        if (dsp.jingle_patch.a_atk > 16u) dsp.jingle_patch.a_atk = 10u;
        if (dsp.jingle_patch.a_dec > 44u) dsp.jingle_patch.a_dec = 38u;
        if (dsp.jingle_patch.a_sus < 45u) dsp.jingle_patch.a_sus = 68u;
        if (dsp.jingle_patch.a_rel > 42u) dsp.jingle_patch.a_rel = 36u;
        if (dsp.jingle_patch.f_atk > 16u) dsp.jingle_patch.f_atk = 10u;
        if (sf_f1_type(&dsp.jingle_patch) == SF_FLT_LP && dsp.jingle_patch.f1_cut < 72u)
            dsp.jingle_patch.f1_cut = 78u;
        dsp.jingle_patch.glide = 0u;
    }
    dsp.jingle_active = 1u;
    plat_unlock(f);
}

/* ---------------------------------------------- RAM Voice Allocation --- */
static PLAT_RAM_CODE uint32_t alloc_voice(int8_t note, uint8_t max_poly)
{
    uint32_t i, best = 0u, active_cnt = 0u;
    uint32_t best_rel = 0xFFFFFFFFu;
    float min_rel_env = 10.0f;
    float min_act_env = 10.0f;
    uint32_t best_act = 0u;

    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].note == note)
            return i;
        if (dsp.voices[i].active)
            active_cnt++;
    }
    if (active_cnt < max_poly) {
        for (i = 0; i < SF_MAX_VOICES; i++) {
            if (!dsp.voices[i].active)
                return i;
        }
    }
    /* Steal quietest released voice first, then quietest active voice */
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (!dsp.voices[i].active)
            continue;
        if (!dsp.voices[i].gate) {
            if (dsp.voices[i].amp_env < min_rel_env) {
                min_rel_env = dsp.voices[i].amp_env;
                best_rel = i;
            }
        }
        if (dsp.voices[i].amp_env < min_act_env) {
            min_act_env = dsp.voices[i].amp_env;
            best_act = i;
        }
    }
    if (best_rel != 0xFFFFFFFFu)
        return best_rel;
    best = best_act;
    return best;
}

static PLAT_RAM_CODE void internal_note_off(uint8_t note)
{
    uint32_t i;
    int32_t any = -1;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].note == (int8_t)note && dsp.voices[i].gate) {
            dsp.voices[i].gate = 0u;
            dsp.voices[i].amp_st = ENV_REL;
            dsp.voices[i].flt_st = ENV_REL;
        }
        if (dsp.voices[i].active && dsp.voices[i].gate && !dsp.voices[i].is_jingle)
            any = (int32_t)dsp.voices[i].note;
    }
    dsp.last_note = any;
}

static PLAT_RAM_CODE void internal_note_on(uint8_t note, uint8_t vel, uint8_t is_jingle)
{
    uint32_t idx;
    va_voice_t *v;
    uint8_t max_poly = dsp.cfg ? sf_polyphony(&dsp.cfg->patch) : SF_MAX_VOICES;
    if (max_poly < 1u) max_poly = 1u;
    if (max_poly > SF_MAX_VOICES) max_poly = SF_MAX_VOICES;
    if (is_jingle) {
        max_poly = SF_MAX_VOICES;
    } else {
        uint8_t active_drums = 0u, di;
        for (di = 0u; di < SF_DRUM_VOICES; di++) {
            if (dsp.drums[di].active) active_drums++;
        }
        uint8_t heavy_fx = (dsp.rev_energy > 0.01f || dsp.del_energy > 0.01f || (dsp.cfg && dsp.cfg->patch.fx_reverb > 25u)) ? 1u : 0u;
        if (active_drums >= 3u && heavy_fx && max_poly > 5u) max_poly = 5u;
        else if (active_drums >= 2u && max_poly > 6u) max_poly = 6u;
    }

    if (!vel) {
        internal_note_off(note);
        return;
    }
    idx = alloc_voice((int8_t)note, max_poly);
    v = &dsp.voices[idx];
    if (v->active && v->note != (int8_t)note) {
        /* Clickless 1.45ms tail crossfade when stealing an active voice */
        v->click_l = v->last_l;
        v->click_r = v->last_r;
        v->click_rem = 64u;
        v->vel_g_cur = -1.0f;
        v->amp_env = 0.0f;
        v->flt_env = 0.0f;
        v->f1l.ic1 = v->f1l.ic2 = 0.0f;
        v->f2l.ic1 = v->f2l.ic2 = 0.0f;
        v->f2r.ic1 = v->f2r.ic2 = 0.0f;
    } else if (!v->active) {
        v->click_rem = 0u;
        v->vel_g_cur = -1.0f;
        v->last_l = v->last_r = 0.0f;
        v->amp_env = 0.0f;
        v->flt_env = 0.0f;
        v->f1l.ic1 = v->f1l.ic2 = 0.0f;
        v->f2l.ic1 = v->f2l.ic2 = 0.0f;
        v->f2r.ic1 = v->f2r.ic2 = 0.0f;
    }
    if (!v->active || v->note != (int8_t)note) {
        v->phase1 = 0.0f;
        v->phase2 = 0.25f;
        v->phase_sub = 0.0f;
        v->hyper_ph[0] = 0.13f;
        v->hyper_ph[1] = 0.37f;
        v->hyper_ph[2] = 0.61f;
        v->hyper_ph[3] = 0.83f;
        v->pitch_cur = (dsp.cfg && dsp.cfg->patch.glide > 0u && !is_jingle) ? dsp.last_pitch_note : (float)note;
    }
    v->active = 1u;
    v->gate = 1u;
    v->is_jingle = is_jingle;
    v->note = (int8_t)note;
    v->vel = vel;
    v->prs = 0u;
    v->age = ++dsp.voice_age;
    v->amp_st = ENV_ATK;
    v->flt_st = ENV_ATK;
    v->punch_env = 1.0f;
    dsp.last_pitch_note = (float)note;
    if (!is_jingle)
        dsp.last_note = (int32_t)note;
    dsp.vis.note_trig_seq++;
}

void sf_dsp_note_on(uint8_t note, uint8_t vel)
{
    uint32_t f = plat_lock();
    PLAT_FAR(internal_note_on)(note, vel, 0u);
    plat_unlock(f);
}

void sf_dsp_note_off(uint8_t note)
{
    uint32_t f = plat_lock();
    PLAT_FAR(internal_note_off)(note);
    plat_unlock(f);
}

void sf_dsp_all_notes_off(void)
{
    uint32_t i;
    uint32_t f = plat_lock();
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].gate && !dsp.voices[i].is_jingle) {
            dsp.voices[i].gate = 0u;
            dsp.voices[i].amp_st = ENV_REL;
            dsp.voices[i].flt_st = ENV_REL;
        }
    }
    dsp.last_note = -1;
    plat_unlock(f);
}

void sf_dsp_aftertouch(uint8_t note, uint8_t prs)
{
    uint32_t i;
    uint32_t f = plat_lock();
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].note == (int8_t)note)
            dsp.voices[i].prs = prs;
    }
    plat_unlock(f);
}

void sf_dsp_trigger_drum(uint8_t voice, uint8_t vel)
{
    static const float TUNE_LUT[25] = {
        0.50000f, 0.52973f, 0.56123f, 0.59460f, 0.62996f, 0.66742f, 0.70711f, 0.74915f,
        0.79370f, 0.84090f, 0.89090f, 0.94387f, 1.00000f, 1.05946f, 1.12246f, 1.18921f,
        1.25992f, 1.33484f, 1.41421f, 1.49831f, 1.58740f, 1.68179f, 1.78180f, 1.88775f, 2.00000f
    };
    drum_voice_t *d;
    float tnorm = 1.0f, dnorm = 1.0f, tonen = 0.6f, lnorm = 0.9f;
    uint32_t f;
    if (voice >= SF_DRUM_VOICES)
        return;
    if (dsp.cfg && ((dsp.cfg->drum_mute >> voice) & 1u))
        return;
    if (dsp.cfg) {
        int32_t ti = (int32_t)dsp.cfg->drum_tune[voice] + 12;
        if (ti < 0) ti = 0;
        if (ti > 24) ti = 24;
        tnorm = TUNE_LUT[ti];
        dnorm = sf_clamp((float)dsp.cfg->drum_decay[voice] * 0.01f, 0.25f, 2.0f);
        tonen = sf_clamp((float)dsp.cfg->drum_tone[voice] * 0.01f, 0.0f, 1.0f);
        lnorm = sf_clamp((float)dsp.cfg->drum_level[voice] * 0.01f, 0.0f, 1.0f);
    }
    f = plat_lock();
    d = &dsp.drums[voice];
    if (d->active) {
        d->click_s = d->last_s;
        d->click_rem = 32u;
    } else {
        d->click_rem = 0u;
        d->last_s = 0.0f;
    }
    d->active = 1u;
    d->ph1 = 0.0f;
    d->ph2 = 0.0f;
    d->env = 1.0f;
    d->pitch_env = 1.0f;
    d->atk_env = 0.0f;
    d->level = ((float)vel * (1.0f / 127.0f)) * lnorm;
    d->tune = tnorm;
    d->decay = dnorm;
    d->tone = tonen;
    d->bp.ic1 = d->bp.ic2 = 0.0f;
    if (!dsp.cfg || voice == dsp.cfg->duck_src)
        dsp.duck_trig = 1u;
    dsp.vis.last_drum_idx = voice;
    dsp.vis.drum_trig_seq++;
    plat_unlock(f);
}

int32_t sf_dsp_last_note(void) { return dsp.last_note; }

uint32_t sf_dsp_active_voices(void)
{
    uint32_t i, cnt = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++)
        if (dsp.voices[i].active && (dsp.voices[i].gate || dsp.voices[i].amp_env > 0.015f))
            cnt++;
    for (i = 0; i < SF_DRUM_VOICES; i++)
        if (dsp.drums[i].active && dsp.drums[i].env > 0.015f)
            cnt++;
    return cnt;
}

void sf_dsp_get_vis(sf_dsp_vis_t *out)
{
    uint32_t i;
    const uint8_t *src = (const uint8_t *)&dsp.vis;
    uint8_t *dst = (uint8_t *)out;
    for (i = 0; i < (uint32_t)sizeof(sf_dsp_vis_t); i++)
        dst[i] = src[i];
}

/* ---------------------------------------------------------- LFO shape --- */
static PLAT_RAM_CODE float eval_lfo(float ph, uint8_t shape)
{
    if (shape == 1u)
        return ph < 0.5f ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
    if (shape == 2u)
        return 2.0f * ph - 1.0f;
    if (shape == 3u)
        return ph < 0.5f ? 1.0f : -1.0f;
    return sf_ram_sin_turns(ph);
}

/* ----------------------------------------------------- Drum synthesis --- */
static PLAT_RAM_CODE void render_drums(float *out_l, float *out_r, uint32_t n)
{
    uint32_t v, i;
    uint8_t kit = dsp.cfg ? dsp.cfg->drum_kit : 0u;
    uint8_t bus = dsp.cfg ? dsp.cfg->drum_bus : 0u;
    float drum_gain = dsp.cfg ? ((float)dsp.cfg->drum_vol * 0.01f) * 0.95f : 0.78f;
    /* Distinct 10-Kit Character Tables:
       0:808, 1:909, 2:CR78, 3:LINN, 4:ELECTRO, 5:HOUSE, 6:JAZZ, 7:DX_FM, 8:LOFI, 9:TRAP */
    static const float K_END[SF_DRUM_KITS]   = {41.0f, 54.0f, 58.0f, 50.0f, 42.0f, 49.0f, 60.0f, 46.0f, 44.0f, 34.0f};
    static const float K_SWEEP[SF_DRUM_KITS] = {86.0f, 178.0f, 48.0f, 128.0f, 260.0f, 125.0f, 52.0f, 165.0f, 76.0f, 118.0f};
    static const float K_DEC[SF_DRUM_KITS]   = {0.00010f, 0.00022f, 0.00042f, 0.00026f, 0.00020f, 0.00021f, 0.00028f, 0.00019f, 0.00024f, 0.000075f};
    static const float K_PDEC[SF_DRUM_KITS]  = {0.0042f, 0.0038f, 0.0050f, 0.0036f, 0.0024f, 0.0034f, 0.0045f, 0.0030f, 0.0035f, 0.0039f};
    static const float S_TONE[SF_DRUM_KITS]  = {178.0f, 205.0f, 210.0f, 188.0f, 285.0f, 224.0f, 198.0f, 255.0f, 166.0f, 248.0f};
    static const float S_NMIX[SF_DRUM_KITS]  = {0.50f, 0.85f, 0.72f, 0.92f, 0.80f, 0.78f, 0.76f, 0.56f, 0.62f, 0.88f};
    static const float H_HZ[SF_DRUM_KITS]    = {7200.0f, 9600.0f, 5600.0f, 8400.0f, 10200.0f, 9200.0f, 5900.0f, 7600.0f, 4100.0f, 11200.0f};
    static const float C_HZ[SF_DRUM_KITS]    = {1180.0f, 1760.0f, 2250.0f, 1380.0f, 2020.0f, 1620.0f, 1150.0f, 2150.0f, 980.0f, 1860.0f};

    if (kit >= SF_DRUM_KITS) kit = 0u;

    for (v = 0; v < SF_DRUM_VOICES; v++) {
        drum_voice_t *d = &dsp.drums[v];
        float dec_amp, dec_p, tone_scale, dg_lvl;
        svf_c_t dc;
        if (!d->active)
            continue;
        dec_amp = (v == 0u) ? (1.0f - K_DEC[kit] / d->decay)
                : (v == 1u) ? (1.0f - ((kit == 2u) ? 0.00078f : (kit == 9u) ? 0.00058f : (kit == 6u) ? 0.00034f : 0.00046f) / d->decay)
                : (v == 2u) ? (1.0f - ((kit == 9u) ? 0.0032f : (kit == 2u) ? 0.0028f : (kit == 6u) ? 0.0017f : 0.0022f) / d->decay)
                : (v == 3u) ? (1.0f - ((kit == 6u) ? 0.00028f : (kit == 2u) ? 0.00052f : 0.00038f) / d->decay)
                : (v == 4u) ? (1.0f - ((kit == 2u) ? 0.00072f : 0.00050f) / d->decay)
                : (v == 5u) ? (1.0f - ((kit == 2u) ? 0.00068f : 0.00042f) / d->decay)
                : (v == 6u) ? (1.0f - ((kit == 6u) ? 0.00014f : 0.00024f) / d->decay)
                : (1.0f - 0.0024f / d->decay);
        dec_p = (v == 0u) ? (1.0f - K_PDEC[kit]) : (1.0f - 0.0026f);
        tone_scale = 0.55f + d->tone * 0.9f;
        dg_lvl = d->level * drum_gain;

        dc.a1 = 0.0f; dc.a2 = 0.0f; dc.a3 = 0.0f; dc.k = 0.5f; dc.type = SF_FLT_HP;
        if (v == 1u) {
            float cut = (kit == 0u) ? 1650.0f : (kit == 8u) ? 1400.0f : (kit == 1u || kit == 9u) ? 2800.0f : 2100.0f;
            uint8_t ft = (kit == 8u) ? SF_FLT_LP : (kit == 6u ? SF_FLT_HP : SF_FLT_BP);
            svf_prep(&dc, sf_clamp(3.14159f * cut * tone_scale * INV_SR, 0.02f, 0.85f), (kit == 2u) ? 0.34f : 0.72f, ft);
        } else if (v == 2u || v == 3u) {
            float f = H_HZ[kit] * (v == 2u ? 1.0f : 0.86f) * tone_scale * d->tune;
            uint8_t ft = (kit == 8u) ? SF_FLT_LP : (kit == 0u || kit == 4u) ? SF_FLT_BP : SF_FLT_HP;
            svf_prep(&dc, sf_clamp(3.14159f * f * INV_SR, 0.02f, 0.88f), (kit == 0u) ? 0.36f : 0.55f, ft);
        } else if (v == 4u) {
            float g = sf_clamp(3.14159f * C_HZ[kit] * (0.75f + d->tone * 0.5f) * d->tune * INV_SR, 0.02f, 0.72f);
            svf_prep(&dc, g, (kit == 2u) ? 0.32f : 0.56f, SF_FLT_BP);
        } else if (v == 6u) {
            float rf = (kit == 8u) ? 2600.0f : (kit == 6u) ? 3100.0f : 4800.0f;
            float g = sf_clamp(3.14159f * (rf + d->tone * 1600.0f) * d->tune * INV_SR, 0.02f, 0.85f);
            svf_prep(&dc, g, 0.42f, (kit == 8u) ? SF_FLT_LP : SF_FLT_HP);
        }

        float pan = (v == 2u) ? -0.18f : (v == 3u) ? 0.18f : (v == 4u) ? -0.12f : (v == 5u) ? 0.22f : (v == 6u) ? 0.28f : (v == 7u) ? -0.25f : 0.0f;
        float pl_g = 1.0f - pan, pr_g = 1.0f + pan;

        for (i = 0; i < n; i++) {
            float s = 0.0f;
            float nse = fast_noise();
            if (v == 0u) {
                float sub_b = (bus == SF_BUS_SUB) ? 1.24f : 1.0f;
                float hz = (K_END[kit] + K_SWEEP[kit] * d->pitch_env * d->pitch_env * tone_scale) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                if (kit == 7u) {
                    /* DX7 2-Op FM Kick: sharp woody mod carrier */
                    d->ph2 = fast_wrap01(d->ph2 + (hz * 1.414f) * INV_SR);
                    s = sf_ram_sin_turns(d->ph1 + 0.42f * d->pitch_env * sf_ram_sin_turns(d->ph2)) * d->env * 1.12f;
                } else {
                    s = sf_ram_sin_turns(d->ph1) * d->env * 1.15f * sub_b;
                    /* 909 / Linn / Electro transient beater snap */
                    if ((kit == 1u || kit == 3u || kit == 4u) && d->pitch_env > 0.72f) {
                        float p45 = d->ph1 * 4.5f;
                        p45 -= (float)(int32_t)p45;
                        s += (nse * 0.22f + sf_ram_sin_turns(p45) * 0.28f) * (d->pitch_env - 0.72f) * 3.0f;
                    }
                    /* 808 / 909 / Trap analog punch & sub harmonics */
                    if (kit == 1u || kit == 4u || kit == 9u || bus == SF_BUS_WARM)
                        s = sf_soft_clip(s * (kit == 9u ? 1.55f : 1.35f));
                    if (dsp.cfg && dsp.cfg->hp_plugged == 0u) {
                        /* Onboard micro-speaker lacks <100 Hz excursion: blend 2nd/3rd chest harmonics + snap */
                        float p2 = fast_wrap01(d->ph1 * 2.0f);
                        float p3 = fast_wrap01(d->ph1 + p2);
                        float chest = (sf_ram_sin_turns(p2) * 0.42f + sf_ram_sin_turns(p3) * 0.24f) * d->env;
                        float snap = (d->pitch_env > 0.55f) ? (sf_ram_sin_turns(p3) * 0.26f + nse * 0.14f) * (d->pitch_env - 0.55f) * 2.2f : 0.0f;
                        s = sf_soft_clip(s * 0.92f + chest + snap);
                    }
                }
            } else if (v == 1u) {
                float hz = (S_TONE[kit] + (kit == 4u ? 190.0f : 105.0f) * d->pitch_env) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                if (kit == 7u) {
                    /* Metallic FM Snare */
                    d->ph2 = fast_wrap01(d->ph2 + (hz * 1.618f) * INV_SR);
                    float fm = sf_ram_sin_turns(d->ph1 + 0.55f * sf_ram_sin_turns(d->ph2));
                    s = (fm * 0.52f + svf_tick_c(&d->bp, nse, &dc) * S_NMIX[kit]) * d->env;
                } else {
                    float body = sf_ram_sin_turns(d->ph1) * ((kit == 2u) ? 0.22f : (kit == 9u) ? 0.58f : 0.45f);
                    float wire = svf_tick_c(&d->bp, nse, &dc) * S_NMIX[kit] * tone_scale;
                    /* LinnDrum iconic 80s gated snare plateau */
                    float gate_e = (kit == 3u && d->env > 0.36f) ? 0.92f : d->env;
                    s = (body * d->env + wire * gate_e);
                }
            } else if (v == 2u || v == 3u) {
                if (kit == 0u || kit == 2u || kit == 4u || kit == 7u) {
                    /* True 6-square metallic ratio emulation (808/CR-78/Electro/DX hats) */
                    float f1 = (kit == 0u ? 304.0f : kit == 2u ? 498.0f : kit == 7u ? 412.0f : 368.0f) * d->tune;
                    float f2 = (kit == 0u ? 522.0f : kit == 2u ? 734.0f : kit == 7u ? 583.0f : 612.0f) * d->tune;
                    d->ph1 = fast_wrap01(d->ph1 + f1 * INV_SR);
                    d->ph2 = fast_wrap01(d->ph2 + f2 * INV_SR);
                    float metal = ((d->ph1 < 0.5f ? 1.0f : -1.0f) * (d->ph2 < 0.5f ? 1.0f : -1.0f));
                    float nmix = (kit == 0u || kit == 2u) ? 0.42f : 0.55f;
                    s = svf_tick_c(&d->bp, nse * nmix + metal * 0.65f, &dc) * d->env * 0.68f;
                } else {
                    /* 909 / Linn / House / Jazz / LoFi / Trap filtered noise hats */
                    s = svf_tick_c(&d->bp, nse, &dc) * d->env * ((kit == 1u || kit == 9u) ? 0.78f : 0.62f);
                }
            } else if (v == 4u) {
                float burst = (kit == 6u) ? 0.65f
                    : (d->pitch_env > 0.72f ? ((uint32_t)(d->pitch_env * (kit == 2u ? 26.0f : 42.0f)) & 1u ? 1.0f : 0.22f) : 1.0f);
                s = svf_tick_c(&d->bp, nse, &dc) * d->env * burst * (kit == 6u ? 0.68f : 0.96f);
            } else if (v == 5u) {
                float t_end = (kit == 2u) ? 215.0f : (kit == 6u) ? 132.0f : (kit == 9u) ? 68.0f : (kit == 3u) ? 98.0f : 105.0f;
                float t_swp = (kit == 2u) ? 115.0f : (kit == 6u) ? 62.0f : (kit == 3u) ? 195.0f : 145.0f;
                float hz = (t_end + t_swp * d->pitch_env * tone_scale) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                if (kit == 7u || kit == 4u) {
                    /* DX7 Log Drum / Electro Synth Tom */
                    d->ph2 = fast_wrap01(d->ph2 + (hz * 2.0f) * INV_SR);
                    s = sf_ram_sin_turns(d->ph1 + 0.48f * d->pitch_env * sf_ram_sin_turns(d->ph2)) * d->env * 0.90f;
                } else {
                    s = sf_ram_sin_turns(d->ph1) * d->env * 0.88f;
                }
            } else if (v == 6u) {
                float r1 = (kit == 0u) ? 540.0f : (kit == 6u) ? 520.0f : (kit == 7u) ? 510.0f : 480.0f;
                float r2 = (kit == 0u) ? 800.0f : (kit == 6u) ? 815.0f : (kit == 7u) ? 721.0f : 731.0f;
                d->ph1 = fast_wrap01(d->ph1 + (r1 * d->tune) * INV_SR);
                d->ph2 = fast_wrap01(d->ph2 + (r2 * d->tune) * INV_SR);
                float fm = (kit == 0u)
                    ? (((d->ph1 < 0.5f ? 1.0f : -1.0f) + (d->ph2 < 0.5f ? 1.0f : -1.0f)) * 0.48f)
                    : sf_ram_sin_turns(d->ph1 + ((kit == 6u) ? 0.38f : 0.72f) * sf_ram_sin_turns(d->ph2));
                s = svf_tick_c(&d->bp, nse * ((kit == 6u || kit == 1u) ? 0.48f : 0.25f) + fm * 0.62f, &dc) * d->env * 0.56f;
            } else {
                float rm1 = (kit == 2u) ? 1580.0f : (kit == 6u) ? 860.0f : (kit == 0u) ? 1710.0f : 460.0f;
                float rm2 = (kit == 2u) ? 2370.0f : (kit == 6u) ? 1290.0f : (kit == 0u) ? 485.0f : 1660.0f;
                d->ph1 = fast_wrap01(d->ph1 + (rm1 * d->tune) * INV_SR);
                d->ph2 = fast_wrap01(d->ph2 + (rm2 * d->tune) * INV_SR);
                s = (sf_ram_sin_turns(d->ph1) * 0.56f + sf_ram_sin_turns(d->ph2) * 0.44f * tone_scale) * d->env;
            }

            if (kit == 8u)
                s = (float)(int32_t)(s * 12.0f) * (1.0f / 12.0f);
            else if (kit == 3u)
                s = (float)(int32_t)(s * 48.0f) * (1.0f / 48.0f);

            if (bus == SF_BUS_COMP)
                s = sf_soft_clip(s * 1.48f) * 0.80f;
            else if (bus == SF_BUS_WARM)
                s = sf_soft_clip(s * 1.15f) * 0.92f;
            else if (bus == SF_BUS_SPARK && v >= 1u)
                s *= 1.15f;

            d->atk_env += 0.055f;
            if (d->atk_env > 1.0f) d->atk_env = 1.0f;
            s *= dg_lvl * d->atk_env;
            if (d->click_rem > 0u) {
                s += d->click_s * ((float)d->click_rem * (1.0f / 32.0f));
                d->click_rem--;
            }
            d->last_s = s;
            out_l[i] += s * pl_g;
            out_r[i] += s * pr_g;

            d->env *= dec_amp;
            d->pitch_env *= dec_p;
        }
        if (d->env < 0.0012f && d->click_rem == 0u)
            d->active = 0u;
    }
}

/* --------------------------------------------------- Main render block --- */
static float block_l[256] PLAT_POOL;
static float block_r[256] PLAT_POOL;

static PLAT_RAM_CODE void step_boot_jingle(uint32_t n)
{
    uint32_t j = dsp.jingle_idx < 6u ? dsp.jingle_idx : 0u;
    uint32_t step_samp = J_STEP_SAMP[j];
    uint32_t i;
    while (dsp.jingle_step < 5u && dsp.jingle_sample_pos >= dsp.jingle_step * step_samp) {
        uint8_t s = dsp.jingle_step;
        uint8_t note = J_NOTES[j][s];
        uint8_t vel = (s == 4u) ? 84u : 72u;
        uint32_t hold_samp = (s == 4u) ? J_LAST_SAMP[j] : J_HOLD_SAMP[j];
        internal_note_on(note, vel, 1u);
        dsp.jingle_rel_at[s] = dsp.jingle_step * step_samp + hold_samp;
        dsp.jingle_rel_done[s] = 0u;
        dsp.jingle_step++;
    }
    uint8_t pending_rel = 0u;
    for (i = 0; i < dsp.jingle_step && i < 5u; i++) {
        if (!dsp.jingle_rel_done[i]) {
            if (dsp.jingle_sample_pos >= dsp.jingle_rel_at[i]) {
                internal_note_off(J_NOTES[dsp.jingle_idx][i]);
                dsp.jingle_rel_done[i] = 1u;
            } else {
                pending_rel = 1u;
            }
        }
    }
    dsp.jingle_sample_pos += n;
    if (dsp.jingle_step >= 5u && !pending_rel) {
        dsp.jingle_active = 0u;
    }
}

PLAT_RAM_CODE void sf_dsp_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    sf_engine_cfg_t *cfg = dsp.cfg;
    sf_preset_t *p;
    float master_target = (float)master_q12 * (1.0f / 4096.0f);
    float gain_scale = 8300000.0f;
    uint32_t active_cnt = 0u;

    if (n > 256u)
        n = 256u;
    if (!cfg) {
        for (i = 0; i < n * 2u; i++) lr[i] = 0;
        return;
    }

    if (dsp.jingle_active)
        step_boot_jingle(n);

    p = dsp.jingle_active ? &dsp.jingle_patch : &cfg->patch;

    for (i = 0; i < n; i++) {
        block_l[i] = 0.0f;
        block_r[i] = 0.0f;
    }
    uint32_t drum_cnt = 0u;
    for (v = 0; v < SF_DRUM_VOICES; v++)
        if (dsp.drums[v].active) drum_cnt++;
    for (v = 0; v < SF_MAX_VOICES; v++)
        if (dsp.voices[v].active) active_cnt++;

    /* Decode preset parameters once per block */
    float a_atk = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->a_atk, 0.55f, 0.002f));
    float a_dec = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->a_dec, 3.0f, 0.03f));
    float a_sus = (float)p->a_sus * (1.0f / 127.0f);
    float a_rel = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->a_rel, 3.2f, 0.015f));

    float f_atk = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->f_atk, 1.2f, 0.003f));
    float f_dec = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->f_dec, 2.4f, 0.03f));
    float f_sus = (float)p->f_sus * (1.0f / 127.0f);
    float f_rel = 1.0f - sf_ram_expf(-INV_SR / byte_to_env_sec(p->f_rel, 2.5f, 0.03f));

    float lfo1_hz = 0.1f + ((float)p->lfo1_rate * (1.0f / 127.0f)) * ((float)p->lfo1_rate * (1.0f / 127.0f)) * 18.0f;
    float lfo2_hz = 0.1f + ((float)p->lfo2_rate * (1.0f / 127.0f)) * ((float)p->lfo2_rate * (1.0f / 127.0f)) * 18.0f;
    float swarm_hz = sf_clamp((float)p->swarm_rate * 0.05f, 0.05f, 4.0f);
    dsp.lfo1_ph = fast_wrap01(dsp.lfo1_ph + lfo1_hz * (float)n * INV_SR);
    dsp.lfo2_ph = fast_wrap01(dsp.lfo2_ph + lfo2_hz * (float)n * INV_SR);
    dsp.swarm_ph = fast_wrap01(dsp.swarm_ph + swarm_hz * (float)n * INV_SR);
    float lfo1 = eval_lfo(dsp.lfo1_ph, sf_lfo1_shape(p)) * ((float)p->lfo1_depth * 0.01f);
    float lfo2 = eval_lfo(dsp.lfo2_ph, sf_lfo2_shape(p)) * ((float)p->lfo2_depth * 0.01f);
    float swarm_mod = sf_ram_sin_turns(dsp.swarm_ph) * ((float)p->swarm_depth * 0.0035f);

    uint8_t l1d = sf_lfo1_dest(p);
    uint8_t l2d = sf_lfo2_dest(p);
    float mod_cut1 = (l1d == SF_LFO_CUT ? lfo1 : 0.0f) + (l2d == SF_LFO_CUT ? lfo2 : 0.0f);
    float mod_cut2 = (l1d == SF_LFO_F2 ? lfo1 : 0.0f) + (l2d == SF_LFO_F2 ? lfo2 : 0.0f);
    float mod_reso = (l1d == SF_LFO_RESO ? lfo1 : 0.0f) + (l2d == SF_LFO_RESO ? lfo2 : 0.0f);
    float mod_pitch = (l1d == SF_LFO_PITCH ? lfo1 : 0.0f) + (l2d == SF_LFO_PITCH ? lfo2 : 0.0f) + swarm_mod * 0.5f;
    float mod_pan = (l1d == SF_LFO_PAN ? lfo1 : 0.0f) + (l2d == SF_LFO_PAN ? lfo2 : 0.0f);
    float mod_spread = (l1d == SF_LFO_SPREAD ? lfo1 : 0.0f) + (l2d == SF_LFO_SPREAD ? lfo2 : 0.0f) + swarm_mod;
    float mod_amp = (l1d == SF_LFO_AMP ? lfo1 : 0.0f) + (l2d == SF_LFO_AMP ? lfo2 : 0.0f);

    float cut1_base = byte_to_hz(p->f1_cut);
    float cut2_base = byte_to_hz(p->f2_cut);
    float reso_mod_amt = sf_clamp(mod_reso * 3.8f, -3.5f, 3.8f);
    float q1 = sf_clamp(byte_to_reso_q(p->f1_res) + reso_mod_amt, 0.5f, 15.5f);
    float q2 = sf_clamp(byte_to_reso_q(p->f2_res), 0.5f, 15.5f);
    float fenv_oct = (float)p->f1_env_s * 0.055f;
    float kf_amt = (float)p->f_keyfollow * 0.01f;
    float f_bal = (float)p->f_bal * 0.01f;

    float osc_bal = (float)p->osc_bal * 0.01f;
    float o1_mix = 1.0f - osc_bal * 0.65f;
    float o2_mix = 0.35f + osc_bal * 0.65f;
    float sub_mix = (float)p->sub_vol * 0.008f;
    float noise_mix = (float)p->noise_vol * 0.006f;
    float ring_mix = (float)p->ring_vol * 0.008f;
    float fm_amt = (float)p->fm_amt * 0.012f;
    float pw = sf_clamp(0.08f + (float)p->pw * (0.84f / 127.0f), 0.08f, 0.92f);
    float wt_pos = (float)p->pw * (1.0f / 127.0f);
    float osc2_ratio = sf_ram_exp2f(((float)p->osc2_semi + (float)p->osc2_detune * 0.0078f) * (1.0f / 12.0f));
    float spread = sf_clamp((float)p->spread * 0.01f + mod_spread * 0.4f, 0.05f, 1.4f);
    uint8_t osc_mode = sf_osc_mode(p);
    uint8_t sub_wave = sf_sub_wave(p);
    uint8_t f1_type = sf_f1_type(p);
    uint8_t f2_type = sf_f2_type(p);
    uint8_t routing = sf_f_route(p);
    uint8_t do_sync = sf_osc_sync(p);
    uint8_t sat_mode = sf_sat_mode(p);
    uint8_t unison = sf_unison(p);
    float drive = 1.0f + (float)p->fx_drive * 0.032f;
    float amp_d = sf_clamp(((l1d == SF_LFO_AMP ? (float)p->lfo1_depth : 0.0f)
                          + (l2d == SF_LFO_AMP ? (float)p->lfo2_depth : 0.0f)) * 0.0038f, 0.0f, 0.42f);
    float trem = sf_clamp((1.0f - amp_d) + mod_amp * 0.36f, 0.16f, 1.0f);
    float punch_amt = dsp.jingle_active ? 0.0f : (float)sf_punch_pct(p) * 0.01f;
    float uni_pan_norm = (float)p->uni_pan * 0.01f;
    float glide_coeff = (p->glide == 0u) ? 1.0f : sf_clamp(1.0f - (float)p->glide * 0.0108f, 0.03f, 0.9f);
    dsp.pitch_bend_cur += (dsp.pitch_bend_targ - dsp.pitch_bend_cur) * 0.35f;
    dsp.mod_wheel_cur += (dsp.mod_wheel_targ - dsp.mod_wheel_cur) * 0.25f;
    /* Adaptive CPU load shedding when polyphonic synth chords + drum machine run together */
    uint8_t lite_hyper = (active_cnt + (drum_cnt > 0u ? 1u : 0u) >= 4u) ? 1u : 0u;
    uint8_t skip_blep = (active_cnt >= 5u || (active_cnt >= 3u && drum_cnt >= 2u)) ? 1u : 0u;
    uint32_t vis_age = 0u;
    int32_t vis_v = -1;
    float vis_c1_oct = mod_cut1 * 2.5f + dsp.mod_wheel_cur * 3.2f;
    float vis_c2_oct = mod_cut2 * 2.5f + dsp.mod_wheel_cur * 1.8f;

    for (v = 0; v < SF_MAX_VOICES; v++) {
        va_voice_t *vc = &dsp.voices[v];
        svf_c_t c1, c2;
        if (!vc->active)
            continue;

        vc->pitch_cur += ((float)vc->note - vc->pitch_cur) * glide_coeff;
        float punch_cur = vc->punch_env * punch_amt;
        float note_f = vc->pitch_cur + (float)p->oct * 12.0f + mod_pitch * 1.5f + dsp.pitch_bend_cur + punch_cur * 2.1f;
        float f0 = 440.0f * sf_ram_exp2f((note_f - 69.0f) * (1.0f / 12.0f));
        float dt1 = sf_clamp(f0 * INV_SR, 0.0001f, 0.45f);
        float dt2 = sf_clamp(f0 * osc2_ratio * INV_SR, 0.0001f, 0.45f);
        float dt_sub = dt1 * 0.5f;

        float kf_oct = ((vc->pitch_cur - 60.0f) * (1.0f / 12.0f)) * kf_amt;
        float vel_norm = (float)vc->vel * (1.0f / 127.0f);
        float vel_cut = (vel_norm - 0.55f) * 1.15f;
        float c1_oct = mod_cut1 * 2.5f + (vc->flt_env * fenv_oct) + kf_oct + vel_cut + ((float)vc->prs * (1.0f / 127.0f)) * 1.4f + dsp.mod_wheel_cur * 3.2f + punch_cur * 1.35f;
        float cut_scale = sf_ram_exp2f(c1_oct);
        float cut1_hz = sf_clamp(cut1_base * cut_scale, 40.0f, 12500.0f);
        float g1 = sf_clamp(3.14159f * cut1_hz * INV_SR, 0.0028f, 0.89f);
        float hf1 = sf_clamp((cut1_hz - 7500.0f) * (1.0f / 5000.0f), 0.0f, 1.0f);
        float q1_eff = q1 * (1.0f - hf1 * 0.65f);
        if (q1_eff < 0.5f) q1_eff = 0.5f;

        float c2_oct = mod_cut2 * 2.5f + kf_oct * 0.5f + vel_cut * 0.4f + dsp.mod_wheel_cur * 1.8f;
        float cut2_hz = sf_clamp(cut2_base * sf_ram_exp2f(c2_oct), 40.0f, 12500.0f);
        if (vc->age >= vis_age) {
            vis_age = vc->age;
            vis_v = (int32_t)v;
            vis_c1_oct = c1_oct;
            vis_c2_oct = c2_oct;
        }
        float g2 = sf_clamp(3.14159f * cut2_hz * INV_SR, 0.0028f, 0.89f);
        float hf2 = sf_clamp((cut2_hz - 7500.0f) * (1.0f / 5000.0f), 0.0f, 1.0f);
        float q2_eff = q2 * (1.0f - hf2 * 0.65f);
        if (q2_eff < 0.5f) q2_eff = 0.5f;

        svf_prep(&c1, g1, 1.0f / q1_eff, f1_type);
        svf_prep(&c2, g2, 1.0f / q2_eff, f2_type);

        uint8_t bands_conflict = 0u;
        if (routing == SF_ROUTE_SER && f_bal > 0.14f && f_bal < 0.86f) {
            if ((f1_type == SF_FLT_HP && f2_type == SF_FLT_LP && cut1_hz >= cut2_hz * 0.78f) ||
                (f1_type == SF_FLT_LP && f2_type == SF_FLT_HP && cut2_hz >= cut1_hz * 0.78f))
                bands_conflict = 1u;
        }
        float eff_cut = (f1_type == SF_FLT_HP) ? sf_clamp(7500.0f - cut1_hz, 400.0f, 7000.0f) : cut1_hz;
        float flt_comp = (f1_type == SF_FLT_BP) ? 1.32f : (f1_type == SF_FLT_HP && cut1_hz > 1400.0f) ? 1.24f : 1.0f;
        float cut_comp = sf_clamp(1.22f - eff_cut * (0.28f / 6000.0f), 0.94f, 1.25f);
        float drv_comp = 1.0f / (1.0f + (drive - 1.0f) * 0.36f);
        float q_comp = 1.0f / (1.0f + sf_clamp((q1_eff - 2.2f) * 0.048f, 0.0f, 0.45f));
        float norm_gain = sf_clamp(flt_comp * cut_comp * drv_comp * q_comp, 0.52f, 1.38f);
        float vel_g = (0.35f + 0.65f * vel_norm) * 0.175f * norm_gain * trem;
        if (vc->vel_g_cur < 0.0f)
            vc->vel_g_cur = vel_g;
        float vpan = sf_clamp((((float)(v & 3u) - 1.5f) * 0.22f * spread * uni_pan_norm) + mod_pan * 0.5f, -0.82f, 0.82f);
        float pan_l = 0.707f * (1.0f - vpan);
        float pan_r = 0.707f * (1.0f + vpan);

        float det = 0.005f + 0.020f * spread;
        float dth0 = dt1 * (1.0f - det), dth1 = dt1 * (1.0f + det);
        float dth2 = dt1 * (1.0f - 0.5f * det), dth3 = dt1 * (1.0f + 0.5f * det);
        float udet = 0.003f + 0.009f * spread;
        float dtu0 = dt1 * (1.0f - udet), dtu1 = dt1 * (1.0f + udet);

        for (i = 0; i < n; i++) {
            float o1 = 0.0f, o2 = 0.0f, sub = 0.0f, sig_l, sig_r;
            if (vc->amp_st == ENV_ATK) {
                vc->amp_env += (1.05f - vc->amp_env) * a_atk;
                if (vc->amp_env >= 1.0f) {
                    vc->amp_env = 1.0f;
                    vc->amp_st = ENV_DEC;
                }
            } else if (vc->amp_st == ENV_DEC) {
                vc->amp_env += (a_sus - vc->amp_env) * a_dec;
            } else if (vc->amp_st == ENV_REL) {
                vc->amp_env -= vc->amp_env * a_rel;
                if (vc->amp_env < 0.0022f) {
                    vc->amp_env = 0.0f;
                    if (vc->click_rem == 0u) {
                        vc->active = 0u;
                        vc->last_l = vc->last_r = 0.0f;
                        break;
                    }
                }
            }

            if (vc->flt_st == ENV_ATK) {
                vc->flt_env += (1.05f - vc->flt_env) * f_atk;
                if (vc->flt_env >= 1.0f) {
                    vc->flt_env = 1.0f;
                    vc->flt_st = ENV_DEC;
                }
            } else if (vc->flt_st == ENV_DEC) {
                vc->flt_env += (f_sus - vc->flt_env) * f_dec;
            } else if (vc->flt_st == ENV_REL) {
                vc->flt_env -= vc->flt_env * f_rel;
            }

            vc->phase1 += dt1;
            if (vc->phase1 >= 1.0f) {
                vc->phase1 -= 1.0f;
                if (do_sync)
                    vc->phase2 = 0.0f;
            }
            if (osc_mode == SF_OSC_HYPERSAW) {
                float s0 = 2.0f * vc->phase1 - 1.0f - (skip_blep ? 0.0f : polyblep(vc->phase1, dt1));
                vc->hyper_ph[0] = fast_wrap01(vc->hyper_ph[0] + dth0);
                vc->hyper_ph[1] = fast_wrap01(vc->hyper_ph[1] + dth1);
                if (lite_hyper || (vc->amp_st == ENV_REL && vc->amp_env < 0.025f)) {
                    o1 = (s0 + (2.0f * vc->hyper_ph[0] - 1.0f) + (2.0f * vc->hyper_ph[1] - 1.0f)) * 0.45f;
                } else {
                    vc->hyper_ph[2] = fast_wrap01(vc->hyper_ph[2] + dth2);
                    vc->hyper_ph[3] = fast_wrap01(vc->hyper_ph[3] + dth3);
                    o1 = (s0 + (2.0f * vc->hyper_ph[0] - 1.0f) + (2.0f * vc->hyper_ph[1] - 1.0f)
                          + (2.0f * vc->hyper_ph[2] - 1.0f) + (2.0f * vc->hyper_ph[3] - 1.0f)) * 0.34f;
                }
            } else if (osc_mode == SF_OSC_PULSE) {
                o1 = vc->phase1 < pw ? 1.0f : -1.0f;
                if (!skip_blep) {
                    o1 += polyblep(vc->phase1, dt1);
                    o1 -= polyblep(fast_wrap01(vc->phase1 + (1.0f - pw)), dt1);
                }
            } else if (osc_mode == SF_OSC_TRI) {
                o1 = 1.0f - 4.0f * sf_fabs(vc->phase1 - 0.5f);
            } else if (osc_mode == SF_OSC_WT) {
                float wf = sf_clamp(wt_pos * 7.0f, 0.0f, 6.999f);
                uint32_t fi0 = (uint32_t)wf;
                float ffrac = wf - (float)fi0;
                float sp = vc->phase1 * 64.0f;
                uint32_t s0 = (uint32_t)sp & 63u;
                uint32_t s1 = (s0 + 1u) & 63u;
                float sfrac = sp - (float)(uint32_t)sp;
                float v0 = wt_bank[fi0][s0] + (wt_bank[fi0][s1] - wt_bank[fi0][s0]) * sfrac;
                float v1 = wt_bank[fi0 + 1u][s0] + (wt_bank[fi0 + 1u][s1] - wt_bank[fi0 + 1u][s0]) * sfrac;
                o1 = v0 + (v1 - v0) * ffrac;
            } else {
                o1 = 2.0f * vc->phase1 - 1.0f - (skip_blep ? 0.0f : polyblep(vc->phase1, dt1));
            }

            if (unison > 1u && osc_mode != SF_OSC_HYPERSAW && !skip_blep && active_cnt < 4u) {
                vc->hyper_ph[0] = fast_wrap01(vc->hyper_ph[0] + dtu0);
                vc->hyper_ph[1] = fast_wrap01(vc->hyper_ph[1] + dtu1);
                float u_a = (osc_mode == SF_OSC_PULSE) ? (vc->hyper_ph[0] < pw ? 0.65f : -0.65f) : (2.0f * vc->hyper_ph[0] - 1.0f) * 0.65f;
                float u_b = (osc_mode == SF_OSC_PULSE) ? (vc->hyper_ph[1] < pw ? 0.65f : -0.65f) : (2.0f * vc->hyper_ph[1] - 1.0f) * 0.65f;
                o1 = (o1 + (u_a + u_b) * (0.28f + 0.1f * (float)unison)) * 0.68f;
            }

            vc->phase2 = fast_wrap01(vc->phase2 + dt2 + fm_amt * 0.02f * o1);
            if (osc_mode == SF_OSC_PULSE)
                o2 = vc->phase2 < pw ? 1.0f : -1.0f;
            else if (osc_mode == SF_OSC_TRI)
                o2 = 1.0f - 4.0f * sf_fabs(vc->phase2 - 0.5f);
            else
                o2 = 2.0f * vc->phase2 - 1.0f - (skip_blep ? 0.0f : polyblep(vc->phase2, dt2));

            vc->phase_sub = fast_wrap01(vc->phase_sub + dt_sub);
            sub = (sub_wave == 1u) ? sf_ram_sin_turns(vc->phase_sub)
                : (sub_wave == 2u) ? (2.0f * vc->phase_sub - 1.0f)
                : (vc->phase_sub < 0.5f ? 1.0f : -1.0f);

            float src1 = o1 * o1_mix + sub * sub_mix
                + (noise_mix > 0.001f ? fast_noise() * noise_mix : 0.0f)
                + (ring_mix > 0.001f ? (o1 * o2) * ring_mix : 0.0f);
            float src2 = o2 * o2_mix;

            if (sat_mode == SF_SAT_HARD) {
                src1 = sf_clamp(src1 * drive, -0.95f, 0.95f);
            } else if (sat_mode == SF_SAT_TUBE) {
                src1 = sf_soft_clip(src1 * drive * 1.2f);
            } else if (sat_mode == SF_SAT_SOFT) {
                src1 = sf_soft_clip(src1 * (0.82f + drive * 0.22f));
            }

            if (routing == SF_ROUTE_SPL) {
                float fl = svf_tick_c(&vc->f1l, src1, &c1);
                float fr = svf_tick_c(&vc->f2r, src2, &c2);
                float w1 = 1.0f - f_bal * 0.45f;
                float w2 = 0.55f + f_bal * 0.45f;
                sig_l = (fl * w1 + fr * 0.45f * w2) * pan_l;
                sig_r = (fl * 0.45f * w1 + fr * w2) * pan_r;
            } else if (routing == SF_ROUTE_PAR) {
                float comb = (src1 + src2) * 0.7f;
                float fl = svf_tick_c(&vc->f1l, comb, &c1);
                float fr = svf_tick_c(&vc->f2r, comb, &c2);
                float mixf = fl * (1.0f - f_bal * 0.6f) + fr * (0.4f + f_bal * 0.6f);
                sig_l = mixf * 0.84f * pan_l;
                sig_r = mixf * 0.84f * pan_r;
            } else {
                float comb = (src1 + src2) * 0.7f;
                float f1 = svf_tick_c(&vc->f1l, comb, &c1);
                if (f_bal <= 0.14f) {
                    sig_l = f1 * pan_l;
                    sig_r = f1 * pan_r;
                } else if (f_bal >= 0.86f) {
                    float f2 = svf_tick_c(&vc->f2r, comb, &c2);
                    sig_l = f2 * pan_l;
                    sig_r = f2 * pan_r;
                } else if (bands_conflict) {
                    float f2 = svf_tick_c(&vc->f2r, comb, &c2);
                    float mout = f1 * (1.0f - f_bal * 0.65f) + f2 * (0.35f + f_bal * 0.65f);
                    sig_l = mout * 0.84f * pan_l;
                    sig_r = mout * 0.84f * pan_r;
                } else {
                    float f2 = svf_tick_c(&vc->f2l, f1, &c2);
                    float mout = f1 * (1.0f - f_bal) + f2 * f_bal;
                    sig_l = mout * pan_l;
                    sig_r = mout * pan_r;
                }
            }

            vc->vel_g_cur += (vel_g - vc->vel_g_cur) * 0.02f;
            float punch_gain = 1.0f + vc->punch_env * punch_amt * 0.45f;
            vc->punch_env = sf_flush(vc->punch_env * 0.9968f);
            float g = vc->amp_env * vc->vel_g_cur * punch_gain;
            float outl = sig_l * g;
            float outr = sig_r * g;
            if (vc->click_rem > 0u) {
                float cf = (float)vc->click_rem * (1.0f / 64.0f);
                outl += vc->click_l * cf;
                outr += vc->click_r * cf;
                vc->click_rem--;
            }
            vc->last_l = outl;
            vc->last_r = outr;
            block_l[i] += outl;
            block_r[i] += outr;
        }
    }

    {
        float patch_g = dsp.jingle_active ? 1.0f : sf_ram_exp2f(sf_clamp((float)cfg->patch_gain_db, -12.0f, 12.0f) * 0.1660964f);
        float s_lvl = sf_clamp((float)cfg->synth_vol * 0.01f, 0.0f, 1.0f) * patch_g;
        float duck_amt_f = sf_clamp((float)cfg->duck_amt * 0.01f, 0.0f, 1.0f);
        if (dsp.s_lvl_cur < 0.0f)
            dsp.s_lvl_cur = s_lvl;
        if (dsp.duck_trig) {
            dsp.duck_atk_rem = 176u;
            dsp.duck_trig = 0u;
        }
        for (i = 0; i < n; i++) {
            dsp.s_lvl_cur += (s_lvl - dsp.s_lvl_cur) * 0.008f;
            if (dsp.duck_atk_rem > 0u) {
                dsp.duck_env += (1.0f - dsp.duck_env) * 0.025f;
                dsp.duck_atk_rem--;
            } else {
                dsp.duck_env = sf_flush(dsp.duck_env * 0.999874f);
            }
            float sg = dsp.s_lvl_cur * (1.0f - dsp.duck_env * duck_amt_f * 0.92f);
            block_l[i] *= sg;
            block_r[i] *= sg;
        }
        dsp.vis.duck_pct = (uint8_t)sf_clamp(dsp.duck_env * duck_amt_f * 100.0f, 0.0f, 100.0f);
    }

    float chor_swing = ((l1d == SF_LFO_CHORUS ? (float)p->lfo1_depth : 0.0f) + (l2d == SF_LFO_CHORUS ? (float)p->lfo2_depth : 0.0f)) * 0.0020f;
    float phas_swing = ((l1d == SF_LFO_PHASER ? (float)p->lfo1_depth : 0.0f) + (l2d == SF_LFO_PHASER ? (float)p->lfo2_depth : 0.0f)) * 0.0022f;
    float del_swing  = ((l1d == SF_LFO_DELAY  ? (float)p->lfo1_depth : 0.0f) + (l2d == SF_LFO_DELAY  ? (float)p->lfo2_depth : 0.0f)) * 0.0022f;
    float rev_swing  = ((l1d == SF_LFO_REVERB ? (float)p->lfo1_depth : 0.0f) + (l2d == SF_LFO_REVERB ? (float)p->lfo2_depth : 0.0f)) * 0.0024f;

    float chor_mix = sf_clamp((float)p->fx_chorus * 0.0065f - sf_clamp(chor_swing * 0.45f, 0.0f, 0.20f)
        + (l1d == SF_LFO_CHORUS ? lfo1 * 0.22f : 0.0f) + (l2d == SF_LFO_CHORUS ? lfo2 * 0.16f : 0.0f), 0.0f, 0.68f);
    float phas_mix = sf_clamp((float)p->fx_phaser * 0.0085f - sf_clamp(phas_swing * 0.40f, 0.0f, 0.18f)
        + (l1d == SF_LFO_PHASER ? lfo1 * 0.24f : 0.0f) + (l2d == SF_LFO_PHASER ? lfo2 * 0.18f : 0.0f), 0.0f, 0.82f);
    float phas_hz = sf_clamp((float)p->fx_phaser_rate * 0.1f, 0.1f, 6.0f);
    float del_fb = sf_clamp((float)p->fx_del_fb * 0.01f, 0.0f, 0.80f);
    float del_mix = sf_clamp((float)p->fx_del_mix * 0.0072f - sf_clamp(del_swing * 0.55f, 0.0f, 0.22f)
        + (l1d == SF_LFO_DELAY ? lfo1 * 0.20f : 0.0f) + (l2d == SF_LFO_DELAY ? lfo2 * 0.15f : 0.0f), 0.0f, 0.68f);
    float del_rev_feed_g = (l1d == SF_LFO_DELAY || l2d == SF_LFO_DELAY) ? 0.10f : 0.15f;
    float del_sec = 0.30f;
    if (p->fx_del_time >= 101u && p->fx_del_time <= 106u) {
        uint8_t sidx = (uint8_t)(p->fx_del_time - 101u);
        float mult = sidx == 0u ? 0.25f
                   : sidx == 1u ? 0.333333f
                   : sidx == 2u ? 0.50f
                   : sidx == 3u ? 0.75f
                   : sidx == 4u ? 1.00f
                   : 1.50f;
        float safe_bpm = sf_clamp((float)cfg->bpm, 40.0f, 240.0f);
        del_sec = sf_clamp((60.0f / safe_bpm) * mult, 0.04f, 0.74f);
    } else {
        del_sec = sf_clamp((float)p->fx_del_time * 0.01f, 0.04f, 0.74f);
    }
    float del_tap_targ = sf_clamp(del_sec * 22050.0f, 400.0f, (float)(DEL_LEN - 8u));
    float rev_mix = sf_clamp((float)p->fx_reverb * 0.0072f - sf_clamp(rev_swing * 0.55f, 0.0f, 0.24f)
        + (l1d == SF_LFO_REVERB ? lfo1 * 0.22f : 0.0f) + (l2d == SF_LFO_REVERB ? lfo2 * 0.16f : 0.0f), 0.0f, 0.72f);
    float rev_fb = sf_clamp(0.80f + rev_mix * 0.095f, 0.80f, 0.895f);

    float phas_a = -0.48f + 0.38f * sf_ram_sin_turns(dsp.phas_ph);
    float phas_inc = phas_hz * INV_SR;
    float chor_m = sf_ram_sin_turns(dsp.chor_ph);
    float rev_mod = sf_ram_sin_turns(dsp.rev_mod_ph) * 0.03f;
    uint8_t want_phas = (phas_mix > 0.02f) ? 1u : 0u;
    uint8_t want_chor = (chor_mix > 0.02f) ? 1u : 0u;
    uint8_t want_del = (del_mix > 0.01f) ? 1u : 0u;
    uint8_t want_rev = (rev_mix > 0.02f) ? 1u : 0u;
    uint8_t do_phas = (want_phas || dsp.phas_energy > 0.0005f) ? 1u : 0u;
    uint8_t do_chor = (want_chor || dsp.chor_energy > 0.0005f) ? 1u : 0u;
    uint8_t do_del = (want_del || dsp.del_energy > 0.0005f) ? 1u : 0u;
    uint8_t do_rev = (want_rev || dsp.rev_energy > 0.0005f) ? 1u : 0u;
    float phas_peak = 0.0f, chor_peak = 0.0f, del_peak = 0.0f, rev_peak = 0.0f;
    float sum_sq = 0.0f;

    if (!do_phas) {
        dsp.phas_energy = 0.0f;
        dsp.phas_st[0][0] = dsp.phas_st[0][1] = dsp.phas_st[1][0] = dsp.phas_st[1][1] = 0.0f;
        dsp.phas_st[2][0] = dsp.phas_st[2][1] = dsp.phas_st[3][0] = dsp.phas_st[3][1] = 0.0f;
    }
    if (!do_chor && !dsp.chor_cleared) {
        uint32_t ci;
        for (ci = 0u; ci < CHOR_LEN; ci++) {
            chor_l[ci] = 0;
            chor_r[ci] = 0;
        }
        dsp.chor_energy = 0.0f;
        dsp.chor_cleared = 1u;
    }
    if (!do_del && dsp.del_clr_rem > 0u) {
        uint32_t zi, chunk = n < dsp.del_clr_rem ? n : dsp.del_clr_rem;
        for (zi = 0u; zi < chunk; zi++) {
            del_buf[dsp.del_wr * 2u] = 0;
            del_buf[dsp.del_wr * 2u + 1u] = 0;
            dsp.del_wr = (dsp.del_wr + 1u) & (DEL_LEN - 1u);
        }
        dsp.del_clr_rem -= chunk;
        if (dsp.del_clr_rem == 0u) {
            dsp.del_lp_l = dsp.del_lp_r = 0.0f;
            dsp.del_hpf_xl = dsp.del_hpf_yl = dsp.del_hpf_xr = dsp.del_hpf_yr = 0.0f;
            dsp.del_out_prev_l = dsp.del_out_prev_r = 0.0f;
            dsp.del_out_cur_l = dsp.del_out_cur_r = 0.0f;
            dsp.del_energy = 0.0f;
        }
    }
    if (!do_rev && !dsp.rev_cleared) {
        uint32_t ri;
        for (ri = 0u; ri < REV_C0; ri++) rev_c0[ri] = 0;
        for (ri = 0u; ri < REV_C1; ri++) rev_c1[ri] = 0;
        for (ri = 0u; ri < REV_C2; ri++) rev_c2[ri] = 0;
        for (ri = 0u; ri < REV_C3; ri++) rev_c3[ri] = 0;
        for (ri = 0u; ri < REV_A0; ri++) rev_a0[ri] = 0;
        for (ri = 0u; ri < REV_A1; ri++) rev_a1[ri] = 0;
        for (ri = 0u; ri < REV_PRE; ri++) rev_pre[ri] = 0;
        for (ri = 0u; ri < 4u; ri++) {
            dsp.rev_clp[ri] = 0.0f;
            dsp.rev_hpf_x[ri] = dsp.rev_hpf_y[ri] = 0.0f;
        }
        dsp.rev_energy = 0.0f;
        dsp.rev_cleared = 1u;
    }

    for (i = 0; i < n; i++) {
        float xl = block_l[i];
        float xr = block_r[i];
        float del_rev_feed = 0.0f;

        if (do_phas) {
            if ((i & 7u) == 0u) {
                dsp.phas_ph = fast_wrap01(dsp.phas_ph + phas_inc * 8.0f);
                phas_a = -0.48f + 0.38f * sf_ram_sin_turns(dsp.phas_ph);
            }
            float pl = xl, pr = xr;
            uint32_t st;
            for (st = 0; st < 4u; st++) {
                float yl = phas_a * pl + dsp.phas_st[st][0];
                dsp.phas_st[st][0] = sf_flush(pl - phas_a * yl);
                pl = yl;
                float yr = phas_a * pr + dsp.phas_st[st][1];
                dsp.phas_st[st][1] = sf_flush(pr - phas_a * yr);
                pr = yr;
            }
            phas_peak += sf_fabs(pl) + sf_fabs(pr);
            xl = xl * (1.0f - phas_mix * 0.45f) + pl * phas_mix * 0.55f;
            xr = xr * (1.0f - phas_mix * 0.45f) + pr * phas_mix * 0.55f;
        }

        if (do_chor) {
            dsp.chor_cleared = 0u;
            chor_l[dsp.chor_wr] = (int16_t)(sf_clamp(xl, -3.5f, 3.5f) * 8192.0f);
            chor_r[dsp.chor_wr] = (int16_t)(sf_clamp(xr, -3.5f, 3.5f) * 8192.0f);
            if ((i & 15u) == 0u) {
                dsp.chor_ph = fast_wrap01(dsp.chor_ph + 0.48f * INV_SR * 16.0f);
                chor_m = sf_ram_sin_turns(dsp.chor_ph);
            }
            uint32_t d1 = (dsp.chor_wr - (uint32_t)(240.0f + 130.0f * chor_m)) & (CHOR_LEN - 1u);
            uint32_t d2 = (dsp.chor_wr - (uint32_t)(240.0f - 130.0f * chor_m)) & (CHOR_LEN - 1u);
            float cl = (float)chor_l[d1] * (1.0f / 8192.0f);
            float cr = (float)chor_r[d2] * (1.0f / 8192.0f);
            chor_peak += sf_fabs(cl) + sf_fabs(cr);
            xl += cl * chor_mix * 0.45f;
            xr += cr * chor_mix * 0.45f;
            dsp.chor_wr = (dsp.chor_wr + 1u) & (CHOR_LEN - 1u);
        }

        if (do_del) {
            dsp.del_clr_rem = DEL_LEN;
            dsp.del_tick ^= 1u;
            if (dsp.del_tick == 0u) {
                dsp.del_tap_cur += (del_tap_targ - dsp.del_tap_cur) * 0.004f;
                uint32_t tap_l = (uint32_t)dsp.del_tap_cur;
                float frac_l = dsp.del_tap_cur - (float)tap_l;
                uint32_t rd_l0 = (dsp.del_wr - tap_l) & (DEL_LEN - 1u);
                uint32_t rd_l1 = (rd_l0 - 1u) & (DEL_LEN - 1u);
                float dl0 = (float)del_buf[rd_l0 * 2u];
                float dl1 = (float)del_buf[rd_l1 * 2u];
                float dl = (dl0 + (dl1 - dl0) * frac_l) * (1.0f / 8192.0f);

                float tap_rf = dsp.del_tap_cur * 0.75f;
                uint32_t tap_r = (uint32_t)tap_rf;
                float frac_r = tap_rf - (float)tap_r;
                uint32_t rd_r0 = (dsp.del_wr - tap_r) & (DEL_LEN - 1u);
                uint32_t rd_r1 = (rd_r0 - 1u) & (DEL_LEN - 1u);
                float dr0 = (float)del_buf[rd_r0 * 2u + 1u];
                float dr1 = (float)del_buf[rd_r1 * 2u + 1u];
                float dr = (dr0 + (dr1 - dr0) * frac_r) * (1.0f / 8192.0f);

                float hp_l = sf_flush((dl - dsp.del_hpf_xl) + 0.958f * dsp.del_hpf_yl);
                dsp.del_hpf_xl = dl;
                dsp.del_hpf_yl = hp_l;
                float hp_r = sf_flush((dr - dsp.del_hpf_xr) + 0.958f * dsp.del_hpf_yr);
                dsp.del_hpf_xr = dr;
                dsp.del_hpf_yr = hp_r;

                float avg_xl = (xl + dsp.del_in_prev_l) * 0.5f;
                float avg_xr = (xr + dsp.del_in_prev_r) * 0.5f;
                dsp.del_lp_l = sf_flush(dsp.del_lp_l + ((hp_r * 0.86f + hp_l * 0.14f) * del_fb - dsp.del_lp_l) * 0.36f);
                dsp.del_lp_r = sf_flush(dsp.del_lp_r + ((hp_l * 0.86f + hp_r * 0.14f) * del_fb - dsp.del_lp_r) * 0.36f);
                float wl = sf_clamp(sf_soft_clip((avg_xl + dsp.del_lp_l) * 0.94f) * 1.08f, -3.5f, 3.5f);
                float wr = sf_clamp(sf_soft_clip((avg_xr + dsp.del_lp_r) * 0.94f) * 1.08f, -3.5f, 3.5f);
                del_buf[dsp.del_wr * 2u] = (int16_t)(wl * 8192.0f);
                del_buf[dsp.del_wr * 2u + 1u] = (int16_t)(wr * 8192.0f);
                dsp.del_wr = (dsp.del_wr + 1u) & (DEL_LEN - 1u);
                dsp.del_out_prev_l = dsp.del_out_cur_l;
                dsp.del_out_prev_r = dsp.del_out_cur_r;
                dsp.del_out_cur_l = dl;
                dsp.del_out_cur_r = dr;
            }
            dsp.del_in_prev_l = xl;
            dsp.del_in_prev_r = xr;
            float dl_out = dsp.del_tick ? (dsp.del_out_prev_l + dsp.del_out_cur_l) * 0.5f : dsp.del_out_cur_l;
            float dr_out = dsp.del_tick ? (dsp.del_out_prev_r + dsp.del_out_cur_r) * 0.5f : dsp.del_out_cur_r;
            del_peak += sf_fabs(dl_out) + sf_fabs(dr_out);
            xl += dl_out * del_mix;
            xr += dr_out * del_mix;
            del_rev_feed = (dl_out + dr_out) * del_mix * del_rev_feed_g;
        }

        if (do_rev) {
            dsp.rev_cleared = 0u;
            if ((i & 15u) == 0u) {
                dsp.rev_mod_ph = fast_wrap01(dsp.rev_mod_ph + 0.72f * INV_SR * 16.0f);
                rev_mod = sf_ram_sin_turns(dsp.rev_mod_ph) * 0.03f;
            }
            float pre_in = ((xl + xr) * 0.5f + del_rev_feed) * rev_mix;
            float mono = (float)rev_pre[dsp.rev_pre_i] * (1.0f / 8192.0f);
            rev_pre[dsp.rev_pre_i] = (int16_t)(sf_clamp(pre_in, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_pre_i >= REV_PRE) dsp.rev_pre_i = 0u;

            float c0 = (float)rev_c0[dsp.rev_ci[0]] * (1.0f / 8192.0f);
            float c1 = (float)rev_c1[dsp.rev_ci[1]] * (1.0f / 8192.0f);
            float c2 = (float)rev_c2[dsp.rev_ci[2]] * (1.0f / 8192.0f);
            float c3 = (float)rev_c3[dsp.rev_ci[3]] * (1.0f / 8192.0f);
            dsp.rev_clp[0] = sf_flush(dsp.rev_clp[0] + (c0 - dsp.rev_clp[0]) * 0.48f);
            dsp.rev_clp[1] = sf_flush(dsp.rev_clp[1] + (c1 - dsp.rev_clp[1]) * 0.48f);
            dsp.rev_clp[2] = sf_flush(dsp.rev_clp[2] + (c2 - dsp.rev_clp[2]) * 0.48f);
            dsp.rev_clp[3] = sf_flush(dsp.rev_clp[3] + (c3 - dsp.rev_clp[3]) * 0.48f);
            float h0 = sf_flush((dsp.rev_clp[0] - dsp.rev_hpf_x[0]) + 0.984f * dsp.rev_hpf_y[0]);
            dsp.rev_hpf_x[0] = dsp.rev_clp[0]; dsp.rev_hpf_y[0] = h0;
            float h1 = sf_flush((dsp.rev_clp[1] - dsp.rev_hpf_x[1]) + 0.984f * dsp.rev_hpf_y[1]);
            dsp.rev_hpf_x[1] = dsp.rev_clp[1]; dsp.rev_hpf_y[1] = h1;
            float h2 = sf_flush((dsp.rev_clp[2] - dsp.rev_hpf_x[2]) + 0.984f * dsp.rev_hpf_y[2]);
            dsp.rev_hpf_x[2] = dsp.rev_clp[2]; dsp.rev_hpf_y[2] = h2;
            float h3 = sf_flush((dsp.rev_clp[3] - dsp.rev_hpf_x[3]) + 0.984f * dsp.rev_hpf_y[3]);
            dsp.rev_hpf_x[3] = dsp.rev_clp[3]; dsp.rev_hpf_y[3] = h3;
            rev_c0[dsp.rev_ci[0]] = (int16_t)(sf_clamp(mono + h0 * (rev_fb - rev_mod * 0.05f), -3.5f, 3.5f) * 8192.0f);
            rev_c1[dsp.rev_ci[1]] = (int16_t)(sf_clamp(mono + h1 * (rev_fb + rev_mod * 0.05f), -3.5f, 3.5f) * 8192.0f);
            rev_c2[dsp.rev_ci[2]] = (int16_t)(sf_clamp(mono + h2 * (rev_fb - rev_mod * 0.04f), -3.5f, 3.5f) * 8192.0f);
            rev_c3[dsp.rev_ci[3]] = (int16_t)(sf_clamp(mono + h3 * (rev_fb + rev_mod * 0.04f), -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ci[0] >= REV_C0) dsp.rev_ci[0] = 0u;
            if (++dsp.rev_ci[1] >= REV_C1) dsp.rev_ci[1] = 0u;
            if (++dsp.rev_ci[2] >= REV_C2) dsp.rev_ci[2] = 0u;
            if (++dsp.rev_ci[3] >= REV_C3) dsp.rev_ci[3] = 0u;

            float sum = (c0 + c1 + c2 + c3) * 0.25f;
            float a0v = (float)rev_a0[dsp.rev_ai[0]] * (1.0f / 8192.0f);
            float y0 = -sum + a0v;
            rev_a0[dsp.rev_ai[0]] = (int16_t)(sf_clamp(sum + a0v * 0.56f, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ai[0] >= REV_A0) dsp.rev_ai[0] = 0u;

            float a1v = (float)rev_a1[dsp.rev_ai[1]] * (1.0f / 8192.0f);
            float y1 = -y0 + a1v;
            rev_a1[dsp.rev_ai[1]] = (int16_t)(sf_clamp(y0 + a1v * 0.56f, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ai[1] >= REV_A1) dsp.rev_ai[1] = 0u;

            float rev_out_l = (y1 + (c0 - c1) * 0.28f) * 0.78f;
            float rev_out_r = (y1 + (c2 - c3) * 0.28f) * 0.78f;
            rev_peak += sf_fabs(rev_out_l) + sf_fabs(rev_out_r);
            xl += rev_out_l;
            xr += rev_out_r;
        }

        block_l[i] = xl;
        block_r[i] = xr;
    }

    /* Per-patch Tilt EQ & M/S Width stage (applied to synth bus before drums) */
    if (!dsp.jingle_active) {
        int32_t tilt_val = (int32_t)sf_tilt_eq(p);
        float tilt_g = (float)tilt_val * 0.085f;
        float w = (float)sf_ms_width_pct(p) * 0.01f;
        float w_trim = (w > 1.0f) ? (1.0f / (1.0f + (w - 1.0f) * 0.25f)) : 1.0f;
        if (tilt_val != 0 || sf_ms_width_pct(p) != 100u) {
            for (i = 0; i < n; i++) {
                float xl = block_l[i];
                float xr = block_r[i];
                if (tilt_val != 0) {
                    dsp.tilt_lp_l = sf_flush(dsp.tilt_lp_l + 0.120f * (xl - dsp.tilt_lp_l));
                    dsp.tilt_lp_r = sf_flush(dsp.tilt_lp_r + 0.120f * (xr - dsp.tilt_lp_r));
                    float hp_l = xl - dsp.tilt_lp_l;
                    float hp_r = xr - dsp.tilt_lp_r;
                    xl = sf_flush(xl + tilt_g * (hp_l - dsp.tilt_lp_l));
                    xr = sf_flush(xr + tilt_g * (hp_r - dsp.tilt_lp_r));
                }
                if (sf_ms_width_pct(p) != 100u) {
                    float m = 0.5f * (xl + xr);
                    float s = 0.5f * (xl - xr) * w * w_trim;
                    xl = m + s;
                    xr = m - s;
                }
                block_l[i] = xl;
                block_r[i] = xr;
            }
        }
    }

    /* Mix dry punchy drums AFTER synth FX chain so only synth engine goes to FX */
    render_drums(block_l, block_r, n);

    for (i = 0; i < n; i++) {
        float xl = block_l[i];
        float xr = block_r[i];

        if (dsp.click_amp > 0.001f) {
            float clk = sf_ram_sin_turns(dsp.click_ph) * dsp.click_amp;
            dsp.click_ph = fast_wrap01(dsp.click_ph + dsp.click_inc);
            dsp.click_amp *= 0.9972f;
            xl += clk;
            xr += clk;
        }

        /* 1-pole ~28 Hz DC & sub-rumble highpass before soft limiter */
        float hl = sf_flush((xl - dsp.dc_xl) + 0.9960f * dsp.dc_yl);
        float hr = sf_flush((xr - dsp.dc_xr) + 0.9960f * dsp.dc_yr);
        dsp.dc_xl = xl;
        dsp.dc_yl = hl;
        dsp.dc_xr = xr;
        dsp.dc_yr = hr;

        dsp.master_now += (master_target - dsp.master_now) * 0.005f;
        float out_trim = (cfg->audio_out_mode == 1u) ? 0.50f
                       : (cfg->audio_out_mode == 2u) ? 1.41f
                       : (cfg->hp_plugged == 0u ? 1.12f : 1.0f);
        float sl = sf_fast_tanh(hl * out_trim);
        float sr = sf_fast_tanh(hr * out_trim);
        float mono_out = (sl + sr) * 0.5f;
        sum_sq += mono_out * mono_out;
        if ((i & 1u) == 0u) {
            uint32_t si = i >> 1;
            if (si < 128u)
                dsp.vis.scope[si] = (int8_t)sf_clamp(mono_out * 110.0f, -120.0f, 120.0f);
        }

        lr[2u * i] = (int32_t)(sl * dsp.master_now * gain_scale);
        lr[2u * i + 1u] = (int32_t)(sr * dsp.master_now * gain_scale);
    }

    {
        float inv_n = n ? (0.5f / (float)n) : 0.0f;
        dsp.phas_energy = do_phas ? sf_flush(dsp.phas_energy * 0.85f + phas_peak * inv_n * 0.50f) : 0.0f;
        dsp.chor_energy = do_chor ? sf_flush(dsp.chor_energy * 0.85f + chor_peak * inv_n * 0.50f) : 0.0f;
        dsp.del_energy = do_del ? sf_flush(dsp.del_energy * 0.90f + del_peak * inv_n * 0.50f) : 0.0f;
        dsp.rev_energy = do_rev ? sf_flush(dsp.rev_energy * 0.90f + rev_peak * inv_n * 0.50f) : 0.0f;
        dsp.vis.del_lvl_pct = (uint8_t)sf_clamp(dsp.del_energy * 220.0f, 0.0f, 100.0f);
        dsp.vis.rev_lvl_pct = (uint8_t)sf_clamp(dsp.rev_energy * 220.0f, 0.0f, 100.0f);
    }

    /* Update live 32-band analyzer, ADSR tracer, and modulated dual-filter telemetry */
    {
        float rms_inst = sf_clamp(sf_ram_sqrtf(sum_sq / (float)(n ? n : 1u)) * 3.5f, 0.0f, 1.0f);
        dsp.vis_rms_f = dsp.vis_rms_f * 0.72f + rms_inst * 0.28f;
        dsp.vis.rms_pct = (uint8_t)(dsp.vis_rms_f * 100.0f);

        dsp.vis.mod_cut1 = (uint8_t)sf_clamp((float)p->f1_cut + vis_c1_oct * (127.0f / 8.287712f), 0.0f, 127.0f);
        dsp.vis.mod_cut2 = (uint8_t)sf_clamp((float)p->f2_cut + vis_c2_oct * (127.0f / 8.287712f), 0.0f, 127.0f);
        dsp.vis.mod_res1_pct = (uint8_t)sf_clamp(((float)p->f1_res * (1.0f / 127.0f) + mod_reso * 0.45f) * 100.0f, 0.0f, 100.0f);
        if (vis_v >= 0 && dsp.voices[vis_v].active) {
            va_voice_t *vv = &dsp.voices[vis_v];
            uint8_t ast = vv->amp_st;
            uint8_t fst = vv->flt_st;
            if (ast == ENV_DEC && sf_fabs(vv->amp_env - a_sus) < 0.015f) ast = ENV_SUS;
            if (fst == ENV_DEC && sf_fabs(vv->flt_env - f_sus) < 0.015f) fst = ENV_SUS;
            dsp.vis.amp_env_pct = (uint8_t)sf_clamp(vv->amp_env * 100.0f, 0.0f, 100.0f);
            dsp.vis.amp_st = ast;
            dsp.vis.flt_env_pct = (uint8_t)sf_clamp(vv->flt_env * 100.0f, 0.0f, 100.0f);
            dsp.vis.flt_st = fst;
        } else {
            dsp.vis.amp_env_pct = 0u;
            dsp.vis.amp_st = ENV_OFF;
            dsp.vis.flt_env_pct = 0u;
            dsp.vis.flt_st = ENV_OFF;
        }

        float c_norm = (float)dsp.vis.mod_cut1 * (1.0f / 127.0f);
        float r_norm = (float)dsp.vis.mod_res1_pct * 0.01f;
        for (i = 0; i < 32u; i++) {
            float bn = (float)i * (1.0f / 31.0f);
            float pass = (f1_type == SF_FLT_HP) ? (bn > c_norm ? 1.0f : 0.22f)
                       : (f1_type == SF_FLT_BP) ? (1.0f / (1.0f + sf_fabs(bn - c_norm) * 8.0f))
                       : (bn <= c_norm ? 1.0f : sf_clamp(1.0f - (bn - c_norm) * 2.5f, 0.12f, 1.0f));
            float peak = 1.0f + r_norm * 0.65f / (1.0f + sf_fabs(bn - c_norm) * 14.0f);
            float jit = 0.62f + 0.38f * sf_fabs((float)dsp.vis.scope[(i * 4u) & 127u] * (1.0f / 120.0f));
            float targ = sf_clamp(rms_inst * pass * peak * jit, 0.0f, 1.0f);
            dsp.vis_bands_f[i] = dsp.vis_bands_f[i] * 0.68f + targ * 0.32f;
            dsp.vis.bands[i] = (uint8_t)(dsp.vis_bands_f[i] * 100.0f);
        }
    }
}
