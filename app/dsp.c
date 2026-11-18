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

static PLAT_RAM_CODE float sf_ram_sin_turns(float ph)
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
    return sf_clamp(45.0f * sf_ram_exp2f(norm * 8.68f), 45.0f, 19000.0f);
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
    if (c->type == SF_FLT_BP) return v1;
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
    float last_l, last_r;
    float click_l, click_r;
    svf_t f1l;
    svf_t f2l, f2r;
} va_voice_t;

/* ----------------------------------------------------------- Drum voices --- */
typedef struct {
    uint8_t active;
    float ph1, ph2;
    float env, pitch_env;
    float level;
    float tone, tune, decay;
    svf_t bp;
} drum_voice_t;

/* ----------------------------------------------------------- FX pools --- */
#define DEL_LEN 16384u
#define CHOR_LEN 2048u
#define REV_C0 1553u
#define REV_C1 1617u
#define REV_C2 1491u
#define REV_C3 1422u
#define REV_A0 225u
#define REV_A1 556u

static int16_t del_buf[DEL_LEN * 2u] PLAT_POOL;
static float chor_l[CHOR_LEN] PLAT_POOL;
static float chor_r[CHOR_LEN] PLAT_POOL;
static int16_t rev_c0[REV_C0] PLAT_POOL;
static int16_t rev_c1[REV_C1] PLAT_POOL;
static int16_t rev_c2[REV_C2] PLAT_POOL;
static int16_t rev_c3[REV_C3] PLAT_POOL;
static int16_t rev_a0[REV_A0] PLAT_POOL;
static int16_t rev_a1[REV_A1] PLAT_POOL;

static const uint8_t J_NOTES[6][5] = {
    {60u, 64u, 67u, 71u, 74u},
    {62u, 67u, 69u, 74u, 79u},
    {64u, 67u, 69u, 71u, 76u},
    {60u, 67u, 65u, 64u, 72u},
    {57u, 64u, 69u, 71u, 76u},
    {60u, 62u, 64u, 67u, 72u},
};
static const uint16_t J_STEP_MS[6] = {130u, 125u, 120u, 125u, 135u, 110u};

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
    float master_now;
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
} dsp;

void sf_dsp_init(sf_engine_cfg_t *cfg)
{
    uint32_t i;
    dsp.cfg = cfg;
    dsp.last_note = -1;
    dsp.last_pitch_note = 60.0f;
    dsp.voice_age = 1u;
    dsp.master_now = 0.0f;
    dsp.jingle_active = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        dsp.voices[i].active = 0u;
        dsp.voices[i].click_rem = 0u;
        dsp.voices[i].last_l = dsp.voices[i].last_r = 0.0f;
    }
    for (i = 0; i < SF_DRUM_VOICES; i++)
        dsp.drums[i].active = 0u;
    for (i = 0; i < 32u; i++) {
        dsp.vis.bands[i] = 0u;
        dsp.vis_bands_f[i] = 0.0f;
    }
    for (i = 0; i < 128u; i++)
        dsp.vis.scope[i] = 0;
}

void sf_dsp_set_cfg(sf_engine_cfg_t *cfg)
{
    dsp.cfg = cfg;
}

void sf_dsp_stop_jingle(void)
{
    uint32_t i;
    if (!dsp.jingle_active)
        return;
    dsp.jingle_active = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].is_jingle) {
            dsp.voices[i].gate = 0u;
            dsp.voices[i].amp_st = ENV_REL;
            dsp.voices[i].flt_st = ENV_REL;
        }
    }
}

void sf_dsp_panic(void)
{
    uint32_t i;
    sf_dsp_stop_jingle();
    for (i = 0; i < SF_MAX_VOICES; i++) {
        dsp.voices[i].active = 0u;
        dsp.voices[i].gate = 0u;
        dsp.voices[i].amp_env = 0.0f;
        dsp.voices[i].amp_st = ENV_OFF;
        dsp.voices[i].flt_env = 0.0f;
        dsp.voices[i].flt_st = ENV_OFF;
        dsp.voices[i].click_rem = 0u;
        dsp.voices[i].last_l = dsp.voices[i].last_r = 0.0f;
    }
    for (i = 0; i < SF_DRUM_VOICES; i++)
        dsp.drums[i].active = 0u;
    dsp.last_note = -1;
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
    if (is_jingle) max_poly = SF_MAX_VOICES;

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
        v->amp_env = 0.0f;
        v->flt_env = 0.0f;
        v->f1l.ic1 = v->f1l.ic2 = 0.0f;
        v->f2l.ic1 = v->f2l.ic2 = 0.0f;
        v->f2r.ic1 = v->f2r.ic2 = 0.0f;
    } else if (!v->active) {
        v->click_rem = 0u;
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
    dsp.last_pitch_note = (float)note;
    if (!is_jingle)
        dsp.last_note = (int32_t)note;
    dsp.vis.note_trig_seq++;
}

