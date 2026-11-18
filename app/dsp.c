/* SPDX-License-Identifier: MPL-2.0 */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "dsp.h"

#define INV_SR (1.0f / 44100.0f)

/* ----------------------------------------------------------- PRNG & helpers --- */
static uint32_t rng_state = 0x13572468u;
static inline float fast_noise(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return (float)((int32_t)x >> 8) * (1.0f / 8388608.0f);
}

static inline float polyblep(float t, float dt)
{
    if (dt <= 0.0f || dt >= 0.5f)
        return 0.0f;
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

static inline float wrap01(float x)
{
    int32_t k = (int32_t)x;
    x -= (float)k;
    if (x < 0.0f)
        x += 1.0f;
    return x;
}

static inline float byte_to_hz(uint8_t b)
{
    float norm = (float)b * (1.0f / 127.0f);
    return fm_clampf(45.0f * fm_exp2f(norm * 8.68f), 45.0f, 19000.0f);
}

static inline float byte_to_reso_q(uint8_t b)
{
    float norm = (float)b * (1.0f / 127.0f);
    return 0.7f + norm * fm_sqrtf(norm) * 15.5f;
}

static inline float byte_to_env_sec(uint8_t b, float max_s, float min_s)
{
    float norm = (float)b * (1.0f / 127.0f);
    return min_s + norm * norm * max_s;
}

/* ----------------------------------------------------------- SVF filter --- */
typedef struct {
    float ic1, ic2;
} svf_t;

static inline float svf_tick(svf_t *s, float in, float g, float k, uint8_t type)
{
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float v3 = in - s->ic2;
    float v1 = a1 * s->ic1 + a2 * v3;
    float v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
    s->ic1 = fm_flush(2.0f * v1 - s->ic1);
    s->ic2 = fm_flush(2.0f * v2 - s->ic2);
    if (type == SF_FLT_BP)
        return v1;
    if (type == SF_FLT_HP)
        return in - k * v1 - v2;
    if (type == SF_FLT_NOTCH)
        return in - k * v1;
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
    uint32_t age;
    float phase1;
    float phase2;
    float phase_sub;
    float hyper_ph[4];
    float amp_env;
    uint8_t amp_st;
    float flt_env;
    uint8_t flt_st;
    float pitch_cur;
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

/* 6 Boot Jingle Melodies matching BOOT_JINGLES in Fm1BasePanel.tsx */
static const uint8_t J_NOTES[6][5] = {
    {60u, 64u, 67u, 71u, 74u}, /* CORAL MAJ9 */
    {62u, 67u, 69u, 74u, 79u}, /* LUNAR FIFTHS */
    {64u, 67u, 69u, 71u, 76u}, /* LAGOON WAVE */
    {60u, 67u, 65u, 64u, 72u}, /* STAR HOP */
    {57u, 64u, 69u, 71u, 76u}, /* REEF DREAM */
    {60u, 62u, 64u, 67u, 72u}, /* TURBO BOOT */
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
    /* Boot Jingle player state */
    uint8_t jingle_active;
    uint8_t jingle_idx;
    uint8_t jingle_step;
    uint32_t jingle_sample_pos;
    uint32_t jingle_rel_at[5];
    uint8_t jingle_rel_done[5];
    sf_preset_t jingle_patch;
    /* Lock-free OLED visualization state */
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
    for (i = 0; i < SF_MAX_VOICES; i++)
        dsp.voices[i].active = 0u;
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
        /* Sanitize preset envelope & cutoff for a crisp musical welcome jingle */
        if (dsp.jingle_patch.a_atk > 18u) dsp.jingle_patch.a_atk = 12u;
        if (dsp.jingle_patch.a_dec > 46u) dsp.jingle_patch.a_dec = 42u;
        if (dsp.jingle_patch.a_sus < 45u) dsp.jingle_patch.a_sus = 68u;
        if (dsp.jingle_patch.a_rel > 46u) dsp.jingle_patch.a_rel = 40u;
        if (dsp.jingle_patch.f_atk > 18u) dsp.jingle_patch.f_atk = 12u;
        if (sf_f1_type(&dsp.jingle_patch) == SF_FLT_LP && dsp.jingle_patch.f1_cut < 72u)
            dsp.jingle_patch.f1_cut = 78u;
        dsp.jingle_patch.glide = 0u;
    }
    dsp.jingle_active = 1u;
    plat_unlock(f);
}

static uint32_t alloc_voice(int8_t note, uint8_t max_poly)
{
    uint32_t i, best = 0u, active_cnt = 0u;
    uint32_t best_age = 0xFFFFFFFFu;
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
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (!dsp.voices[i].active)
            continue;
        if (!dsp.voices[i].gate && dsp.voices[i].amp_env < 0.18f)
            return i;
        if (dsp.voices[i].age < best_age) {
            best_age = dsp.voices[i].age;
            best = i;
        }
    }
    return best;
}

