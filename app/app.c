/* SPDX-License-Identifier: MPL-2.0 */
#include "fastmath.h"
#include "dsp.h"
#include "presets_rom.h"

#define SET_MAGIC 0x53545331u
#define BANKX_MAGIC 0x53545831u

enum {
    PAGE_HOME = 0, PAGE_FX, PAGE_SEL, PAGE_ENV, PAGE_LFO, PAGE_EDIT,
    PAGE_GLO, PAGE_ARP, PAGE_SEQ, PAGE_SAVE, PAGE_RAND_ASK, PAGE_BLE
};

typedef struct {
    uint32_t magic;
    uint8_t bank;       /* 0..7=A..H, 8=I, 9=X */
    uint8_t prog;       /* 0..127 */
    int8_t octave;      /* -2..+2 */
    uint8_t palette;    /* 0..4 */
    uint8_t ble_off;    /* 0..1 */
    uint16_t bpm;       /* 50..220 */
    uint8_t swing;      /* 0..60 */
    uint8_t drum_kit;   /* 0..9 */
    uint8_t drum_vol;   /* 0..100 */
    uint8_t drum_bus;   /* 0..2 */
    uint8_t scale;      /* 0..11 */
    uint8_t root;       /* 0..11 */
    uint8_t chord_mode; /* 0..4 */
    uint8_t arp_mode;   /* 0..3 */
    uint8_t arp_rate;   /* 0..3 */
    uint8_t arp_octs;   /* 1..3 */
    uint8_t arp_latch;  /* 0..1 */
    uint8_t rand_chaos; /* 0..100 */
    uint8_t rand_style; /* 0..5 */
    uint8_t mascot_idx; /* 0..5 */
    uint8_t seq_pat[SF_DRUM_VOICES][SF_SEQ_STEPS];
    uint8_t drum_mute;
    uint8_t drum_tune[SF_DRUM_VOICES];
    uint8_t drum_decay[SF_DRUM_VOICES];
    sf_preset_t patch;
} sf_settings_t;

typedef struct {
    uint32_t magic;
    uint8_t used[SF_BANK_X_SLOTS];
    sf_preset_t slots[SF_BANK_X_SLOTS];
} sf_bankx_t;

static sf_settings_t set;
static sf_bankx_t bankx PLAT_POOL;
static sf_engine_cfg_t eng;

static volatile uint8_t seq_playing;
static volatile uint8_t seq_step;
static volatile uint32_t tick_acc_us;
static volatile uint8_t arp_active;
static volatile uint8_t arp_held[NKEYS];
static volatile uint8_t arp_latch_buf[NKEYS];
static volatile int8_t arp_cur_note = -1;
static volatile uint8_t arp_pos;
static volatile uint8_t arp_dir_dn;
static volatile uint32_t arp_tick_us;

static uint8_t set_dirty;
static uint8_t unsaved_random;
static uint8_t pending_nav_bank;
static uint8_t pending_nav_dir; /* 1=bank_step, 2=prog_step */
static int8_t pending_nav_delta;
static volatile uint8_t backup_hold;
static uint32_t rand_seed = 0x9E3779B9u;

static const char *const PAGE_NAME[] = {
    "HOME", "FX", "SEL/FLT", "ENV", "LFO", "EDIT", "GLOBAL", "ARP", "SEQ/DRUM", "SAVE", "RANDOM?", "BLE"
};
static const char *const BANK_CHAR = "ABCDEFGHIX";
static const char *const OSC_NAMES[SF_NOSC] = {"HYPRSAW", "SAW", "PULSE", "TRI", "SYNC", "WAVETBL"};
static const char *const FLT_NAMES[4] = {"LP", "BP", "HP", "NOTCH"};
static const char *const ROUTE_NAMES[3] = {"SER", "PAR", "SPL"};
static const char *const SAT_NAMES[4] = {"SOFT", "TUBE", "HARD", "OFF"};
static const char *const LFO_NAMES[SF_NLFO_DEST] = {
    "F1 CUT", "F2 CUT", "RESO", "PITCH", "PAN", "SPREAD", "AMP", "CHORUS", "PHASER", "DELAY", "REVERB"
};
static const char *const LFO_SHAPES[4] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const SCALE_NAMES[SF_NSCALE] = {
    "CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXOLYD", "HARM MIN", "MIN PENT", "MAJ PENT", "WHOLE", "HIRAJOS"
};
static const char *const CHORD_NAMES[SF_NCHORD] = {"OFF", "OCT", "5TH", "MIN7", "MAJ7"};
static const char *const ARP_MODE_NAMES[SF_NARP_MODE] = {"UP", "DOWN", "UP/DN", "RAND"};
static const char *const ARP_RATE_NAMES[4] = {"1/4", "1/8", "1/16", "1/32"};
static const char *const KIT_NAMES[SF_DRUM_KITS] = {
    "TR-808", "TR-909", "CR-78", "LINN", "DMX", "FM-PERC", "ACID-9", "INDUS", "LO-FI", "STARFISH"
};
static const char *const DRUM_NAMES[SF_DRUM_VOICES] = {
    "BD", "SD", "CH", "OH", "CP", "TM", "RM", "CY"
};
static const char *const BUS_NAMES[SF_NBUS] = {"CLEAN", "COMP", "CRUSH"};
static const char *const RAND_NAMES[SF_NRAND_STYLE] = {"WILD", "BASS", "PAD", "PLUCK", "ACID", "LEAD"};
static const char *const MASCOT_TITLES[6] = {
    "BEACH CHILLIN'", "MOON EXPLORER", "SUB CAPTAIN", "CAMPFIRE JAM", "ROCKET SKATER", "STARFISH CHEF"
};
static const char *const NOTE12[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static uint32_t xorshift32(void)
{
    uint32_t x = rand_seed ^ plat_ms();
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_seed = x;
    return x;
}

static void mem_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

static void mem_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = 0u;
}

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void sync_engine_cfg(void)
{
    uint32_t f = plat_lock();
    mem_copy(&eng.patch, &set.patch, (uint32_t)sizeof(sf_preset_t));
    eng.bpm = set.bpm;
    eng.swing = set.swing;
    eng.drum_kit = set.drum_kit;
    eng.drum_vol = set.drum_vol;
    eng.drum_bus = set.drum_bus;
    eng.scale = set.scale;
    eng.root = set.root;
    eng.chord_mode = set.chord_mode;
    eng.arp_mode = set.arp_mode;
    eng.arp_rate = set.arp_rate;
    eng.arp_octs = set.arp_octs;
    eng.arp_latch = set.arp_latch;
    eng.rand_chaos = set.rand_chaos;
    eng.rand_style = set.rand_style;
    mem_copy(eng.seq_pat, set.seq_pat, (uint32_t)sizeof(set.seq_pat));
    eng.drum_mute = set.drum_mute;
    mem_copy(eng.drum_tune, set.drum_tune, (uint32_t)sizeof(set.drum_tune));
    mem_copy(eng.drum_decay, set.drum_decay, (uint32_t)sizeof(set.drum_decay));
    sf_dsp_set_cfg(&eng);
    plat_unlock(f);
}

static uint32_t bank_len(uint8_t bank)
{
    if (bank < 8u)
        return SF_BANK_SIZE;
    if (bank == 8u)
        return SF_BANK_I_SIZE;
    return SF_BANK_X_SLOTS;
}

static const sf_preset_t *get_preset_ptr(uint8_t bank, uint8_t prog)
{
    if (bank < 8u)
        return &SF_ROM[(uint32_t)bank * SF_BANK_SIZE + (prog % SF_BANK_SIZE)];
    if (bank == 8u)
        return &SF_ROM[1024u + (prog % SF_BANK_I_SIZE)];
    return &bankx.slots[prog % SF_BANK_X_SLOTS];
}

static void load_preset(uint8_t bank, uint8_t prog)
{
    set.bank = bank % 10u;
    set.prog = (uint8_t)(prog % bank_len(set.bank));
    mem_copy(&set.patch, get_preset_ptr(set.bank, set.prog), (uint32_t)sizeof(sf_preset_t));
    arp_active = sf_arp_on(&set.patch);
    unsaved_random = 0u;
    sync_engine_cfg();
}

static void make_init_patch(sf_preset_t *p, const char *label)
{
    uint32_t i;
    mem_zero(p, (uint32_t)sizeof(*p));
    for (i = 0; i < 15u && label[i]; i++)
        p->name[i] = label[i];
    p->osc_mode = SF_OSC_HYPERSAW;
    p->pw = 64u;
    p->osc2_detune = 14u;
    p->osc_bal = 50u;
    p->sub_vol = 60u;
    p->unison = 2u;
    p->spread = 76u;
    p->f1_cut = 86u;
    p->f1_res = 26u;
    p->f1_env = 42u;
    p->f2_cut = 112u;
    p->a_atk = 4u;
    p->a_dec = 68u;
    p->a_sus = 96u;
    p->a_rel = 44u;
    p->f_atk = 4u;
    p->f_dec = 52u;
    p->f_sus = 54u;
    p->f_rel = 44u;
    p->lfo1_rate = 28u;
    p->lfo1_depth = 18u;
    p->lfo2_meta = (4u) | (3u << 4);
    p->fx_chorus = 45u;
    p->fx_phaser = 0u;
    p->fx_delay = 5u | (5u << 4);
    p->fx_rvb_drv = 6u | (3u << 4);
}