void sf_dsp_note_on(uint8_t note, uint8_t vel)
{
    PLAT_FAR(internal_note_on)(note, vel, 0u);
}

void sf_dsp_note_off(uint8_t note)
{
    PLAT_FAR(internal_note_off)(note);
}

void sf_dsp_aftertouch(uint8_t note, uint8_t prs)
{
    uint32_t i;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (dsp.voices[i].active && dsp.voices[i].note == (int8_t)note)
            dsp.voices[i].prs = prs;
    }
}

void sf_dsp_trigger_drum(uint8_t voice, uint8_t vel)
{
    drum_voice_t *d;
    float tnorm = 1.0f, dnorm = 1.0f, tonen = 0.6f, lnorm = 0.9f;
    if (voice >= SF_DRUM_VOICES)
        return;
    if (dsp.cfg && ((dsp.cfg->drum_mute >> voice) & 1u))
        return;
    if (dsp.cfg) {
        tnorm = fm_exp2f((float)dsp.cfg->drum_tune[voice] * (1.0f / 12.0f));
        dnorm = fm_clampf((float)dsp.cfg->drum_decay[voice] * 0.01f, 0.25f, 2.0f);
        tonen = fm_clampf((float)dsp.cfg->drum_tone[voice] * 0.01f, 0.0f, 1.0f);
        lnorm = fm_clampf((float)dsp.cfg->drum_level[voice] * 0.01f, 0.0f, 1.0f);
    }
    d = &dsp.drums[voice];
    d->active = 1u;
    d->ph1 = 0.0f;
    d->ph2 = 0.0f;
    d->env = 1.0f;
    d->pitch_env = 1.0f;
    d->level = ((float)vel * (1.0f / 127.0f)) * lnorm;
    d->tune = tnorm;
    d->decay = dnorm;
    d->tone = tonen;
    d->bp.ic1 = d->bp.ic2 = 0.0f;
    dsp.vis.last_drum_idx = voice;
    dsp.vis.drum_trig_seq++;
}

int32_t sf_dsp_last_note(void) { return dsp.last_note; }