static void internal_note_on(uint8_t note, uint8_t vel, uint8_t is_jingle)
{
    uint32_t idx;
    va_voice_t *v;
    uint8_t max_poly = dsp.cfg ? sf_polyphony(&dsp.cfg->patch) : SF_MAX_VOICES;
    if (max_poly < 1u) max_poly = 1u;
    if (max_poly > SF_MAX_VOICES) max_poly = SF_MAX_VOICES;
    if (is_jingle) max_poly = SF_MAX_VOICES;

    if (!vel) {
        sf_dsp_note_off(note);
        return;
    }
    idx = alloc_voice((int8_t)note, max_poly);
    v = &dsp.voices[idx];
    if (!v->active) {
        v->phase1 = 0.0f;
        v->phase2 = 0.25f;
        v->phase_sub = 0.0f;
        v->hyper_ph[0] = 0.13f;
        v->hyper_ph[1] = 0.37f;
        v->hyper_ph[2] = 0.61f;
        v->hyper_ph[3] = 0.83f;
        v->pitch_cur = (dsp.cfg && dsp.cfg->patch.glide > 0u && !is_jingle) ? dsp.last_pitch_note : (float)note;
        v->f1l.ic1 = v->f1l.ic2 = 0.0f;
        v->f2l.ic1 = v->f2l.ic2 = 0.0f;
        v->f2r.ic1 = v->f2r.ic2 = 0.0f;
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
    internal_note_on(note, vel, 0u);
}

void sf_dsp_note_off(uint8_t note)
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
static float eval_lfo(float ph, uint8_t shape)
{
    if (shape == 1u)
        return ph < 0.5f ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
    if (shape == 2u)
        return 2.0f * ph - 1.0f;
    if (shape == 3u)
        return ph < 0.5f ? 1.0f : -1.0f;
    return fm_sin_turns(ph);
}

/* ----------------------------------------------------- Drum synthesis --- */
static void render_drums(float *out_l, float *out_r, float *rev_send, uint32_t n)
{
    uint32_t v, i;
    uint8_t kit = dsp.cfg ? dsp.cfg->drum_kit : 0u;
    uint8_t bus = dsp.cfg ? dsp.cfg->drum_bus : 0u;
    float drum_gain = dsp.cfg ? ((float)dsp.cfg->drum_vol * 0.01f) * 0.58f : 0.45f;
    /* 10 iconic drum kits: base kick Hz, brightness, sub punch */
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
        float dec_amp, dec_p, tone_scale;
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

        for (i = 0; i < n; i++) {
            float s = 0.0f, pan = 0.0f;
            float nse = fast_noise();
            if (v == 0u) {
                /* Kick */
                float sub_b = (bus == SF_BUS_SUB) ? 1.25f : 1.0f;
                float hz = (kbase + 148.0f * d->pitch_env * crisp * tone_scale) * d->tune;
                d->ph1 = wrap01(d->ph1 + hz * INV_SR);
                s = fm_sin_turns(d->ph1) * d->env * 1.15f * sub_b;
                if (kit == 1u || kit == 4u || kit == 9u || bus == SF_BUS_WARM)
                    s = fm_tanhf(s * 1.35f);
            } else if (v == 1u) {
                /* Snare */
                float hz = (175.0f + 115.0f * d->pitch_env) * d->tune;
                d->ph1 = wrap01(d->ph1 + hz * INV_SR);
                s = (fm_sin_turns(d->ph1) * 0.48f + nse * 0.65f * crisp * tone_scale) * d->env;
            } else if (v == 2u || v == 3u) {
                /* Closed / Open Hi-Hat */
                float f = ((v == 2u ? 6800.0f : 6100.0f) * tone_scale) * d->tune;
                d->ph1 = wrap01(d->ph1 + (330.0f * d->tune) * INV_SR);
                d->ph2 = wrap01(d->ph2 + (540.0f * d->tune) * INV_SR);
                float sq = ((d->ph1 < 0.5f ? 1.0f : -1.0f) + (d->ph2 < 0.5f ? 1.0f : -1.0f)) * 0.5f;
                float g = fm_clampf(3.14159f * f * INV_SR, 0.02f, 0.85f);
                s = svf_tick(&d->bp, nse * 0.82f + sq * 0.35f, g, 0.45f, SF_FLT_HP) * d->env * 0.62f;
                pan = (v == 2u) ? -0.18f : 0.18f;
            } else if (v == 4u) {
                /* Clap */
                float g = fm_clampf(3.14159f * (1350.0f + d->tone * 900.0f) * d->tune * INV_SR, 0.02f, 0.7f);
                float burst = d->pitch_env > 0.72f ? ((uint32_t)(d->pitch_env * 40.0f) & 1u ? 1.0f : 0.25f) : 1.0f;
                s = svf_tick(&d->bp, nse, g, 0.52f, SF_FLT_BP) * d->env * burst * 0.92f;
                pan = -0.12f;
            } else if (v == 5u) {
                /* FM Tom */
                float hz = (112.0f + 165.0f * d->pitch_env * tone_scale) * d->tune;
                d->ph1 = wrap01(d->ph1 + hz * INV_SR);
                s = fm_sin_turns(d->ph1) * d->env * 0.85f;
                pan = 0.22f;
            } else if (v == 6u) {
                /* Ride (RD) */
                float g = fm_clampf(3.14159f * (4800.0f + d->tone * 1800.0f) * d->tune * INV_SR, 0.02f, 0.85f);
                d->ph1 = wrap01(d->ph1 + (420.0f * d->tune) * INV_SR);
                d->ph2 = wrap01(d->ph2 + (1170.0f * d->tune) * INV_SR);
                float fm = fm_sin_turns(d->ph1 + 0.65f * fm_sin_turns(d->ph2));
                s = svf_tick(&d->bp, nse * 0.62f + fm * 0.45f, g, 0.4f, SF_FLT_HP) * d->env * 0.52f;
                pan = 0.28f;
            } else {
                /* Rimshot (RS) */
                d->ph1 = wrap01(d->ph1 + (480.0f * d->tune) * INV_SR);
                d->ph2 = wrap01(d->ph2 + (1680.0f * d->tune) * INV_SR);
                s = (fm_sin_turns(d->ph1) * 0.55f + fm_sin_turns(d->ph2) * 0.45f * tone_scale) * d->env;
                pan = -0.25f;
            }

            if (kit == 8u) {
                s = (float)(int32_t)(s * 14.0f) * (1.0f / 14.0f);
            }
            if (bus == SF_BUS_COMP) {
                s = fm_tanhf(s * 1.48f) * 0.80f;
            } else if (bus == SF_BUS_WARM) {
                s = fm_tanhf(s * 1.15f) * 0.92f;
            } else if (bus == SF_BUS_SPARK && v >= 1u) {
                s *= 1.15f;
            }
            s *= d->level * drum_gain;
            out_l[i] += s * (1.0f - pan);
            out_r[i] += s * (1.0f + pan);
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

static void step_boot_jingle(uint32_t n)
{
    uint32_t step_samp = ((uint32_t)J_STEP_MS[dsp.jingle_idx] * 44100u) / 1000u;
    uint32_t i;
    while (dsp.jingle_step < 5u && dsp.jingle_sample_pos >= dsp.jingle_step * step_samp) {
        uint8_t s = dsp.jingle_step;
        uint8_t note = J_NOTES[dsp.jingle_idx][s];
        uint8_t vel = (s == 4u) ? 86u : 74u;
        uint32_t hold_samp = (s == 4u) ? (step_samp * 24u) / 10u : (step_samp * 92u) / 100u;
        internal_note_on(note, vel, 1u);
        dsp.jingle_rel_at[s] = dsp.jingle_step * step_samp + hold_samp;
        dsp.jingle_rel_done[s] = 0u;
        dsp.jingle_step++;
    }
    uint8_t pending_rel = 0u;
    for (i = 0; i < dsp.jingle_step && i < 5u; i++) {
        if (!dsp.jingle_rel_done[i]) {
            if (dsp.jingle_sample_pos >= dsp.jingle_rel_at[i]) {
                sf_dsp_note_off(J_NOTES[dsp.jingle_idx][i]);
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

void sf_dsp_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    sf_engine_cfg_t *cfg = dsp.cfg;
    sf_preset_t *p;
    float master_target = (float)master_q12 * (1.0f / 4096.0f);
    float gain_scale = 8300000.0f;

    if (n > 256u)
        n = 256u;

    if (dsp.jingle_active)
        step_boot_jingle(n);

    p = dsp.jingle_active ? &dsp.jingle_patch : &cfg->patch;

    for (i = 0; i < n; i++) {
        block_l[i] = 0.0f;
        block_r[i] = 0.0f;
        drum_rev[i] = 0.0f;
    }

    /* Decode preset parameters once per block */
    float a_atk = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->a_atk, 0.55f, 0.003f));
    float a_dec = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->a_dec, 3.0f, 0.03f));
    float a_sus = (float)p->a_sus * (1.0f / 127.0f);
    float a_rel = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->a_rel, 3.2f, 0.02f));

    float f_atk = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->f_atk, 1.2f, 0.003f));
    float f_dec = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->f_dec, 2.4f, 0.03f));
    float f_sus = (float)p->f_sus * (1.0f / 127.0f);
    float f_rel = 1.0f - fm_expf(-INV_SR / byte_to_env_sec(p->f_rel, 2.5f, 0.03f));

    float lfo1_hz = 0.1f + ((float)p->lfo1_rate * (1.0f / 127.0f)) * ((float)p->lfo1_rate * (1.0f / 127.0f)) * 18.0f;
    float lfo2_hz = 0.1f + ((float)p->lfo2_rate * (1.0f / 127.0f)) * ((float)p->lfo2_rate * (1.0f / 127.0f)) * 18.0f;
    float swarm_hz = fm_clampf((float)p->swarm_rate * 0.05f, 0.05f, 4.0f);
    dsp.lfo1_ph = wrap01(dsp.lfo1_ph + lfo1_hz * (float)n * INV_SR);
    dsp.lfo2_ph = wrap01(dsp.lfo2_ph + lfo2_hz * (float)n * INV_SR);
    dsp.swarm_ph = wrap01(dsp.swarm_ph + swarm_hz * (float)n * INV_SR);
    float lfo1 = eval_lfo(dsp.lfo1_ph, sf_lfo1_shape(p)) * ((float)p->lfo1_depth * 0.01f);
    float lfo2 = eval_lfo(dsp.lfo2_ph, sf_lfo2_shape(p)) * ((float)p->lfo2_depth * 0.01f);
    float swarm_mod = fm_sin_turns(dsp.swarm_ph) * ((float)p->swarm_depth * 0.0035f);

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
    float q1 = fm_clampf(byte_to_reso_q(p->f1_res) + mod_reso * 5.0f, 0.5f, 18.0f);
    float k1 = 1.0f / q1;
    float q2 = fm_clampf(byte_to_reso_q(p->f2_res), 0.5f, 18.0f);
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
    float pw = fm_clampf(0.08f + (float)p->pw * (0.84f / 127.0f), 0.08f, 0.92f);
    float wt_pos = (float)p->pw * (1.0f / 127.0f);
    float osc2_ratio = fm_exp2f(((float)p->osc2_semi + (float)p->osc2_detune * 0.0078f) * (1.0f / 12.0f));
    float spread = fm_clampf((float)p->spread * 0.01f + mod_spread * 0.4f, 0.05f, 1.4f);
    uint8_t osc_mode = sf_osc_mode(p);
    uint8_t sub_wave = sf_sub_wave(p);
    uint8_t f1_type = sf_f1_type(p);
    uint8_t f2_type = sf_f2_type(p);
    uint8_t routing = sf_f_route(p);
    uint8_t do_sync = sf_osc_sync(p);
    uint8_t sat_mode = sf_sat_mode(p);
    uint8_t unison = sf_unison(p);
    float drive = 1.0f + (float)p->fx_drive * 0.032f;
    float trem = fm_clampf(1.0f + mod_amp * 0.45f, 0.1f, 1.3f);
    float uni_pan_norm = (float)p->uni_pan * 0.01f;
    float glide_coeff = (p->glide == 0u) ? 1.0f : fm_clampf(1.0f - (float)p->glide * 0.0108f, 0.03f, 0.9f);

    for (v = 0; v < SF_MAX_VOICES; v++) {
        va_voice_t *vc = &dsp.voices[v];
        float note_f, f0, dt1, dt2, dt_sub, cut1_hz, g1, cut2_hz, g2, vel_g;
        float pan_l, pan_r;
        if (!vc->active)
            continue;

        vc->pitch_cur += ((float)vc->note - vc->pitch_cur) * glide_coeff;
        note_f = vc->pitch_cur + (float)p->oct * 12.0f + mod_pitch * 1.5f;
        f0 = 440.0f * fm_exp2f((note_f - 69.0f) * (1.0f / 12.0f));
        dt1 = fm_clampf(f0 * INV_SR, 0.0001f, 0.45f);
        dt2 = fm_clampf(f0 * osc2_ratio * INV_SR, 0.0001f, 0.45f);
        dt_sub = dt1 * 0.5f;

        float kf_oct = ((vc->pitch_cur - 60.0f) * (1.0f / 12.0f)) * kf_amt;
        float cut_scale = fm_exp2f(mod_cut1 * 2.5f + (vc->flt_env * fenv_oct) + kf_oct + ((float)vc->prs * (1.0f / 127.0f)) * 1.4f);
        cut1_hz = fm_clampf(cut1_base * cut_scale, 45.0f, 18500.0f);
        g1 = fm_clampf(3.14159f * cut1_hz * INV_SR, 0.003f, 0.92f);

        cut2_hz = fm_clampf(cut2_base * fm_exp2f(mod_cut2 * 2.5f + kf_oct * 0.5f), 45.0f, 18500.0f);
        g2 = fm_clampf(3.14159f * cut2_hz * INV_SR, 0.003f, 0.92f);

        vel_g = (0.35f + 0.65f * ((float)vc->vel * (1.0f / 127.0f))) * 0.22f * trem;
        float vpan = fm_clampf((((float)(v & 3u) - 1.5f) * 0.22f * spread * uni_pan_norm) + mod_pan * 0.5f, -0.82f, 0.82f);
        pan_l = 0.707f * (1.0f - vpan);
        pan_r = 0.707f * (1.0f + vpan);

        uint8_t do_hyper = (osc_mode == SF_OSC_HYPERSAW || unison > 1u);

        for (i = 0; i < n; i++) {
            float o1 = 0.0f, o2 = 0.0f, sub = 0.0f, sig_l, sig_r;
            /* Envelopes */
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

            /* Oscillator 1 */
            vc->phase1 += dt1;
            if (vc->phase1 >= 1.0f) {
                vc->phase1 -= 1.0f;
                if (do_sync)
                    vc->phase2 = 0.0f;
            }
            if (do_hyper && osc_mode == SF_OSC_HYPERSAW) {
                float s0 = 2.0f * vc->phase1 - 1.0f - polyblep(vc->phase1, dt1);
                float det = 0.005f + 0.020f * spread;
                vc->hyper_ph[0] = wrap01(vc->hyper_ph[0] + dt1 * (1.0f - det));
                vc->hyper_ph[1] = wrap01(vc->hyper_ph[1] + dt1 * (1.0f + det));
                vc->hyper_ph[2] = wrap01(vc->hyper_ph[2] + dt1 * (1.0f - 0.5f * det));
                vc->hyper_ph[3] = wrap01(vc->hyper_ph[3] + dt1 * (1.0f + 0.5f * det));
                o1 = (s0 + (2.0f * vc->hyper_ph[0] - 1.0f) + (2.0f * vc->hyper_ph[1] - 1.0f)
                      + (2.0f * vc->hyper_ph[2] - 1.0f) + (2.0f * vc->hyper_ph[3] - 1.0f)) * 0.34f;
            } else if (osc_mode == SF_OSC_PULSE) {
                o1 = vc->phase1 < pw ? 1.0f : -1.0f;
                o1 += polyblep(vc->phase1, dt1);
                o1 -= polyblep(wrap01(vc->phase1 + (1.0f - pw)), dt1);
            } else if (osc_mode == SF_OSC_TRI) {
                o1 = 1.0f - 4.0f * fm_fabsf(vc->phase1 - 0.5f);
            } else if (osc_mode == SF_OSC_WT) {
                float s1 = fm_sin_turns(vc->phase1);
                float s2 = fm_sin_turns(wrap01(vc->phase1 * (2.0f + wt_pos * 4.0f)));
                float saw = 2.0f * vc->phase1 - 1.0f;
                o1 = s1 * (1.0f - wt_pos) + (s2 * 0.65f + saw * 0.35f) * wt_pos;
            } else {
                o1 = 2.0f * vc->phase1 - 1.0f - polyblep(vc->phase1, dt1);
            }

            if (unison > 1u && osc_mode != SF_OSC_HYPERSAW) {
                float udet = 0.003f + 0.009f * spread;
                vc->hyper_ph[0] = wrap01(vc->hyper_ph[0] + dt1 * (1.0f - udet));
                vc->hyper_ph[1] = wrap01(vc->hyper_ph[1] + dt1 * (1.0f + udet));
                float u_a = (osc_mode == SF_OSC_PULSE) ? (vc->hyper_ph[0] < pw ? 0.65f : -0.65f) : (2.0f * vc->hyper_ph[0] - 1.0f) * 0.65f;
                float u_b = (osc_mode == SF_OSC_PULSE) ? (vc->hyper_ph[1] < pw ? 0.65f : -0.65f) : (2.0f * vc->hyper_ph[1] - 1.0f) * 0.65f;
                o1 = (o1 + (u_a + u_b) * (0.28f + 0.1f * (float)unison)) * 0.68f;
            }

            /* Oscillator 2 */
            vc->phase2 = wrap01(vc->phase2 + dt2 + fm_amt * 0.02f * o1);
            if (osc_mode == SF_OSC_PULSE)
                o2 = vc->phase2 < pw ? 1.0f : -1.0f;
            else if (osc_mode == SF_OSC_TRI)
                o2 = 1.0f - 4.0f * fm_fabsf(vc->phase2 - 0.5f);
            else
                o2 = 2.0f * vc->phase2 - 1.0f - polyblep(vc->phase2, dt2);

            /* Sub & Noise & Ring */
            vc->phase_sub = wrap01(vc->phase_sub + dt_sub);
            sub = (sub_wave == 1u) ? fm_sin_turns(vc->phase_sub)
                : (sub_wave == 2u) ? (2.0f * vc->phase_sub - 1.0f)
                : (vc->phase_sub < 0.5f ? 1.0f : -1.0f);

            float src1 = o1 * o1_mix + sub * sub_mix + fast_noise() * noise_mix + (o1 * o2) * ring_mix;
            float src2 = o2 * o2_mix;

            if (sat_mode == SF_SAT_HARD) {
                src1 = fm_clampf(src1 * drive, -0.95f, 0.95f);
            } else if (sat_mode == SF_SAT_TUBE) {
                src1 = fm_tanhf(src1 * drive * 1.25f);
            } else if (sat_mode == SF_SAT_SOFT) {
                src1 = fm_tanhf(src1 * (0.85f + drive * 0.25f));
            }

            /* Filter Routing: Serial, Parallel, Split */
            if (routing == SF_ROUTE_SPL) {
                float fl = svf_tick(&vc->f1l, src1, g1, k1, f1_type);
                float fr = svf_tick(&vc->f2r, src2, g2, k2, f2_type);
                float w1 = 1.0f - f_bal * 0.5f;
                float w2 = 0.5f + f_bal * 0.5f;
                sig_l = (fl * w1 + fr * 0.28f * w2) * pan_l;
                sig_r = (fl * 0.28f * w1 + fr * w2) * pan_r;
            } else if (routing == SF_ROUTE_PAR) {
                float comb = (src1 + src2) * 0.7f;
                float fl = svf_tick(&vc->f1l, comb, g1, k1, f1_type);
                float fr = svf_tick(&vc->f2r, comb, g2, k2, f2_type);
                float mixf = fl * (1.0f - f_bal * 0.6f) + fr * (0.4f + f_bal * 0.6f);
                sig_l = mixf * 0.72f * pan_l;
                sig_r = mixf * 0.72f * pan_r;
            } else {
                float comb = (src1 + src2) * 0.7f;
                float f1 = svf_tick(&vc->f1l, comb, g1, k1, f1_type);
                float f2 = svf_tick(&vc->f2l, f1, g2, k2, f2_type);
                float mout = f1 * (1.0f - f_bal) + f2 * f_bal;
                sig_l = mout * pan_l;
                sig_r = mout * pan_r;
            }

            float g = vc->amp_env * vel_g;
            block_l[i] += sig_l * g;
            block_r[i] += sig_r * g;
        }
    }

    /* Synthesized Drums */
    render_drums(block_l, block_r, drum_rev, n);

    /* Stereo FX Bus: Phaser, Chorus, Ping-Pong Delay, Reverb, Master Gain + Limiter */
    float chor_mix = fm_clampf((float)p->fx_chorus * 0.01f
        + (l1d == SF_LFO_CHORUS ? lfo1 * 0.4f : 0.0f) + (l2d == SF_LFO_CHORUS ? lfo2 * 0.3f : 0.0f), 0.0f, 1.0f);
    float phas_mix = fm_clampf((float)p->fx_phaser * 0.01f
        + (l1d == SF_LFO_PHASER ? lfo1 * 0.4f : 0.0f) + (l2d == SF_LFO_PHASER ? lfo2 * 0.3f : 0.0f), 0.0f, 1.0f);
    float phas_hz = fm_clampf((float)p->fx_phaser_rate * 0.1f, 0.1f, 6.0f);
    float del_fb = fm_clampf((float)p->fx_del_fb * 0.01f, 0.0f, 0.85f);
    float del_mix = fm_clampf((float)p->fx_del_mix * 0.0082f
        + (l1d == SF_LFO_DELAY ? lfo1 * 0.35f : 0.0f) + (l2d == SF_LFO_DELAY ? lfo2 * 0.25f : 0.0f), 0.0f, 0.82f);
    uint32_t del_tap = 800u + (uint32_t)p->fx_del_time * 152u;
    if (del_tap >= DEL_LEN - 4u)
        del_tap = DEL_LEN - 4u;
    float rev_mix = fm_clampf((float)p->fx_reverb * 0.0072f
        + (l1d == SF_LFO_REVERB ? lfo1 * 0.35f : 0.0f) + (l2d == SF_LFO_REVERB ? lfo2 * 0.25f : 0.0f), 0.0f, 0.78f);

    float sum_sq = 0.0f;
    for (i = 0; i < n; i++) {
        float xl = block_l[i];
        float xr = block_r[i];

        /* 4-stage Phaser */
        if (phas_mix > 0.02f) {
            dsp.phas_ph = wrap01(dsp.phas_ph + phas_hz * INV_SR);
            float a = -0.78f + 0.65f * fm_sin_turns(dsp.phas_ph);
            float pl = xl, pr = xr;
            uint32_t st;
            for (st = 0; st < 4u; st++) {
                float yl = a * pl + dsp.phas_st[st][0];
                dsp.phas_st[st][0] = pl - a * yl;
                pl = yl;
                float yr = a * pr + dsp.phas_st[st][1];
                dsp.phas_st[st][1] = pr - a * yr;
                pr = yr;
            }
            xl = xl * (1.0f - phas_mix * 0.45f) + pl * phas_mix * 0.55f;
            xr = xr * (1.0f - phas_mix * 0.45f) + pr * phas_mix * 0.55f;
        }

        /* Stereo Chorus */
        chor_l[dsp.chor_wr] = xl;
        chor_r[dsp.chor_wr] = xr;
        if (chor_mix > 0.02f) {
            dsp.chor_ph = wrap01(dsp.chor_ph + 0.48f * INV_SR);
            float m = fm_sin_turns(dsp.chor_ph);
            uint32_t d1 = (dsp.chor_wr - (uint32_t)(280.0f + 160.0f * m)) & (CHOR_LEN - 1u);
            uint32_t d2 = (dsp.chor_wr - (uint32_t)(280.0f - 160.0f * m)) & (CHOR_LEN - 1u);
            xl += chor_l[d1] * chor_mix * 0.48f;
            xr += chor_r[d2] * chor_mix * 0.48f;
        }
        dsp.chor_wr = (dsp.chor_wr + 1u) & (CHOR_LEN - 1u);

        /* Ping-Pong Stereo Delay (int16 pool) */
        uint32_t rd_l = (dsp.del_wr - del_tap) & (DEL_LEN - 1u);
        uint32_t rd_r = (dsp.del_wr - ((del_tap * 3u) >> 2)) & (DEL_LEN - 1u);
        float dl = (float)del_buf[rd_l * 2u] * (1.0f / 8192.0f);
        float dr = (float)del_buf[rd_r * 2u + 1u] * (1.0f / 8192.0f);
        float wl = fm_clampf(xl + dr * del_fb, -3.5f, 3.5f);
        float wr = fm_clampf(xr + dl * del_fb, -3.5f, 3.5f);
        del_buf[dsp.del_wr * 2u] = (int16_t)(wl * 8192.0f);
        del_buf[dsp.del_wr * 2u + 1u] = (int16_t)(wr * 8192.0f);
        dsp.del_wr = (dsp.del_wr + 1u) & (DEL_LEN - 1u);
        xl += dl * del_mix;
        xr += dr * del_mix;

        /* Diffusion Reverb (4 comb + 2 allpass in int16 pool) */
        if (rev_mix > 0.02f || cfg->drum_bus == SF_BUS_ROOM) {
            float mono = ((xl + xr) * 0.5f) * rev_mix + drum_rev[i];
            static const uint32_t CLENS[4] = {REV_C0, REV_C1, REV_C2, REV_C3};
            int16_t *cb[4] = {rev_c0, rev_c1, rev_c2, rev_c3};
            float sum = 0.0f;
            uint32_t k;
            for (k = 0; k < 4u; k++) {
                uint32_t ci = dsp.rev_ci[k];
                float c = (float)cb[k][ci] * (1.0f / 8192.0f);
                sum += c;
                cb[k][ci] = (int16_t)(fm_clampf(mono + c * 0.78f, -3.5f, 3.5f) * 8192.0f);
                dsp.rev_ci[k] = (ci + 1u >= CLENS[k]) ? 0u : (ci + 1u);
            }
            sum *= 0.25f;
            float a0v = (float)rev_a0[dsp.rev_ai[0]] * (1.0f / 8192.0f);
            float y0 = -sum + a0v;
            rev_a0[dsp.rev_ai[0]] = (int16_t)(fm_clampf(sum + a0v * 0.5f, -3.5f, 3.5f) * 8192.0f);
            dsp.rev_ai[0] = (dsp.rev_ai[0] + 1u >= REV_A0) ? 0u : (dsp.rev_ai[0] + 1u);
            float a1v = (float)rev_a1[dsp.rev_ai[1]] * (1.0f / 8192.0f);
            float y1 = -y0 + a1v;
            rev_a1[dsp.rev_ai[1]] = (int16_t)(fm_clampf(y0 + a1v * 0.5f, -3.5f, 3.5f) * 8192.0f);
            dsp.rev_ai[1] = (dsp.rev_ai[1] + 1u >= REV_A1) ? 0u : (dsp.rev_ai[1] + 1u);

            xl += y1 * (0.85f);
            xr += y1 * (0.78f);
        }

        /* Master Volume & Soft Limiting */
        dsp.master_now += (master_target - dsp.master_now) * 0.005f;
        float sl = fm_tanhf(xl);
        float sr = fm_tanhf(xr);
        float mono_out = (sl + sr) * 0.5f;
        sum_sq += mono_out * mono_out;
        if ((i & 1u) == 0u) {
            uint32_t si = i >> 1;
            if (si < 128u)
                dsp.vis.scope[si] = (int8_t)fm_clampf(mono_out * 110.0f, -120.0f, 120.0f);
        }

        lr[2u * i] = (int32_t)(sl * dsp.master_now * gain_scale);
        lr[2u * i + 1u] = (int32_t)(sr * dsp.master_now * gain_scale);
    }

    /* Update live 32-band analyzer & RMS telemetry once per audio block */
    {
        float rms_inst = fm_clampf(fm_sqrtf(sum_sq / (float)(n ? n : 1u)) * 3.5f, 0.0f, 1.0f);
        dsp.vis_rms_f = dsp.vis_rms_f * 0.72f + rms_inst * 0.28f;
        dsp.vis.rms_pct = (uint8_t)(dsp.vis_rms_f * 100.0f);

        float c_norm = (float)p->f1_cut * (1.0f / 127.0f);
        float r_norm = (float)p->f1_res * (1.0f / 127.0f);
        for (i = 0; i < 32u; i++) {
            float bn = (float)i * (1.0f / 31.0f);
            float pass = (f1_type == SF_FLT_HP) ? (bn > c_norm ? 1.0f : 0.25f)
                       : (f1_type == SF_FLT_BP) ? (1.0f / (1.0f + fm_fabsf(bn - c_norm) * 8.0f))
                       : (bn <= c_norm ? 1.0f : fm_clampf(1.0f - (bn - c_norm) * 2.5f, 0.12f, 1.0f));
            float peak = 1.0f + r_norm * 0.6f / (1.0f + fm_fabsf(bn - c_norm) * 14.0f);
            float jit = 0.65f + 0.35f * fm_fabsf((float)dsp.vis.scope[(i * 4u) & 127u] * (1.0f / 120.0f));
            float targ = fm_clampf(rms_inst * pass * peak * jit, 0.0f, 1.0f);
            dsp.vis_bands_f[i] = dsp.vis_bands_f[i] * 0.68f + targ * 0.32f;
            dsp.vis.bands[i] = (uint8_t)(dsp.vis_bands_f[i] * 255.0f);
        }
    }
}