static void bankx_defaults(void)
{
    uint32_t i;
    mem_zero(&bankx, (uint32_t)sizeof(bankx));
    bankx.magic = BANKX_MAGIC;
    for (i = 0; i < SF_BANK_X_SLOTS; i++) {
        char nm[16] = "USER ";
        char d[6];
        int_text((int32_t)(i + 1u), d);
        text_cat(nm, d);
        make_init_patch(&bankx.slots[i], nm);
    }
}

static void default_drums(void)
{
    static const uint8_t P0[SF_SEQ_STEPS] = {2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0};
    static const uint8_t P1[SF_SEQ_STEPS] = {0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1};
    static const uint8_t P2[SF_SEQ_STEPS] = {1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1};
    uint32_t v, s;
    mem_zero(set.seq_pat, (uint32_t)sizeof(set.seq_pat));
    for (s = 0; s < SF_SEQ_STEPS; s++) {
        set.seq_pat[0][s] = P0[s];
        set.seq_pat[1][s] = P1[s];
        set.seq_pat[2][s] = P2[s];
    }
    set.drum_mute = 0u;
    for (v = 0; v < SF_DRUM_VOICES; v++) {
        set.drum_tune[v] = 50u;
        set.drum_decay[v] = 50u;
    }
}

static void settings_defaults(void)
{
    mem_zero(&set, (uint32_t)sizeof(set));
    set.magic = SET_MAGIC;
    set.bank = 8u; /* Bank I Starfish signature bank on fresh boot */
    set.prog = 0u;
    set.octave = 0;
    set.palette = 2u;
    set.ble_off = 0u;
    set.bpm = 122u;
    set.swing = 8u;
    set.drum_kit = 9u;
    set.drum_vol = 80u;
    set.drum_bus = 0u;
    set.scale = SF_SCALE_CHR;
    set.root = 0u;
    set.chord_mode = SF_CHORD_OFF;
    set.arp_mode = SF_ARP_UPDN;
    set.arp_rate = 2u;
    set.arp_octs = 2u;
    set.arp_latch = 0u;
    set.rand_chaos = 60u;
    set.rand_style = SF_RAND_WILD;
    set.mascot_idx = 0u;
    default_drums();
    load_preset(8u, 0u);
}

int starfish_ble_off(void) { return set.ble_off; }

static void load_all_from_store(void)
{
    if (plat_store_load(OBJ_BANKX, &bankx, (uint32_t)sizeof(bankx)) != (int)sizeof(bankx) || bankx.magic != BANKX_MAGIC)
        bankx_defaults();
    if (plat_store_load(OBJ_SETTINGS, &set, (uint32_t)sizeof(set)) != (int)sizeof(set) || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.bank >= 10u) {
        settings_defaults();
    } else {
        set.bpm = (uint16_t)clamp((int32_t)set.bpm, 50, 220);
        set.palette %= NPALETTES;
        palette_set(set.palette);
        sync_engine_cfg();
    }
}

void app_init(void)
{
    sf_dsp_init(&eng);
    load_all_from_store();
    sf_dsp_play_jingle(1u);
}

/* ----------------------------------------------------------- Scale helpers --- */
static uint8_t quantize_note(int32_t midi)
{
    static const uint16_t MASKS[SF_NSCALE] = {
        0x0FFFu, /* Chromatic */
        0x0AB5u, /* Major */
        0x05ADu, /* Minor */
        0x06B5u, /* Dorian */
        0x05ABu, /* Phrygian */
        0x0AD5u, /* Lydian */
        0x06B5u, /* Mixolydian */
        0x09ADu, /* Harmonic Minor */
        0x04A9u, /* Minor Pentatonic */
        0x0295u, /* Major Pentatonic */
        0x0555u, /* Whole Tone */
        0x08B3u, /* Hirajoshi */
    };
    uint16_t mask = MASKS[set.scale % SF_NSCALE];
    int32_t best = midi, d;
    if (set.scale == SF_SCALE_CHR)
        return (uint8_t)clamp(midi, 0, 127);
    for (d = 0; d < 12; d++) {
        int32_t up = midi + d;
        int32_t pc = ((up - (int32_t)set.root) % 12 + 12) % 12;
        if ((mask >> pc) & 1u) {
            best = up;
            break;
        }
        int32_t dn = midi - d;
        pc = ((dn - (int32_t)set.root) % 12 + 12) % 12;
        if ((mask >> pc) & 1u) {
            best = dn;
            break;
        }
    }
    return (uint8_t)clamp(best, 0, 127);
}

static uint32_t chord_intervals(int8_t out[4])
{
    out[0] = 0;
    if (set.chord_mode == SF_CHORD_OCT) { out[1] = 12; return 2u; }
    if (set.chord_mode == SF_CHORD_5TH) { out[1] = 7; return 2u; }
    if (set.chord_mode == SF_CHORD_MIN7) { out[1] = 3; out[2] = 7; out[3] = 10; return 4u; }
    if (set.chord_mode == SF_CHORD_MAJ7) { out[1] = 4; out[2] = 7; out[3] = 11; return 4u; }
    return 1u;
}

/* ----------------------------------------------------------- Randomizer --- */
static void randomize_patch(void)
{
    uint32_t r = xorshift32();
    sf_preset_t *p = &set.patch;
    uint32_t c = set.rand_chaos;
    static const char *const R_PREFIX[6] = {"STAR ", "NEBULA ", "ACID ", "VOID ", "PULSE ", "CORAL "};
    char num[6];
    uint8_t st = set.rand_style % SF_NRAND_STYLE;

    p->name[0] = 0;
    text_cat(p->name, R_PREFIX[r % 6u]);
    int_text((int32_t)((r >> 8) % 99u) + 1, num);
    text_cat(p->name, num);

    p->osc_mode = (uint8_t)((st == SF_RAND_ACID) ? SF_OSC_SAW
                 : (st == SF_RAND_PAD) ? SF_OSC_HYPERSAW
                 : (r >> 4) % SF_NOSC);
    p->pw = (uint8_t)(20u + ((r >> 10) % 90u));
    p->osc2_semi = (int8_t)((st == SF_RAND_PAD || st == SF_RAND_LEAD) ? (((r >> 12) & 1u) ? 7 : 12) : 0);
    p->osc2_detune = (uint8_t)(6u + ((r >> 14) % (12u + c / 4u)));
    p->osc_bal = (uint8_t)(25u + ((r >> 16) % 55u));
    p->sub_vol = (uint8_t)((st == SF_RAND_BASS) ? 85u : 30u + ((r >> 18) % 55u));
    p->unison = (uint8_t)(1u + ((r >> 20) % 4u));
    p->spread = (uint8_t)(35u + ((r >> 22) % 60u));

    uint32_t r2 = xorshift32();
    p->f1_cut = (uint8_t)((st == SF_RAND_BASS) ? (28u + (r2 % 35u))
                : (st == SF_RAND_ACID) ? (24u + (r2 % 48u))
                : (45u + (r2 % 70u)));
    p->f1_res = (uint8_t)((st == SF_RAND_ACID) ? (72u + ((r2 >> 4) % 45u))
                : (15u + ((r2 >> 4) % (25u + c * 3u / 4u))));
    p->f1_type = (uint8_t)((c > 70u && ((r2 >> 8) % 4u) == 0u) ? SF_FLT_BP : SF_FLT_LP);
    p->f1_env = (uint8_t)(25u + ((r2 >> 10) % 65u));

    p->a_atk = (uint8_t)((st == SF_RAND_PAD) ? (38u + ((r2 >> 12) % 55u)) : (2u + ((r2 >> 12) % 10u)));
    p->a_dec = (uint8_t)(35u + ((r2 >> 15) % 65u));
    p->a_sus = (uint8_t)((st == SF_RAND_PLUCK) ? 16u : (65u + ((r2 >> 18) % 55u)));
    p->a_rel = (uint8_t)((st == SF_RAND_PAD) ? (60u + ((r2 >> 20) % 55u)) : (24u + ((r2 >> 20) % 45u)));

    uint32_t r3 = xorshift32();
    uint8_t l1d = (uint8_t)((r3) % SF_NLFO_DEST);
    uint8_t l1s = (uint8_t)((r3 >> 4) % 4u);
    p->lfo1_rate = (uint8_t)(12u + ((r3 >> 6) % 75u));
    p->lfo1_depth = (uint8_t)(20u + ((r3 >> 9) % (35u + c / 2u)));
    p->lfo1_meta = (uint8_t)((l1d & 15u) | ((l1s & 3u) << 4));
    p->lfo2_meta = (uint8_t)(SF_LFO_PAN | ((2u + ((r3 >> 14) % 8u)) << 4));
    p->fx_chorus = (uint8_t)((r3 >> 16) % 85u);
    p->fx_phaser = (uint8_t)((r3 >> 18) % 75u);
    p->fx_delay = (uint8_t)((3u + ((r3 >> 20) % 10u)) | ((3u + ((r3 >> 22) % 9u)) << 4));
    p->fx_rvb_drv = (uint8_t)((4u + ((r3 >> 24) % 10u)) | ((2u + ((r3 >> 26) % 9u)) << 4));

    unsaved_random = 1u;
    set_dirty = 1u;
    sync_engine_cfg();
}