uint32_t sf_dsp_active_voices(void)
{
    uint32_t i, cnt = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++)
        if (dsp.voices[i].active)
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
static PLAT_RAM_CODE void render_drums(float *out_l, float *out_r, float *rev_send, uint32_t n)
{
    uint32_t v, i;
    uint8_t kit = dsp.cfg ? dsp.cfg->drum_kit : 0u;
    uint8_t bus = dsp.cfg ? dsp.cfg->drum_bus : 0u;
    float drum_gain = dsp.cfg ? ((float)dsp.cfg->drum_vol * 0.01f) * 0.58f : 0.45f;
    static const float KIT_BASE_KICK[SF_DRUM_KITS] = {
        46.0f, 54.0f, 62.0f, 52.0f, 56.0f, 50.0f, 60.0f, 48.0f, 49.0f, 43.0f
    };
    static const float KIT_CRISP[SF_DRUM_KITS] = {
        0.92f, 1.32f, 0.85f, 1.18f, 1.25f, 1.12f, 0.95f, 1.55f, 0.78f, 1.22f
    };
    float kbase = KIT_BASE_KICK[kit % SF_DRUM_KITS];
    float crisp = KIT_CRISP[kit % SF_DRUM_KITS];
    float room_amt = (bus == SF_BUS_ROOM) ? 0.36f : 0.06f;

    for (v = 0; v < SF_DRUM_VOICES; v++) {
        drum_voice_t *d = &dsp.drums[v];
        float dec_amp, dec_p, tone_scale, dg_lvl;
        svf_c_t dc;
        if (!d->active)
            continue;
        dec_amp = (v == 0u) ? (1.0f - ((kit == 0u || kit == 9u) ? 0.00012f : 0.00019f) / d->decay)
                : (v == 1u) ? (1.0f - 0.00045f / d->decay)
                : (v == 2u) ? (1.0f - 0.0022f / d->decay)
                : (v == 3u) ? (1.0f - 0.00038f / d->decay)
                : (v == 4u) ? (1.0f - 0.00052f / d->decay)
                : (v == 5u) ? (1.0f - 0.00042f / d->decay)
                : (v == 6u) ? (1.0f - 0.00022f / d->decay)
                : (1.0f - 0.0024f / d->decay);
        dec_p = 1.0f - 0.0026f;
        tone_scale = 0.55f + d->tone * 0.9f;
        dg_lvl = d->level * drum_gain;

        /* Precompute drum filter coefficients once per block */
        dc.a1 = 0.0f; dc.a2 = 0.0f; dc.a3 = 0.0f; dc.k = 0.5f; dc.type = SF_FLT_HP;
        if (v == 2u || v == 3u) {
            float f = ((v == 2u ? 6800.0f : 6100.0f) * tone_scale) * d->tune;
            svf_prep(&dc, sf_clamp(3.14159f * f * INV_SR, 0.02f, 0.85f), 0.45f, SF_FLT_HP);
        } else if (v == 4u) {
            float g = sf_clamp(3.14159f * (1350.0f + d->tone * 900.0f) * d->tune * INV_SR, 0.02f, 0.7f);
            svf_prep(&dc, g, 0.52f, SF_FLT_BP);
        } else if (v == 6u) {
            float g = sf_clamp(3.14159f * (4800.0f + d->tone * 1800.0f) * d->tune * INV_SR, 0.02f, 0.85f);
            svf_prep(&dc, g, 0.4f, SF_FLT_HP);
        }

        float pan = (v == 2u) ? -0.18f : (v == 3u) ? 0.18f : (v == 4u) ? -0.12f : (v == 5u) ? 0.22f : (v == 6u) ? 0.28f : (v == 7u) ? -0.25f : 0.0f;
        float pl_g = 1.0f - pan, pr_g = 1.0f + pan;

        for (i = 0; i < n; i++) {
            float s = 0.0f;
            float nse = fast_noise();
            if (v == 0u) {
                float sub_b = (bus == SF_BUS_SUB) ? 1.25f : 1.0f;
                float hz = (kbase + 148.0f * d->pitch_env * crisp * tone_scale) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                s = sf_ram_sin_turns(d->ph1) * d->env * 1.15f * sub_b;
                if (kit == 1u || kit == 4u || kit == 9u || bus == SF_BUS_WARM)
                    s = sf_soft_clip(s * 1.35f);
            } else if (v == 1u) {
                float hz = (175.0f + 115.0f * d->pitch_env) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                s = (sf_ram_sin_turns(d->ph1) * 0.48f + nse * 0.65f * crisp * tone_scale) * d->env;
            } else if (v == 2u || v == 3u) {
                d->ph1 = fast_wrap01(d->ph1 + (330.0f * d->tune) * INV_SR);
                d->ph2 = fast_wrap01(d->ph2 + (540.0f * d->tune) * INV_SR);
                float sq = ((d->ph1 < 0.5f ? 1.0f : -1.0f) + (d->ph2 < 0.5f ? 1.0f : -1.0f)) * 0.5f;
                s = svf_tick_c(&d->bp, nse * 0.82f + sq * 0.35f, &dc) * d->env * 0.62f;
            } else if (v == 4u) {
                float burst = d->pitch_env > 0.72f ? ((uint32_t)(d->pitch_env * 40.0f) & 1u ? 1.0f : 0.25f) : 1.0f;
                s = svf_tick_c(&d->bp, nse, &dc) * d->env * burst * 0.92f;
            } else if (v == 5u) {
                float hz = (112.0f + 165.0f * d->pitch_env * tone_scale) * d->tune;
                d->ph1 = fast_wrap01(d->ph1 + hz * INV_SR);
                s = sf_ram_sin_turns(d->ph1) * d->env * 0.85f;
            } else if (v == 6u) {
                d->ph1 = fast_wrap01(d->ph1 + (420.0f * d->tune) * INV_SR);
                d->ph2 = fast_wrap01(d->ph2 + (1170.0f * d->tune) * INV_SR);
                float fm = sf_ram_sin_turns(d->ph1 + 0.65f * sf_ram_sin_turns(d->ph2));
                s = svf_tick_c(&d->bp, nse * 0.62f + fm * 0.45f, &dc) * d->env * 0.52f;
            } else {
                d->ph1 = fast_wrap01(d->ph1 + (480.0f * d->tune) * INV_SR);
                d->ph2 = fast_wrap01(d->ph2 + (1680.0f * d->tune) * INV_SR);
                s = (sf_ram_sin_turns(d->ph1) * 0.55f + sf_ram_sin_turns(d->ph2) * 0.45f * tone_scale) * d->env;
            }

            if (kit == 8u)
                s = (float)(int32_t)(s * 14.0f) * (1.0f / 14.0f);
            if (bus == SF_BUS_COMP)
                s = sf_soft_clip(s * 1.48f) * 0.80f;
            else if (bus == SF_BUS_WARM)
                s = sf_soft_clip(s * 1.15f) * 0.92f;
            else if (bus == SF_BUS_SPARK && v >= 1u)
                s *= 1.15f;

            s *= dg_lvl;
            out_l[i] += s * pl_g;
            out_r[i] += s * pr_g;
            if (rev_send && v > 0u)
                rev_send[i] += s * room_amt;

            d->env *= dec_amp;
            d->pitch_env *= dec_p;
        }
        if (d->env < 0.001f)
            d->active = 0u;
    }
}

/* --------------------------------------------------- Main render block --- */
static float block_l[256] PLAT_POOL;
static float block_r[256] PLAT_POOL;
static float drum_rev[256] PLAT_POOL;

static PLAT_RAM_CODE void step_boot_jingle(uint32_t n)
{
    uint32_t step_samp = ((uint32_t)J_STEP_MS[dsp.jingle_idx] * 44100u) / 1000u;
    uint32_t i;
    while (dsp.jingle_step < 5u && dsp.jingle_sample_pos >= dsp.jingle_step * step_samp) {
        uint8_t s = dsp.jingle_step;
        uint8_t note = J_NOTES[dsp.jingle_idx][s];
        uint8_t vel = (s == 4u) ? 84u : 72u;
        uint32_t hold_samp = (s == 4u) ? (step_samp * 24u) / 10u : (step_samp * 88u) / 100u;
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
        drum_rev[i] = 0.0f;
    }
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
    float q1 = sf_clamp(byte_to_reso_q(p->f1_res) + mod_reso * 5.0f, 0.5f, 18.0f);
    float k1 = 1.0f / q1;
    float q2 = sf_clamp(byte_to_reso_q(p->f2_res), 0.5f, 18.0f);
    float k2 = 1.0f / q2;
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
    float trem = sf_clamp(1.0f + mod_amp * 0.45f, 0.1f, 1.3f);
    float uni_pan_norm = (float)p->uni_pan * 0.01f;
    float glide_coeff = (p->glide == 0u) ? 1.0f : sf_clamp(1.0f - (float)p->glide * 0.0108f, 0.03f, 0.9f);
    uint8_t lite_hyper = (active_cnt >= 5u) ? 1u : 0u;

    for (v = 0; v < SF_MAX_VOICES; v++) {
        va_voice_t *vc = &dsp.voices[v];
        svf_c_t c1, c2;
        if (!vc->active)
            continue;

        vc->pitch_cur += ((float)vc->note - vc->pitch_cur) * glide_coeff;
        float note_f = vc->pitch_cur + (float)p->oct * 12.0f + mod_pitch * 1.5f;
        float f0 = 440.0f * sf_ram_exp2f((note_f - 69.0f) * (1.0f / 12.0f));
        float dt1 = sf_clamp(f0 * INV_SR, 0.0001f, 0.45f);
        float dt2 = sf_clamp(f0 * osc2_ratio * INV_SR, 0.0001f, 0.45f);
        float dt_sub = dt1 * 0.5f;

        float kf_oct = ((vc->pitch_cur - 60.0f) * (1.0f / 12.0f)) * kf_amt;
        float cut_scale = sf_ram_exp2f(mod_cut1 * 2.5f + (vc->flt_env * fenv_oct) + kf_oct + ((float)vc->prs * (1.0f / 127.0f)) * 1.4f);
        float cut1_hz = sf_clamp(cut1_base * cut_scale, 45.0f, 18500.0f);
        float g1 = sf_clamp(3.14159f * cut1_hz * INV_SR, 0.003f, 0.92f);

        float cut2_hz = sf_clamp(cut2_base * sf_ram_exp2f(mod_cut2 * 2.5f + kf_oct * 0.5f), 45.0f, 18500.0f);
        float g2 = sf_clamp(3.14159f * cut2_hz * INV_SR, 0.003f, 0.92f);

        svf_prep(&c1, g1, k1, f1_type);
        svf_prep(&c2, g2, k2, f2_type);

        float vel_g = (0.35f + 0.65f * ((float)vc->vel * (1.0f / 127.0f))) * 0.22f * trem;
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
                if (vc->amp_env < 0.0005f) {
                    vc->active = 0u;
                    vc->last_l = vc->last_r = 0.0f;
                    break;
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
                float s0 = 2.0f * vc->phase1 - 1.0f - polyblep(vc->phase1, dt1);
                vc->hyper_ph[0] = fast_wrap01(vc->hyper_ph[0] + dth0);
                vc->hyper_ph[1] = fast_wrap01(vc->hyper_ph[1] + dth1);
                if (lite_hyper) {
                    o1 = (s0 + (2.0f * vc->hyper_ph[0] - 1.0f) + (2.0f * vc->hyper_ph[1] - 1.0f)) * 0.45f;
                } else {
                    vc->hyper_ph[2] = fast_wrap01(vc->hyper_ph[2] + dth2);
                    vc->hyper_ph[3] = fast_wrap01(vc->hyper_ph[3] + dth3);
                    o1 = (s0 + (2.0f * vc->hyper_ph[0] - 1.0f) + (2.0f * vc->hyper_ph[1] - 1.0f)
                          + (2.0f * vc->hyper_ph[2] - 1.0f) + (2.0f * vc->hyper_ph[3] - 1.0f)) * 0.34f;
                }
            } else if (osc_mode == SF_OSC_PULSE) {
                o1 = vc->phase1 < pw ? 1.0f : -1.0f;
                o1 += polyblep(vc->phase1, dt1);
                o1 -= polyblep(fast_wrap01(vc->phase1 + (1.0f - pw)), dt1);
            } else if (osc_mode == SF_OSC_TRI) {
                o1 = 1.0f - 4.0f * sf_fabs(vc->phase1 - 0.5f);
            } else if (osc_mode == SF_OSC_WT) {
                float s1 = sf_ram_sin_turns(vc->phase1);
                float ph2 = fast_wrap01(vc->phase1 * (2.0f + wt_pos * 2.0f));
                float s2 = sf_ram_sin_turns(ph2);
                float saw = 2.0f * vc->phase1 - 1.0f;
                o1 = s1 * (1.0f - wt_pos) + (s2 * 0.65f + saw * 0.35f) * wt_pos;
            } else {
                o1 = 2.0f * vc->phase1 - 1.0f - polyblep(vc->phase1, dt1);
            }

            if (unison > 1u && osc_mode != SF_OSC_HYPERSAW) {
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
                o2 = 2.0f * vc->phase2 - 1.0f - polyblep(vc->phase2, dt2);

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
                float w1 = 1.0f - f_bal * 0.5f;
                float w2 = 0.5f + f_bal * 0.5f;
                sig_l = (fl * w1 + fr * 0.28f * w2) * pan_l;
                sig_r = (fl * 0.28f * w1 + fr * w2) * pan_r;
            } else if (routing == SF_ROUTE_PAR) {
                float comb = (src1 + src2) * 0.7f;
                float fl = svf_tick_c(&vc->f1l, comb, &c1);
                float fr = svf_tick_c(&vc->f2r, comb, &c2);
                float mixf = fl * (1.0f - f_bal * 0.6f) + fr * (0.4f + f_bal * 0.6f);
                sig_l = mixf * 0.72f * pan_l;
                sig_r = mixf * 0.72f * pan_r;
            } else {
                float comb = (src1 + src2) * 0.7f;
                float f1 = svf_tick_c(&vc->f1l, comb, &c1);
                float f2 = svf_tick_c(&vc->f2l, f1, &c2);
                float mout = f1 * (1.0f - f_bal) + f2 * f_bal;
                sig_l = mout * pan_l;
                sig_r = mout * pan_r;
            }

            float g = vc->amp_env * vel_g;
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

    render_drums(block_l, block_r, drum_rev, n);

    float chor_mix = sf_clamp((float)p->fx_chorus * 0.01f
        + (l1d == SF_LFO_CHORUS ? lfo1 * 0.4f : 0.0f) + (l2d == SF_LFO_CHORUS ? lfo2 * 0.3f : 0.0f), 0.0f, 1.0f);
    float phas_mix = sf_clamp((float)p->fx_phaser * 0.01f
        + (l1d == SF_LFO_PHASER ? lfo1 * 0.4f : 0.0f) + (l2d == SF_LFO_PHASER ? lfo2 * 0.3f : 0.0f), 0.0f, 1.0f);
    float phas_hz = sf_clamp((float)p->fx_phaser_rate * 0.1f, 0.1f, 6.0f);
    float del_fb = sf_clamp((float)p->fx_del_fb * 0.01f, 0.0f, 0.85f);
    float del_mix = sf_clamp((float)p->fx_del_mix * 0.0082f
        + (l1d == SF_LFO_DELAY ? lfo1 * 0.35f : 0.0f) + (l2d == SF_LFO_DELAY ? lfo2 * 0.25f : 0.0f), 0.0f, 0.82f);
    uint32_t del_tap = 800u + (uint32_t)p->fx_del_time * 152u;
    if (del_tap >= DEL_LEN - 4u)
        del_tap = DEL_LEN - 4u;
    float rev_mix = sf_clamp((float)p->fx_reverb * 0.0072f
        + (l1d == SF_LFO_REVERB ? lfo1 * 0.35f : 0.0f) + (l2d == SF_LFO_REVERB ? lfo2 * 0.25f : 0.0f), 0.0f, 0.78f);

    float phas_a = -0.48f + 0.38f * sf_ram_sin_turns(dsp.phas_ph);
    float phas_inc = phas_hz * INV_SR;
    float chor_m = sf_ram_sin_turns(dsp.chor_ph);
    uint8_t do_phas = (phas_mix > 0.02f) ? 1u : 0u;
    uint8_t do_chor = (chor_mix > 0.02f) ? 1u : 0u;
    uint8_t do_rev = (rev_mix > 0.02f || cfg->drum_bus == SF_BUS_ROOM) ? 1u : 0u;
    float sum_sq = 0.0f;

    for (i = 0; i < n; i++) {
        float xl = block_l[i];
        float xr = block_r[i];

        if (do_phas) {
            if ((i & 7u) == 0u) {
                dsp.phas_ph = fast_wrap01(dsp.phas_ph + phas_inc * 8.0f);
                phas_a = -0.48f + 0.38f * sf_ram_sin_turns(dsp.phas_ph);
            }
            float pl = xl, pr = xr;
            uint32_t st;
            for (st = 0; st < 4u; st++) {
                float yl = phas_a * pl + dsp.phas_st[st][0];
                dsp.phas_st[st][0] = pl - phas_a * yl;
                pl = yl;
                float yr = phas_a * pr + dsp.phas_st[st][1];
                dsp.phas_st[st][1] = pr - phas_a * yr;
                pr = yr;
            }
            xl = xl * (1.0f - phas_mix * 0.45f) + pl * phas_mix * 0.55f;
            xr = xr * (1.0f - phas_mix * 0.45f) + pr * phas_mix * 0.55f;
        }

        chor_l[dsp.chor_wr] = xl;
        chor_r[dsp.chor_wr] = xr;
        if (do_chor) {
            if ((i & 15u) == 0u) {
                dsp.chor_ph = fast_wrap01(dsp.chor_ph + 0.48f * INV_SR * 16.0f);
                chor_m = sf_ram_sin_turns(dsp.chor_ph);
            }
            uint32_t d1 = (dsp.chor_wr - (uint32_t)(280.0f + 160.0f * chor_m)) & (CHOR_LEN - 1u);
            uint32_t d2 = (dsp.chor_wr - (uint32_t)(280.0f - 160.0f * chor_m)) & (CHOR_LEN - 1u);
            xl += chor_l[d1] * chor_mix * 0.48f;
            xr += chor_r[d2] * chor_mix * 0.48f;
        }
        dsp.chor_wr = (dsp.chor_wr + 1u) & (CHOR_LEN - 1u);

        uint32_t rd_l = (dsp.del_wr - del_tap) & (DEL_LEN - 1u);
        uint32_t rd_r = (dsp.del_wr - ((del_tap * 3u) >> 2)) & (DEL_LEN - 1u);
        float dl = (float)del_buf[rd_l * 2u] * (1.0f / 8192.0f);
        float dr = (float)del_buf[rd_r * 2u + 1u] * (1.0f / 8192.0f);
        float wl = sf_clamp(xl + dr * del_fb, -3.5f, 3.5f);
        float wr = sf_clamp(xr + dl * del_fb, -3.5f, 3.5f);
        del_buf[dsp.del_wr * 2u] = (int16_t)(wl * 8192.0f);
        del_buf[dsp.del_wr * 2u + 1u] = (int16_t)(wr * 8192.0f);
        dsp.del_wr = (dsp.del_wr + 1u) & (DEL_LEN - 1u);
        xl += dl * del_mix;
        xr += dr * del_mix;

        if (do_rev) {
            float mono = ((xl + xr) * 0.5f) * rev_mix + drum_rev[i];
            float c0 = (float)rev_c0[dsp.rev_ci[0]] * (1.0f / 8192.0f);
            float c1 = (float)rev_c1[dsp.rev_ci[1]] * (1.0f / 8192.0f);
            float c2 = (float)rev_c2[dsp.rev_ci[2]] * (1.0f / 8192.0f);
            float c3 = (float)rev_c3[dsp.rev_ci[3]] * (1.0f / 8192.0f);
            rev_c0[dsp.rev_ci[0]] = (int16_t)(sf_clamp(mono + c0 * 0.78f, -3.5f, 3.5f) * 8192.0f);
            rev_c1[dsp.rev_ci[1]] = (int16_t)(sf_clamp(mono + c1 * 0.78f, -3.5f, 3.5f) * 8192.0f);
            rev_c2[dsp.rev_ci[2]] = (int16_t)(sf_clamp(mono + c2 * 0.78f, -3.5f, 3.5f) * 8192.0f);
            rev_c3[dsp.rev_ci[3]] = (int16_t)(sf_clamp(mono + c3 * 0.78f, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ci[0] >= REV_C0) dsp.rev_ci[0] = 0u;
            if (++dsp.rev_ci[1] >= REV_C1) dsp.rev_ci[1] = 0u;
            if (++dsp.rev_ci[2] >= REV_C2) dsp.rev_ci[2] = 0u;
            if (++dsp.rev_ci[3] >= REV_C3) dsp.rev_ci[3] = 0u;

            float sum = (c0 + c1 + c2 + c3) * 0.25f;
            float a0v = (float)rev_a0[dsp.rev_ai[0]] * (1.0f / 8192.0f);
            float y0 = -sum + a0v;
            rev_a0[dsp.rev_ai[0]] = (int16_t)(sf_clamp(sum + a0v * 0.5f, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ai[0] >= REV_A0) dsp.rev_ai[0] = 0u;

            float a1v = (float)rev_a1[dsp.rev_ai[1]] * (1.0f / 8192.0f);
            float y1 = -y0 + a1v;
            rev_a1[dsp.rev_ai[1]] = (int16_t)(sf_clamp(y0 + a1v * 0.5f, -3.5f, 3.5f) * 8192.0f);
            if (++dsp.rev_ai[1] >= REV_A1) dsp.rev_ai[1] = 0u;

            xl += y1 * 0.85f;
            xr += y1 * 0.78f;
        }

        dsp.master_now += (master_target - dsp.master_now) * 0.005f;
        float sl = sf_fast_tanh(xl);
        float sr = sf_fast_tanh(xr);
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

    /* Update live 32-band analyzer (0..100 scale) & RMS once per audio block */
    {
        float rms_inst = sf_clamp(sf_ram_sqrtf(sum_sq / (float)(n ? n : 1u)) * 3.5f, 0.0f, 1.0f);
        dsp.vis_rms_f = dsp.vis_rms_f * 0.72f + rms_inst * 0.28f;
        dsp.vis.rms_pct = (uint8_t)(dsp.vis_rms_f * 100.0f);

        float c_norm = (float)p->f1_cut * (1.0f / 127.0f);
        float r_norm = (float)p->f1_res * (1.0f / 127.0f);
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