static void save_to_next_bankx(void)
{
    uint32_t slot = 0u, i;
    for (i = 0; i < SF_BANK_X_SLOTS; i++) {
        if (!bankx.used[i]) {
            slot = i;
            break;
        }
    }
    bankx.used[slot] = 1u;
    mem_copy(&bankx.slots[slot], &set.patch, (uint32_t)sizeof(sf_preset_t));
    set.bank = 9u;
    set.prog = (uint8_t)slot;
    unsaved_random = 0u;
    plat_store_save(OBJ_BANKX, &bankx, (uint32_t)sizeof(bankx));
    plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
    set_dirty = 0u;
}

/* ------------------------------------------------------------- MIDI in --- */
static volatile uint32_t midi_last, midi_last_port, midi_count;

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st != 0xF8u && st != 0xFEu) {
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if ((st & 0xF0u) == 0x90u && d2)
            sf_dsp_note_on((uint8_t)d1, (uint8_t)d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            sf_dsp_note_off((uint8_t)d1);
        else if ((st & 0xF0u) == 0xA0u)
            sf_dsp_aftertouch((uint8_t)d1, (uint8_t)d2);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    midi_in();
    sf_dsp_render(lr, n, master_q12);
}

void app_panic(void)
{
    seq_playing = 0u;
    arp_active = 0u;
    sf_dsp_panic();
}

/* ----------------------------------------------------- 1 kHz Sequencer --- */
void app_tick(void)
{
    uint32_t step_us;
    if (seq_playing) {
        step_us = 15000000u / (uint32_t)(eng.bpm ? eng.bpm : 120u);
        if (seq_step & 1u)
            step_us = step_us - (step_us * eng.swing) / 200u;
        else
            step_us = step_us + (step_us * eng.swing) / 200u;
        tick_acc_us += 1000u;
        if (tick_acc_us >= step_us) {
            uint8_t st = seq_step, v;
            tick_acc_us -= step_us;
            for (v = 0; v < SF_DRUM_VOICES; v++) {
                uint8_t hit = eng.seq_pat[v][st];
                if (hit)
                    sf_dsp_trigger_drum(v, hit == 2u ? 127u : 94u);
            }
            seq_step = (uint8_t)((st + 1u) & 15u);
        }
    }

    if (arp_active) {
        static const uint8_t RATE_DIV[4] = {4u, 2u, 1u, 1u};
        uint32_t div = RATE_DIV[eng.arp_rate & 3u];
        uint32_t arp_us = (15000000u / (uint32_t)(eng.bpm ? eng.bpm : 120u)) * div;
        if (eng.arp_rate == 3u)
            arp_us >>= 1;
        arp_tick_us += 1000u;
        if (arp_tick_us >= arp_us) {
            uint8_t pool[NKEYS * 3u], cnt = 0u, k, oct;
            arp_tick_us -= arp_us;
            for (oct = 0; oct < (eng.arp_octs ? eng.arp_octs : 1u); oct++) {
                for (k = 0; k < NKEYS; k++) {
                    if (arp_held[k] || (eng.arp_latch && arp_latch_buf[k]))
                        pool[cnt++] = (uint8_t)clamp(53 + (int32_t)k + 12 * set.octave + (int32_t)oct * 12, 0, 127);
                }
            }
            if (arp_cur_note >= 0) {
                sf_dsp_note_off((uint8_t)arp_cur_note);
                arp_cur_note = -1;
            }
            if (cnt) {
                uint8_t pick;
                if (eng.arp_mode == SF_ARP_DN) {
                    pick = pool[(cnt - 1u) - (arp_pos % cnt)];
                    arp_pos++;
                } else if (eng.arp_mode == SF_ARP_RND) {
                    pick = pool[(plat_ticks24() ^ arp_pos) % cnt];
                    arp_pos++;
                } else if (eng.arp_mode == SF_ARP_UPDN && cnt > 1u) {
                    if (arp_pos >= cnt) arp_pos = (uint8_t)(cnt - 1u);
                    pick = pool[arp_pos];
                    if (!arp_dir_dn) {
                        if (arp_pos + 1u >= cnt) arp_dir_dn = 1u;
                        else arp_pos++;
                    } else {
                        if (arp_pos == 0u) arp_dir_dn = 0u;
                        else arp_pos--;
                    }
                } else {
                    pick = pool[arp_pos % cnt];
                    arp_pos++;
                }
                sf_dsp_note_on(pick, 108u);
                arp_cur_note = (int8_t)pick;
            }
        }
    }
}

/* ------------------------------------------------------- Starfish Art --- */
static void draw_starfish_scene(int32_t cx, int32_t cy, uint8_t scene, uint32_t ms)
{
    uint16_t body = (scene == 0u) ? RGB(255, 107, 139)
                  : (scene == 1u) ? RGB(34, 211, 238)
                  : (scene == 2u) ? RGB(251, 191, 36)
                  : (scene == 3u) ? RGB(167, 139, 250)
                  : (scene == 4u) ? RGB(52, 211, 153)
                  : RGB(251, 146, 60);
    uint16_t belly = C_WHITE;
    int32_t bob = (int32_t)(((ms / 180u) & 3u) == 1u ? -2 : (((ms / 180u) & 3u) == 3u ? 2 : 0));
    int32_t y = cy + bob;
    /* 5-arm chunky pixel Starfish mascot */
    cv_rect(cx - 12, y - 10, 24, 20, body);
    cv_rect(cx - 6, y - 24, 12, 14, body);      /* top arm */
    cv_rect(cx - 26, y - 8, 14, 10, body);      /* left arm */
    cv_rect(cx + 12, y - 8, 14, 10, body);      /* right arm */
    cv_rect(cx - 18, y + 10, 12, 14, body);     /* leg L */
    cv_rect(cx + 6, y + 10, 12, 14, body);      /* leg R */
    /* Belly + eyes */
    cv_rect(cx - 7, y - 4, 14, 10, C_LINE);
    cv_rect(cx - 6, y - 5, 4, 5, C_BLACK);
    cv_rect(cx + 2, y - 5, 4, 5, C_BLACK);
    cv_pset(cx - 5, y - 4, belly);
    cv_pset(cx + 3, y - 4, belly);
    /* Cute smile */
    cv_line(cx - 4, y + 3, cx + 4, y + 3, C_BLACK);
    /* Scene prop */
    if (scene == 1u) {                          /* Moon Explorer visor + crescent */
        cv_rect(cx - 9, y - 8, 18, 8, C_HI);
        cv_rect(cx + 32, y - 22, 10, 10, C_AMB);
    } else if (scene == 2u) {                   /* Sub Captain periscope */
        cv_rect(cx + 24, y - 16, 4, 20, C_GRAY);
        cv_rect(cx + 24, y - 16, 10, 4, C_HI);
    } else if (scene == 3u) {                   /* Campfire */
        cv_rect(cx - 34, y + 12, 10, 8, C_AMB);
        cv_rect(cx - 32, y + 6, 6, 6, C_HI);
    } else if (scene == 4u) {                   /* Rocket Skateboard */
        cv_rect(cx - 22, y + 24, 44, 4, C_HI);
        cv_rect(cx - 16, y + 28, 6, 4, C_GRAY);
        cv_rect(cx + 10, y + 28, 6, 4, C_GRAY);
    } else if (scene == 5u) {                   /* Chef Hat */
        cv_rect(cx - 9, y - 32, 18, 8, C_WHITE);
    }
}

void app_splash(void)
{
    /* Called from main_fm1.c before fm1_timer5_start() and fm1_irq_enable_all():
     * plat_ms() is 0 here on hardware, so draw one crisp frame and return immediately. */
    palette_set(2u);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 144, C_BLACK);
    draw_starfish_scene(120, 62, 0u, 0u);
    cv_text(120 - text_w(&FONT_B, "STARFISH") / 2, 98, &FONT_B, "STARFISH", C_WHITE);
    cv_text(120 - text_w(&FONT_XS, MASCOT_TITLES[0]) / 2, 124, &FONT_XS, MASCOT_TITLES[0], C_HI);
    cv_blit(0, 48);
    lcd_sync();
}

/* ------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t vel[NKEYS], prs[NKEYS];
    int32_t last_key;
    uint8_t page;
    uint8_t edit_sub;       /* ENV: 0=amp,1=flt */
    uint8_t glo_page;       /* GLO: 0=scale/chord,1=rand,2=sys/ble */
    uint8_t seq_voice;      /* SEQ: 0..7 drum track */
    uint8_t save_choice;    /* SAVE: 0=save slot,1=init,2=randomize */
    uint32_t arp_t;
    uint32_t rec_t;
    uint32_t b25_t;         /* SEQ extreme top-right mute/clear hold */
    int32_t turns[NE];
    uint32_t turned_at[NE];
    uint8_t redraw;
} ui;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1250u;
    ui.redraw = 1u;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void note_name(int32_t note, char *b)
{
    char o[12];
    b[0] = 0;
    text_cat(b, NOTE12[(note % 12 + 12) % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void speak(void)
{
    char t[120], num[8];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". BANK ");
        num[0] = BANK_CHAR[set.bank % 10u];
        num[1] = 0;
        text_cat(t, num);
        text_cat(t, " ");
        int_text((int32_t)set.prog + 1, num);
        text_cat(t, num);
        text_cat(t, " ");
        text_cat(t, set.patch.name);
        text_cat(t, ". PAGE ");
        text_cat(t, PAGE_NAME[ui.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void ble_toggle(void)
{
    set.ble_off = (uint8_t)!set.ble_off;
    set_dirty = 1u;
    plat_ble_enable(!set.ble_off);
    app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}

static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && ui.page != PAGE_BLE) {
        ui.page = PAGE_BLE;
        app_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.page = PAGE_HOME;
    app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

static void step_bank(int32_t d)
{
    if (unsaved_random) {
        pending_nav_bank = (uint8_t)(((int32_t)set.bank + d + 10) % 10);
        pending_nav_dir = 1u;
        pending_nav_delta = (int8_t)d;
        ui.page = PAGE_RAND_ASK;
        app_say("UNSAVED RANDOM", "OCT+ SAVE");
        return;
    }
    uint8_t nb = (uint8_t)(((int32_t)set.bank + d + 10) % 10);
    load_preset(nb, 0u);
    set_dirty = 1u;
    char s[4] = {BANK_CHAR[nb], 0, 0, 0};
    app_say("BANK", s);
}

static void step_prog(int32_t d)
{
    if (unsaved_random) {
        pending_nav_dir = 2u;
        pending_nav_delta = (int8_t)d;
        ui.page = PAGE_RAND_ASK;
        app_say("UNSAVED RANDOM", "OCT+ SAVE");
        return;
    }
    int32_t len = (int32_t)bank_len(set.bank);
    uint8_t np = (uint8_t)(((int32_t)set.prog + d + len * 4) % len);
    load_preset(set.bank, np);
    set_dirty = 1u;
    char s[8];
    int_text((int32_t)np + 1, s);
    app_say(s, set.patch.name);
}

static void handle_knob(uint32_t k, int32_t d)
{
    char t[16];
    sf_preset_t *p = &set.patch;
    if (k == EN_SELECT) {
        if (ui.page == PAGE_SAVE) {
            ui.save_choice = (uint8_t)(((int32_t)ui.save_choice + d + 3) % 3);
            app_say("ACTION", ui.save_choice == 0u ? "SAVE BANK X" : ui.save_choice == 1u ? "INIT PATCH" : "RANDOMIZE");
        } else if (ui.page == PAGE_SEQ) {
            set.drum_bus = (uint8_t)(((int32_t)set.drum_bus + d + (int32_t)SF_NBUS) % (int32_t)SF_NBUS);
            sync_engine_cfg();
            set_dirty = 1u;
            app_say("DRUM BUS", BUS_NAMES[set.drum_bus]);
        } else {
            step_bank(d);
        }
        return;
    }
    if (k == EN_PRESET) {
        if (ui.page == PAGE_SEQ) {
            set.drum_kit = (uint8_t)(((int32_t)set.drum_kit + d + (int32_t)SF_DRUM_KITS) % (int32_t)SF_DRUM_KITS);
            sync_engine_cfg();
            set_dirty = 1u;
            app_say("DRUM KIT", KIT_NAMES[set.drum_kit]);
        } else if (ui.page == PAGE_SAVE && ui.save_choice == 0u) {
            int32_t ns = ((int32_t)set.prog + d + (int32_t)SF_BANK_X_SLOTS) % (int32_t)SF_BANK_X_SLOTS;
            set.bank = 9u;
            set.prog = (uint8_t)ns;
            int_text(ns + 1, t);
            app_say("BANK X", t);
        } else {
            step_prog(d);
        }
        return;
    }
    if (k == EN_ALGO) {
        if (FM1B_BLE_CENTRAL && ui.page == PAGE_BLE) {
            btdev_choose(btdev_pick() + d);
            ble_device_say();
        } else if (ui.page == PAGE_HOME) {
            set.mascot_idx = (uint8_t)(((int32_t)set.mascot_idx + d + 6) % 6);
            app_say("SCENE", MASCOT_TITLES[set.mascot_idx]);
        } else if (ui.page == PAGE_GLO) {
            ui.glo_page = (uint8_t)(((int32_t)ui.glo_page + d + 3) % 3);
            app_say("GLOBAL", ui.glo_page == 0u ? "SCALE/CHORD" : ui.glo_page == 1u ? "RANDOMIZER" : "SYSTEM");
        } else if (ui.page == PAGE_ENV) {
            ui.edit_sub = (uint8_t)!ui.edit_sub;
            app_say("ENV", ui.edit_sub ? "FILTER ENV" : "AMP ENV");
        } else if (ui.page == PAGE_FX) {
            uint8_t s = (uint8_t)((sf_sat_mode(p) + (uint32_t)d + 4u) & 3u);
            p->f2_type_route = (uint8_t)((p->f2_type_route & ~(3u << 5)) | (s << 5));
            sync_engine_cfg();
            set_dirty = 1u;
            app_say("SAT", SAT_NAMES[s]);
        } else if (ui.page == PAGE_LFO) {
            uint8_t sh = (uint8_t)((sf_lfo1_shape(p) + (uint32_t)d + 4u) & 3u);
            p->lfo1_meta = (uint8_t)((p->lfo1_meta & ~(3u << 4)) | (sh << 4));
            sync_engine_cfg();
            set_dirty = 1u;
            app_say("LFO1 SHP", LFO_SHAPES[sh]);
        } else {
            uint8_t rt = (uint8_t)((sf_f_route(p) + (uint32_t)d + 3u) % 3u);
            p->f2_type_route = (uint8_t)((p->f2_type_route & ~(3u << 2)) | (rt << 2));
            sync_engine_cfg();
            set_dirty = 1u;
            app_say("ROUTING", ROUTE_NAMES[rt]);
        }
        return;
    }

    /* K1..K4 (k = EN_K1..EN_K4 -> idx = 0..3) */
    uint32_t kn = k - EN_K1;
    set_dirty = 1u;
    if (ui.page == PAGE_HOME || ui.page == PAGE_SEL) {
        if (kn == 0u) {
            p->f1_cut = (uint8_t)clamp((int32_t)p->f1_cut + d * 3, 0, 127);
            int_text(p->f1_cut, t);
            app_say("CUTOFF", t);
        } else if (kn == 1u) {
            p->f1_res = (uint8_t)clamp((int32_t)p->f1_res + d * 3, 0, 127);
            int_text(p->f1_res, t);
            app_say("RESO", t);
        } else if (kn == 2u) {
            if (ui.page == PAGE_SEL) {
                p->f1_env = (uint8_t)clamp((int32_t)p->f1_env + d * 4, 0, 100);
                int_text(p->f1_env, t);
                app_say("FLT ENV", t);
            } else {
                p->fx_chorus = (uint8_t)clamp((int32_t)p->fx_chorus + d * 4, 0, 100);
                int_text(p->fx_chorus, t);
                app_say("CHORUS", t);
            }
        } else {
            if (ui.page == PAGE_SEL) {
                p->f2_cut = (uint8_t)clamp((int32_t)p->f2_cut + d * 3, 0, 127);
                int_text(p->f2_cut, t);
                app_say("FLT2 CUT", t);
            } else {
                uint8_t rv = (uint8_t)clamp((int32_t)sf_reverb_q(p) + d, 0, 15);
                p->fx_rvb_drv = (uint8_t)((p->fx_rvb_drv & 0xF0u) | rv);
                int_text((int32_t)rv * 7, t);
                app_say("REVERB", t);
            }
        }
    } else if (ui.page == PAGE_FX) {
        if (kn == 0u) {
            p->fx_chorus = (uint8_t)clamp((int32_t)p->fx_chorus + d * 4, 0, 100);
            int_text(p->fx_chorus, t);
            app_say("CHORUS", t);
        } else if (kn == 1u) {
            p->fx_phaser = (uint8_t)clamp((int32_t)p->fx_phaser + d * 4, 0, 100);
            int_text(p->fx_phaser, t);
            app_say("PHASER", t);
        } else if (kn == 2u) {
            uint8_t fb = (uint8_t)clamp((int32_t)sf_del_fb_q(p) + d, 0, 15);
            p->fx_delay = (uint8_t)((p->fx_delay & 0x0Fu) | (fb << 4));
            int_text((int32_t)fb * 6, t);
            app_say("DELAY FB", t);
        } else {
            uint8_t rv = (uint8_t)clamp((int32_t)sf_reverb_q(p) + d, 0, 15);
            p->fx_rvb_drv = (uint8_t)((p->fx_rvb_drv & 0xF0u) | rv);
            int_text((int32_t)rv * 7, t);
            app_say("REVERB", t);
        }
    } else if (ui.page == PAGE_ENV) {
        uint8_t *atk = ui.edit_sub ? &p->f_atk : &p->a_atk;
        uint8_t *dec = ui.edit_sub ? &p->f_dec : &p->a_dec;
        uint8_t *sus = ui.edit_sub ? &p->f_sus : &p->a_sus;
        uint8_t *rel = ui.edit_sub ? &p->f_rel : &p->a_rel;
        if (kn == 0u) { *atk = (uint8_t)clamp((int32_t)*atk + d * 3, 0, 127); int_text(*atk, t); app_say("ATTACK", t); }
        else if (kn == 1u) { *dec = (uint8_t)clamp((int32_t)*dec + d * 3, 0, 127); int_text(*dec, t); app_say("DECAY", t); }
        else if (kn == 2u) { *sus = (uint8_t)clamp((int32_t)*sus + d * 3, 0, 127); int_text(*sus, t); app_say("SUSTAIN", t); }
        else { *rel = (uint8_t)clamp((int32_t)*rel + d * 3, 0, 127); int_text(*rel, t); app_say("RELEASE", t); }
    } else if (ui.page == PAGE_LFO) {
        if (kn == 0u) {
            p->lfo1_rate = (uint8_t)clamp((int32_t)p->lfo1_rate + d * 3, 0, 127);
            int_text(p->lfo1_rate, t);
            app_say("LFO1 RATE", t);
        } else if (kn == 1u) {
            p->lfo1_depth = (uint8_t)clamp((int32_t)p->lfo1_depth + d * 3, 0, 127);
            int_text(p->lfo1_depth, t);
            app_say("LFO1 DPTH", t);
        } else if (kn == 2u) {
            uint8_t dest = (uint8_t)((sf_lfo1_dest(p) + (uint32_t)d + SF_NLFO_DEST) % SF_NLFO_DEST);
            p->lfo1_meta = (uint8_t)((p->lfo1_meta & 0xF0u) | dest);
            app_say("LFO1 DEST", LFO_NAMES[dest]);
        } else {
            uint8_t dest2 = (uint8_t)((sf_lfo2_dest(p) + (uint32_t)d + SF_NLFO_DEST) % SF_NLFO_DEST);
            p->lfo2_meta = (uint8_t)((p->lfo2_meta & 0xF0u) | dest2);
            app_say("LFO2 DEST", LFO_NAMES[dest2]);
        }
    } else if (ui.page == PAGE_EDIT) {
        if (kn == 0u) {
            p->osc_mode = (uint8_t)((p->osc_mode + (uint32_t)d + SF_NOSC) % SF_NOSC);
            app_say("OSC1 MODE", OSC_NAMES[p->osc_mode]);
        } else if (kn == 1u) {
            p->pw = (uint8_t)clamp((int32_t)p->pw + d * 3, 0, 127);
            int_text(p->pw, t);
            app_say("PW / WT", t);
        } else if (kn == 2u) {
            p->osc2_semi = (int8_t)clamp((int32_t)p->osc2_semi + d, -24, 24);
            int_text(p->osc2_semi, t);
            app_say("OSC2 SEMI", t);
        } else {
            p->sub_vol = (uint8_t)clamp((int32_t)p->sub_vol + d * 4, 0, 100);
            int_text(p->sub_vol, t);
            app_say("SUB VOL", t);
        }
    } else if (ui.page == PAGE_ARP) {
        if (kn == 0u) {
            set.arp_rate = (uint8_t)((set.arp_rate + (uint32_t)d + 4u) & 3u);
            app_say("ARP RATE", ARP_RATE_NAMES[set.arp_rate]);
        } else if (kn == 1u) {
            set.arp_mode = (uint8_t)((set.arp_mode + (uint32_t)d + SF_NARP_MODE) % SF_NARP_MODE);
            app_say("ARP MODE", ARP_MODE_NAMES[set.arp_mode]);
        } else if (kn == 2u) {
            set.arp_octs = (uint8_t)clamp((int32_t)set.arp_octs + d, 1, 3);
            int_text(set.arp_octs, t);
            app_say("ARP OCT", t);
        } else {
            set.arp_latch = (uint8_t)!set.arp_latch;
            app_say("ARP LATCH", set.arp_latch ? "ON" : "OFF");
        }
    } else if (ui.page == PAGE_SEQ) {
        if (kn == 0u) {
            set.bpm = (uint16_t)clamp((int32_t)set.bpm + d, 50, 220);
            int_text(set.bpm, t);
            app_say("BPM", t);
        } else if (kn == 1u) {
            set.swing = (uint8_t)clamp((int32_t)set.swing + d * 2, 0, 60);
            int_text(set.swing, t);
            app_say("SWING", t);
        } else if (kn == 2u) {
            set.drum_tune[ui.seq_voice] = (uint8_t)clamp((int32_t)set.drum_tune[ui.seq_voice] + d * 3, 0, 100);
            int_text(set.drum_tune[ui.seq_voice], t);
            app_say("DRUM TUNE", t);
        } else {
            set.drum_decay[ui.seq_voice] = (uint8_t)clamp((int32_t)set.drum_decay[ui.seq_voice] + d * 3, 0, 100);
            int_text(set.drum_decay[ui.seq_voice], t);
            app_say("DRUM DEC", t);
        }
    } else if (ui.page == PAGE_GLO) {
        if (ui.glo_page == 0u) {
            if (kn == 0u) { set.scale = (uint8_t)((set.scale + (uint32_t)d + SF_NSCALE) % SF_NSCALE); app_say("SCALE", SCALE_NAMES[set.scale]); }
            else if (kn == 1u) { set.root = (uint8_t)((set.root + (uint32_t)d + 12u) % 12u); app_say("ROOT", NOTE12[set.root]); }
            else if (kn == 2u) { set.chord_mode = (uint8_t)((set.chord_mode + (uint32_t)d + SF_NCHORD) % SF_NCHORD); app_say("CHORD", CHORD_NAMES[set.chord_mode]); }
            else { set.drum_vol = (uint8_t)clamp((int32_t)set.drum_vol + d * 4, 0, 100); int_text(set.drum_vol, t); app_say("DRUM VOL", t); }
        } else if (ui.glo_page == 1u) {
            if (kn == 0u) { set.rand_chaos = (uint8_t)clamp((int32_t)set.rand_chaos + d * 5, 0, 100); int_text(set.rand_chaos, t); app_say("CHAOS", t); }
            else if (kn == 1u) { set.rand_style = (uint8_t)((set.rand_style + (uint32_t)d + SF_NRAND_STYLE) % SF_NRAND_STYLE); app_say("RND STYLE", RAND_NAMES[set.rand_style]); }
            else if (kn >= 2u) { randomize_patch(); app_say("RANDOM", set.patch.name); }
        } else {
            if (kn == 0u) { set.palette = (uint8_t)((set.palette + (uint32_t)d + NPALETTES) % NPALETTES); palette_set(set.palette); app_say("THEME", PALETTES[set.palette].name); }
            else if (kn == 1u && FM1B_BLE) { ble_toggle(); }
            else { randomize_patch(); app_say("RANDOM", set.patch.name); }
        }
    }
    sync_engine_cfg();
}

static void key_note_action(uint32_t k, int down, uint32_t vel, uint32_t prs)
{
    int32_t raw_midi = 53 + (int32_t)k + 12 * set.octave;
    uint8_t qmidi = quantize_note(raw_midi);
    int8_t iv[4];
    uint32_t cnt = chord_intervals(iv), c;

    if (ui.page == PAGE_SEQ) {
        static const uint8_t WK[16] = PLAT_WHITE_KEYS;
        static const uint8_t BK[11] = PLAT_BLACK_KEYS;
        uint32_t i;
        if (down) {
            for (i = 0; i < 16u; i++) {
                if (WK[i] == k) {
                    set.seq_pat[ui.seq_voice][i] = set.seq_pat[ui.seq_voice][i] ? 0u : (vel >= 100u ? 2u : 1u);
                    sync_engine_cfg();
                    set_dirty = 1u;
                    ui.redraw = 1u;
                    return;
                }
            }
            for (i = 0; i < 8u; i++) {
                if (BK[i] == k) {
                    ui.seq_voice = (uint8_t)i;
                    sf_dsp_trigger_drum((uint8_t)i, (uint8_t)vel);
                    app_say("DRUM", DRUM_NAMES[i]);
                    return;
                }
            }
            if (BK[10] == k) {
                ui.b25_t = plat_ms();
                return;
            }
        } else if (BK[10] == k && ui.b25_t) {
            ui.b25_t = 0u;
            set.drum_mute ^= (uint8_t)(1u << ui.seq_voice);
            sync_engine_cfg();
            set_dirty = 1u;
            app_say(DRUM_NAMES[ui.seq_voice], (set.drum_mute >> ui.seq_voice) & 1u ? "MUTED" : "ACTIVE");
        }
        return;
    }

    if (arp_active) {
        arp_held[k] = (uint8_t)(down ? 1u : 0u);
        if (down && set.arp_latch)
            arp_latch_buf[k] = (uint8_t)!arp_latch_buf[k];
        if (down)
            ui.last_key = (int32_t)k;
        return;
    }

    if (down) {
        ui.last_key = (int32_t)k;
        for (c = 0; c < cnt; c++) {
            uint8_t mn = (uint8_t)clamp((int32_t)qmidi + iv[c], 0, 127);
            sf_dsp_note_on(mn, (uint8_t)vel);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)mn << 16 | vel << 24, TO_USB | TO_BLE);
        }
    } else if (prs > 0u) {
        for (c = 0; c < cnt; c++) {
            uint8_t mn = (uint8_t)clamp((int32_t)qmidi + iv[c], 0, 127);
            sf_dsp_aftertouch(mn, (uint8_t)prs);
            plat_midi_send(0x0Au | 0xA0u << 8 | (uint32_t)mn << 16 | prs << 24, TO_USB | TO_BLE);
        }
    } else {
        for (c = 0; c < cnt; c++) {
            uint8_t mn = (uint8_t)clamp((int32_t)qmidi + iv[c], 0, 127);
            sf_dsp_note_off(mn);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)mn << 16, TO_USB | TO_BLE);
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();
    keys = touch_keys();

    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0u;
            key_note_action(k, 1, v, 0u);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ui.prs[k] = 0u;
            key_note_action(k, 0, 0u, 0u);
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1u;
            key_note_action(k, 0, 0u, ui.prs[k]);
        }
    }

    if (ui.page == PAGE_SEQ && ui.b25_t && (keys & (1u << 25))) {
        if (plat_ms() - ui.b25_t >= 600u) {
            uint32_t s;
            ui.b25_t = 0u;
            for (s = 0; s < SF_SEQ_STEPS; s++)
                set.seq_pat[ui.seq_voice][s] = 0u;
            sync_engine_cfg();
            set_dirty = 1u;
            app_say(DRUM_NAMES[ui.seq_voice], "CLEARED");
        }
    }

    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;

        if (ui.page == PAGE_RAND_ASK) {
            if (k == B_OCTUP) {
                save_to_next_bankx();
                app_say("SAVED BANK X", set.patch.name);
                ui.page = PAGE_HOME;
                continue;
            }
            if (k == B_OCTDN) {
                unsaved_random = 0u;
                if (pending_nav_dir == 1u) step_bank(pending_nav_delta);
                else step_prog(pending_nav_delta);
                ui.page = PAGE_HOME;
                continue;
            }
            if (k == B_SEL || k == B_HOME) {
                ui.page = PAGE_HOME;
                app_say("KEPT", set.patch.name);
                continue;
            }
        }

        if (k == B_HOME) { ui.page = PAGE_HOME; app_say("HOME", set.patch.name); }
        else if (k == B_FX) { ui.page = PAGE_FX; app_say("FX", ""); }
        else if (k == B_SEL) { ui.page = PAGE_SEL; app_say("FILTER", FLT_NAMES[set.patch.f1_type & 3u]); }
        else if (k == B_ENV) { ui.page = PAGE_ENV; app_say("ENV", ui.edit_sub ? "FILTER" : "AMP"); }
        else if (k == B_LFO) { ui.page = PAGE_LFO; app_say("LFO", LFO_NAMES[sf_lfo1_dest(&set.patch)]); }
        else if (k == B_EDIT) { ui.page = PAGE_EDIT; app_say("EDIT", OSC_NAMES[set.patch.osc_mode % SF_NOSC]); }
        else if (k == B_GLO) {
            if (ui.page == PAGE_GLO) ui.glo_page = (uint8_t)((ui.glo_page + 1u) % 3u);
            else ui.page = PAGE_GLO;
            app_say("GLOBAL", ui.glo_page == 0u ? "SCALE" : ui.glo_page == 1u ? "RANDOM" : "SYSTEM");
        }
        else if (k == B_PLAY) {
            seq_playing = (uint8_t)!seq_playing;
            if (!seq_playing)
                seq_step = 0u;
            tick_acc_us = 0u;
            app_say("SEQ", seq_playing ? "PLAY" : "STOP");
        }
        else if (k == B_ARP) { ui.arp_t = plat_ms(); }
        else if (k == B_SEQ) { ui.page = (ui.page == PAGE_SEQ) ? PAGE_HOME : PAGE_SEQ; app_say("SEQ", KIT_NAMES[set.drum_kit]); }
        else if (k == B_OCTDN) {
            if (btn & (1u << B_ARP)) {
                set.arp_latch = (uint8_t)!set.arp_latch;
                ui.arp_t = 0u;
                sync_engine_cfg();
                app_say("LATCH", set.arp_latch ? "ON" : "OFF");
            } else {
                set.octave = (int8_t)clamp((int32_t)set.octave - 1, -2, 2);
                sf_dsp_panic();
                set_dirty = 1u;
                char o[4] = {set.octave < 0 ? '-' : '+', (char)('0' + (set.octave < 0 ? -set.octave : set.octave)), 0, 0};
                app_say("OCTAVE", o);
            }
        }
        else if (k == B_OCTUP) {
            if (btn & (1u << B_ARP)) {
                set.arp_latch = (uint8_t)!set.arp_latch;
                ui.arp_t = 0u;
                sync_engine_cfg();
                app_say("LATCH", set.arp_latch ? "ON" : "OFF");
            } else {
                set.octave = (int8_t)clamp((int32_t)set.octave + 1, -2, 2);
                sf_dsp_panic();
                set_dirty = 1u;
                char o[4] = {set.octave < 0 ? '-' : '+', (char)('0' + (set.octave < 0 ? -set.octave : set.octave)), 0, 0};
                app_say("OCTAVE", o);
            }
        }
        else if (k == B_SAVE) {
            if (ui.page == PAGE_GLO && ui.glo_page == 1u) {
                randomize_patch();
                app_say("RANDOM", set.patch.name);
            } else if (ui.page != PAGE_SAVE) {
                ui.page = PAGE_SAVE;
                ui.save_choice = 0u;
                app_say("SAVE", "PRESS SAVE");
            } else {
                if (ui.save_choice == 0u) {
                    save_to_next_bankx();
                    app_say("SAVED", set.patch.name);
                } else if (ui.save_choice == 1u) {
                    make_init_patch(&set.patch, "INIT PATCH");
                    sync_engine_cfg();
                    app_say("INIT", "PATCH");
                } else {
                    randomize_patch();
                    app_say("RANDOM", set.patch.name);
                }
                ui.page = PAGE_HOME;
            }
        }
        else if (k == B_REC && FM1B_BLE) {
            ui.rec_t = plat_ms();
        }
    }

    if (ui.arp_t) {
        if (btn & (1u << B_ARP)) {
            if (plat_ms() - ui.arp_t >= 450u) {
                ui.arp_t = 0u;
                ui.page = PAGE_ARP;
                app_say("ARP PAGE", ARP_MODE_NAMES[set.arp_mode]);
            }
        } else {
            ui.arp_t = 0u;
            arp_active = (uint8_t)!arp_active;
            if (!arp_active) {
                mem_zero((void *)arp_latch_buf, (uint32_t)sizeof(arp_latch_buf));
                if (arp_cur_note >= 0) { sf_dsp_note_off((uint8_t)arp_cur_note); arp_cur_note = -1; }
            }
            app_say("ARP", arp_active ? "ON" : "OFF");
        }
    }

    if (FM1B_BLE && ui.rec_t) {
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0u;
                ble_toggle();
            }
        } else {
            ui.rec_t = 0u;
            if (FM1B_BLE_CENTRAL && !set.ble_off)
                ble_device_button();
            else
                app_say("BLE", set.ble_off ? "OFF" : "READY");
        }
    }

    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        ui.turned_at[k] = plat_ms();
        ui.redraw = 1u;
        handle_knob(k, e);
    }

    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1u;
    ui.keys = keys;
    ui.btn = btn;

    /* LED choreography */
    uint32_t led_btn = btn | (set_dirty ? (1u << B_SAVE) : 0u) | (arp_active ? (1u << B_ARP) : 0u) | (seq_playing ? (1u << B_PLAY) : 0u);
    uint32_t led_keys = keys;
    if (ui.page == PAGE_SEQ) {
        static const uint8_t WK[16] = PLAT_WHITE_KEYS;
        uint32_t s;
        led_keys = 1u << (uint32_t)ui.seq_voice;
        for (s = 0; s < SF_SEQ_STEPS; s++) {
            if (set.seq_pat[ui.seq_voice][s])
                led_keys |= (1u << WK[s]);
            if (seq_playing && s == seq_step)
                led_keys ^= (1u << WK[s]);
        }
    }
    plat_glow(1);
    plat_play_red(0);
    plat_leds(led_btn, led_keys);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------- Drawing --- */
static void draw_bar(int32_t x, int32_t y, const char *lbl, const char *val, int32_t pct)
{
    int32_t w = 108;
    int32_t fill = clamp(pct, 0, 100) * (w - 4) / 100;
    cv_text(x, y, &FONT_XS, lbl, C_GRAY);
    cv_text(x + w - text_w(&FONT_XS, val), y, &FONT_XS, val, C_WHITE);
    cv_rect(x, y + 14, w, 6, C_LINE);
    if (fill > 0)
        cv_rect(x + 2, y + 16, fill, 2, C_HI);
}

static void draw_header(uint32_t now)
{
    char t[24];
    cv_begin(240, 34, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 7, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 7, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t x;
        char code[12];
        code[0] = BANK_CHAR[set.bank % 10u];
        code[1] = 0;
        int_text((int32_t)set.prog + 1, t);
        text_cat(code, t);
        x = cv_text(8, 7, &FONT_B, code, C_HI);
        cv_text(x + 6, 9, &FONT_S, set.patch.name, C_WHITE);
        if (FM1B_BLE && plat_ble_connected())
            cv_text(204, 10, &FONT_XS, "BLE", C_HI);
    }
    cv_line(0, 32, 239, 32, C_LINE);
    cv_blit(0, 0);
}

static void draw_main(uint32_t now)
{
    char b1[16], b2[16], b3[16], b4[16];
    sf_preset_t *p = &set.patch;
    cv_begin(240, 144, C_BLACK);

    if (ui.page == PAGE_RAND_ASK) {
        cv_text(20, 18, &FONT_B, "UNSAVED RANDOM!", C_HI);
        cv_text(20, 48, &FONT_S, p->name, C_WHITE);
        cv_text(20, 78, &FONT_XS, "OCT+ : SAVE TO BANK X", C_AMB);
        cv_text(20, 96, &FONT_XS, "OCT- : DISCARD", C_GRAY);
        cv_text(20, 114, &FONT_XS, "SEL  : KEEP EDITING", C_DIM);
    } else if (ui.page == PAGE_SAVE) {
        cv_text(12, 12, &FONT_B, "SAVE / UTILITY", C_HI);
        cv_text(12, 42, &FONT_S, ui.save_choice == 0u ? "> SAVE TO BANK X" : "  SAVE TO BANK X", ui.save_choice == 0u ? C_WHITE : C_GRAY);
        cv_text(12, 66, &FONT_S, ui.save_choice == 1u ? "> INIT PATCH" : "  INIT PATCH", ui.save_choice == 1u ? C_WHITE : C_GRAY);
        cv_text(12, 90, &FONT_S, ui.save_choice == 2u ? "> RANDOMIZE" : "  RANDOMIZE", ui.save_choice == 2u ? C_WHITE : C_GRAY);
        cv_text(12, 122, &FONT_XS, "TURN SELECT OR PRESS SAVE", C_DIM);
    } else if (ui.page == PAGE_SEQ) {
        uint32_t s;
        cv_text(8, 4, &FONT_S, KIT_NAMES[set.drum_kit], C_HI);
        cv_text(110, 4, &FONT_XS, DRUM_NAMES[ui.seq_voice], C_WHITE);
        cv_text(165, 4, &FONT_XS, BUS_NAMES[set.drum_bus], C_AMB);
        for (s = 0; s < SF_SEQ_STEPS; s++) {
            int32_t x = 8 + (int32_t)s * 14;
            uint8_t st = set.seq_pat[ui.seq_voice][s];
            uint16_t col = (seq_playing && s == seq_step) ? C_WHITE : (st == 2u ? C_HI : st == 1u ? C_AMB : C_LINE);
            cv_rect(x, 26, 12, 22, col);
        }
        int_text(set.bpm, b1); int_text(set.swing, b2);
        int_text(set.drum_tune[ui.seq_voice], b3); int_text(set.drum_decay[ui.seq_voice], b4);
        draw_bar(8, 66, "K1 BPM", b1, ((int32_t)set.bpm - 50) * 100 / 170);
        draw_bar(124, 66, "K2 SWING", b2, (int32_t)set.swing * 100 / 60);
        draw_bar(8, 102, "K3 TUNE", b3, set.drum_tune[ui.seq_voice]);
        draw_bar(124, 102, "K4 DECAY", b4, set.drum_decay[ui.seq_voice]);
    } else if (ui.page == PAGE_HOME) {
        draw_starfish_scene(120, 44, set.mascot_idx, now);
        cv_text(8, 6, &FONT_XS, OSC_NAMES[p->osc_mode % SF_NOSC], C_GRAY);
        cv_text(170, 6, &FONT_XS, FLT_NAMES[p->f1_type & 3u], C_GRAY);
        int_text(p->f1_cut, b1); int_text(p->f1_res, b2);
        int_text(p->fx_chorus, b3); int_text((int32_t)sf_reverb_q(p) * 7, b4);
        draw_bar(8, 86, "K1 CUT", b1, (int32_t)p->f1_cut * 100 / 127);
        draw_bar(124, 86, "K2 RES", b2, (int32_t)p->f1_res * 100 / 127);
        draw_bar(8, 116, "K3 CHOR", b3, p->fx_chorus);
        draw_bar(124, 116, "K4 VERB", b4, (int32_t)sf_reverb_q(p) * 100 / 15);
    } else if (ui.page == PAGE_FX) {
        cv_text(8, 6, &FONT_S, "FX & SAT", C_HI);
        cv_text(165, 6, &FONT_XS, SAT_NAMES[sf_sat_mode(p)], C_WHITE);
        int_text(p->fx_chorus, b1); int_text(p->fx_phaser, b2);
        int_text((int32_t)sf_del_fb_q(p) * 6, b3); int_text((int32_t)sf_reverb_q(p) * 7, b4);
        draw_bar(8, 42, "K1 CHOR", b1, p->fx_chorus);
        draw_bar(124, 42, "K2 PHAS", b2, p->fx_phaser);
        draw_bar(8, 88, "K3 DLY", b3, (int32_t)sf_del_fb_q(p) * 100 / 15);
        draw_bar(124, 88, "K4 VERB", b4, (int32_t)sf_reverb_q(p) * 100 / 15);
    } else if (ui.page == PAGE_SEL) {
        cv_text(8, 6, &FONT_S, "FILTER", C_HI);
        cv_text(140, 6, &FONT_XS, ROUTE_NAMES[sf_f_route(p)], C_WHITE);
        int_text(p->f1_cut, b1); int_text(p->f1_res, b2);
        int_text(p->f1_env, b3); int_text(p->f2_cut, b4);
        draw_bar(8, 42, "K1 CUT1", b1, (int32_t)p->f1_cut * 100 / 127);
        draw_bar(124, 42, "K2 RES1", b2, (int32_t)p->f1_res * 100 / 127);
        draw_bar(8, 88, "K3 ENV", b3, p->f1_env);
        draw_bar(124, 88, "K4 CUT2", b4, (int32_t)p->f2_cut * 100 / 127);
    } else if (ui.page == PAGE_ENV) {
        uint8_t atk = ui.edit_sub ? p->f_atk : p->a_atk;
        uint8_t dec = ui.edit_sub ? p->f_dec : p->a_dec;
        uint8_t sus = ui.edit_sub ? p->f_sus : p->a_sus;
        uint8_t rel = ui.edit_sub ? p->f_rel : p->a_rel;
        cv_text(8, 6, &FONT_S, ui.edit_sub ? "FILTER ENV" : "AMP ENV", C_HI);
        int_text(atk, b1); int_text(dec, b2); int_text(sus, b3); int_text(rel, b4);
        draw_bar(8, 42, "K1 ATK", b1, (int32_t)atk * 100 / 127);
        draw_bar(124, 42, "K2 DEC", b2, (int32_t)dec * 100 / 127);
        draw_bar(8, 88, "K3 SUS", b3, (int32_t)sus * 100 / 127);
        draw_bar(124, 88, "K4 REL", b4, (int32_t)rel * 100 / 127);
    } else if (ui.page == PAGE_LFO) {
        cv_text(8, 6, &FONT_S, "DUAL LFO", C_HI);
        cv_text(145, 6, &FONT_XS, LFO_SHAPES[sf_lfo1_shape(p)], C_WHITE);
        int_text(p->lfo1_rate, b1); int_text(p->lfo1_depth, b2);
        draw_bar(8, 42, "K1 RATE", b1, (int32_t)p->lfo1_rate * 100 / 127);
        draw_bar(124, 42, "K2 DPTH", b2, (int32_t)p->lfo1_depth * 100 / 127);
        draw_bar(8, 88, "K3 DST1", LFO_NAMES[sf_lfo1_dest(p)], (int32_t)sf_lfo1_dest(p) * 100 / 10);
        draw_bar(124, 88, "K4 DST2", LFO_NAMES[sf_lfo2_dest(p)], (int32_t)sf_lfo2_dest(p) * 100 / 10);
    } else if (ui.page == PAGE_EDIT) {
        cv_text(8, 6, &FONT_S, "OSC & VOICE", C_HI);
        int_text(p->pw, b2); int_text(p->osc2_semi, b3); int_text(p->sub_vol, b4);
        draw_bar(8, 42, "K1 MODE", OSC_NAMES[p->osc_mode % SF_NOSC], (int32_t)p->osc_mode * 100 / 5);
        draw_bar(124, 42, "K2 PW/WT", b2, (int32_t)p->pw * 100 / 127);
        draw_bar(8, 88, "K3 SEMI", b3, ((int32_t)p->osc2_semi + 24) * 100 / 48);
        draw_bar(124, 88, "K4 SUB", b4, p->sub_vol);
    } else if (ui.page == PAGE_ARP) {
        cv_text(8, 6, &FONT_S, "ARPEGGIATOR", C_HI);
        cv_text(165, 6, &FONT_XS, arp_active ? "ON" : "OFF", C_WHITE);
        int_text(set.arp_octs, b3);
        draw_bar(8, 42, "K1 RATE", ARP_RATE_NAMES[set.arp_rate], (int32_t)set.arp_rate * 100 / 3);
        draw_bar(124, 42, "K2 MODE", ARP_MODE_NAMES[set.arp_mode], (int32_t)set.arp_mode * 100 / 3);
        draw_bar(8, 88, "K3 OCT", b3, (int32_t)set.arp_octs * 100 / 3);
        draw_bar(124, 88, "K4 LTCH", set.arp_latch ? "ON" : "OFF", set.arp_latch ? 100 : 0);
    } else if (ui.page == PAGE_GLO) {
        cv_text(8, 6, &FONT_S, "GLOBAL", C_HI);
        if (ui.glo_page == 0u) {
            int_text(set.drum_vol, b4);
            draw_bar(8, 42, "K1 SCL", SCALE_NAMES[set.scale], (int32_t)set.scale * 100 / 11);
            draw_bar(124, 42, "K2 ROOT", NOTE12[set.root], (int32_t)set.root * 100 / 11);
            draw_bar(8, 88, "K3 CHRD", CHORD_NAMES[set.chord_mode], (int32_t)set.chord_mode * 100 / 4);
            draw_bar(124, 88, "K4 DRMV", b4, set.drum_vol);
        } else if (ui.glo_page == 1u) {
            int_text(set.rand_chaos, b1);
            draw_bar(8, 42, "K1 CHAO", b1, set.rand_chaos);
            draw_bar(124, 42, "K2 STYL", RAND_NAMES[set.rand_style], (int32_t)set.rand_style * 100 / 5);
            cv_text(8, 96, &FONT_S, "PRESS SAVE TO RANDOMIZE", C_WHITE);
        } else {
            draw_bar(8, 42, "K1 THME", PALETTES[set.palette].name, (int32_t)set.palette * 100 / 4);
            draw_bar(124, 42, "K2 BLE", set.ble_off ? "OFF" : "ON", set.ble_off ? 0 : 100);
            cv_text(8, 96, &FONT_XS, "STARFISH FIRMWARE ON FM1-BASE", C_GRAY);
        }
    } else if (ui.page == PAGE_BLE) {
        char bt[36];
        btdev_title(bt);
        cv_text(8, 6, &FONT_S, bt, C_HI);
        cv_text(8, 52, &FONT_XS, "ALGO PICK · REC CONNECT", C_WHITE);
    }
    cv_blit(0, 36);
}

static void draw_footer(void)
{
    char t[16];
    uint32_t i, pct = plat_master() * 100u / 4096u;
    int32_t note = sf_dsp_last_note();
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    cv_begin(240, 56, C_BLACK);
    cv_line(0, 0, 239, 0, C_LINE);
    for (i = 0; i < 16u; i++) {
        uint32_t k = WK[i];
        cv_rect(4 + (int32_t)i * 14, 16, 13, 14, (ui.keys >> k & 1u) ? C_HI : C_DIM);
    }
    for (i = 0; i < 11u; i++) {
        static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
        uint32_t k = BK[i];
        cv_rect(4 + (int32_t)AT[i] * 14 + 8, 4, 11, 12, (ui.keys >> k & 1u) ? C_WHITE : C_LINE);
    }
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
    cv_text(4, 38, &FONT_XS, "OCT", C_GRAY);
    cv_text(28, 38, &FONT_XS, t, C_HI);
    cv_text(58, 38, &FONT_XS, PAGE_NAME[ui.page], C_WHITE);
    if (note >= 0) {
        note_name(note, t);
        cv_text(132, 38, &FONT_XS, t, C_HI);
    }
    cv_text(178, 38, &FONT_XS, "VOL", C_GRAY);
    int_text((int32_t)pct, t);
    text_cat(t, "%");
    cv_text(204, 38, &FONT_XS, t, C_WHITE);
    cv_blit(0, 184);
}

void app_ui_init(void)
{
    touch_on(1);
    palette_set(set.palette);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1u;
}

void app_frame(void)
{
    static uint32_t last_pct, last_step, last_midi;
    uint32_t now = plat_ms(), pct = plat_master() * 100u / 4096u;
    input();
    speak();

    if (FM1B_BLE_CENTRAL) {
        int ev;
        btdev_page(ui.page == PAGE_BLE && !set.ble_off);
        while ((ev = btdev_event()) != BTDEV_NONE)
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }

    if (midi_count != last_midi || pct != last_pct || (seq_playing && seq_step != last_step) || (ui.msg_until && now >= ui.msg_until)) {
        last_midi = midi_count;
        last_pct = pct;
        last_step = seq_step;
        if (now >= ui.msg_until)
            ui.msg_until = 0u;
        ui.redraw = 1u;
    }

    if (ui.redraw) {
        ui.redraw = 0u;
        draw_header(now);
        draw_main(now);
        draw_footer();
    }
}

/* ----------------------------------------------------- Backup hooks --- */
int app_backup_playing(void) { return seq_playing ? 1 : 0; }
void app_backup_flush(void)
{
    if (set_dirty && !backup_hold) {
        plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
        plat_store_save(OBJ_BANKX, &bankx, (uint32_t)sizeof(bankx));
        set_dirty = 0u;
    }
}
void app_backup_hold(int on) { backup_hold = (uint8_t)(on ? 1u : 0u); }
void app_backup_restored(void)
{
    load_all_from_store();
    ui.redraw = 1u;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave")) *got = set.octave;
    else if (!strcmp(what, "bank")) *got = set.bank;
    else if (!strcmp(what, "prog")) *got = set.prog;
    else if (!strcmp(what, "note")) *got = sf_dsp_last_note();
    else if (!strcmp(what, "dirty")) *got = set_dirty;
    else if (!strcmp(what, "playing")) *got = seq_playing;
    else if (!strcmp(what, "arp")) *got = arp_active;
    else if (!strcmp(what, "page")) *got = ui.page;
    else if (!strcmp(what, "vel")) *got = ui.vel[ui.last_key];
    else if (!strcmp(what, "prs")) *got = ui.prs[ui.last_key];
    else if (!strncmp(what, "turns", 5)) *got = ui.turns[atoi(what + 5) % NE];
    else return 0;
    return 1;
}
#endif
