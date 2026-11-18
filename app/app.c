/* SPDX-License-Identifier: MPL-2.0 */
#include "fastmath.h"
#include "dsp.h"
#include "presets_rom.h"

#define SET_MAGIC   0x53545332u
#define BANKX_MAGIC 0x53545832u
#define SYNTH_LOOP_MAX 32u

enum {
    PAGE_HOME = 0, PAGE_FX, PAGE_SEL, PAGE_ENV, PAGE_LFO, PAGE_EDIT,
    PAGE_GLO, PAGE_ARP, PAGE_SEQ, PAGE_SAVE, PAGE_RAND_ASK, PAGE_BLE, PAGE_RESET
};

typedef struct {
    uint32_t magic;
    uint8_t bank;                       /* 0..7=A..H, 8=I, 9=X */
    uint8_t prog;                       /* 0..127 */
    int8_t octave;                      /* -2..+2 */
    uint8_t palette;                    /* 0..4 */
    uint8_t ble_off;                    /* 0..1 */
    uint16_t bpm;                       /* 40..240 */
    uint8_t swing;                      /* 50..75 (%) */
    uint8_t seq_length;                 /* 1..64 */
    uint8_t drum_kit;                   /* 0..9 */
    uint8_t drum_vol;                   /* 0..100 */
    uint8_t drum_bus;                   /* 0..5 */
    uint8_t drum_preset;                /* 0=TECHNO,1=HOUSE,2=ELECTRO,3=BREAK,4=CLEAR */
    uint8_t scale;                      /* 0..11 */
    uint8_t root;                       /* 0..11 */
    uint8_t chord_mode;                 /* 0..4 */
    uint8_t arp_mode;                   /* 0..3 */
    uint8_t arp_rate;                   /* 0..3 */
    uint8_t arp_octs;                   /* 1..3 */
    uint8_t arp_latch;                  /* 0..1 */
    uint8_t arp_gate;                   /* 20..95 */
    uint8_t rand_chaos;                 /* 20..100 */
    uint8_t rand_style;                 /* 0..7 */
    uint8_t mascot_idx;                 /* 0..5 */
    uint8_t seq_pat[SF_DRUM_VOICES][SF_SEQ_STEPS];
    uint8_t drum_mute;
    int8_t drum_tune[SF_DRUM_VOICES];   /* -12..+12 */
    uint8_t drum_decay[SF_DRUM_VOICES]; /* 25..200 */
    uint8_t drum_tone[SF_DRUM_VOICES];  /* 0..100 */
    uint8_t drum_level[SF_DRUM_VOICES]; /* 0..100 */
    uint8_t euclid_pulses[SF_DRUM_VOICES];
    uint8_t euclid_rot[SF_DRUM_VOICES];
    sf_preset_t patch;
} sf_settings_t;

typedef struct {
    uint32_t magic;
    uint8_t used[SF_BANK_X_CHUNK];
    sf_preset_t slots[SF_BANK_X_CHUNK];
} sf_bankx_chunk_t;

_Static_assert(sizeof(sf_settings_t) <= PLAT_STORE_MAX, "sf_settings_t exceeds store object size");
_Static_assert(sizeof(sf_bankx_chunk_t) <= PLAT_STORE_MAX, "sf_bankx_chunk_t exceeds store object size");

typedef struct {
    uint8_t step;
    uint8_t note;
    uint8_t vel;
    uint8_t len_steps;
} sf_loop_ev_t;

static sf_settings_t set;
static sf_bankx_chunk_t bankx_chunks[3] PLAT_POOL;
static sf_engine_cfg_t eng;
static sf_dsp_vis_t vis;

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
static volatile uint32_t arp_gate_us;

static sf_loop_ev_t synth_loop[SYNTH_LOOP_MAX];
static volatile uint8_t synth_loop_cnt;
static volatile uint8_t rec_armed;
static volatile int8_t loop_active_note = -1;
static volatile uint8_t loop_active_left;

static uint8_t set_dirty;
static uint8_t unsaved_random;
static uint8_t pending_nav_dir;
static int8_t pending_nav_delta;
static uint16_t rand_count;
static uint8_t target_bankx_slot;
static volatile uint8_t backup_hold;
static uint32_t rand_seed = 0x9E3779B9u;

static const char *const PAGE_NAME[] = {
    "HOME", "FX", "FILTER", "ENV", "LFO", "EDIT", "GLOBAL", "ARP", "SEQ", "SAVE", "RANDOM", "BLE", "RESET"
};
static const uint8_t PAGE_SUBS[13] = {1u, 2u, 3u, 2u, 3u, 3u, 3u, 2u, 3u, 1u, 1u, 1u, 1u};
static const char *const BANK_CHAR = "ABCDEFGHIX";
static const uint8_t OSC_ORDER[SF_NOSC] = {
    SF_OSC_HYPERSAW, SF_OSC_SAW, SF_OSC_PULSE, SF_OSC_WT, SF_OSC_SYNC, SF_OSC_TRI
};
static const char *const OSC_NAMES[SF_NOSC] = {"HYPRSAW", "SAW", "PULSE", "TRI", "SYNC", "WAVETBL"};
static const char *const SUB_NAMES[3] = {"SQR", "SIN", "SAW"};
static const char *const FLT_NAMES[4] = {"LP", "BP", "HP", "BS"};
static const char *const ROUTE_NAMES[3] = {"SER", "PAR", "SPL"};
static const char *const SAT_NAMES[4] = {"SOFT", "TUBE", "HARD", "OFF"};
static const char *const LFO_NAMES[SF_NLFO_DEST] = {
    "CUT1", "CUT2", "RESO", "PITCH", "PAN", "SPRD", "AMP", "CHOR", "PHAS", "DLY", "VERB"
};
static const char *const LFO_SHAPES[4] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const SCALE_NAMES[SF_NSCALE] = {
    "CHROM", "MAJOR", "MINOR", "HARM MIN", "MEL MIN", "DORIAN",
    "PHRYG", "LYDIAN", "MIXOLYD", "LOCRIAN", "MAJ PENT", "MIN PENT"
};
static const char *const CHORD_NAMES[SF_NCHORD] = {"OFF", "OCT", "5TH", "MIN7", "MAJ7"};
static const char *const ARP_MODE_NAMES[SF_NARP_MODE] = {"UP", "DOWN", "UP/DN", "RAND"};
static const char *const ARP_RATE_NAMES[4] = {"1/8", "1/16", "1/16T", "1/32"};
static const char *const KIT_NAMES[SF_DRUM_KITS] = {
    "TR-808", "TR-909", "CR-78", "LINN", "DMX", "FM-PERC", "ACID-9", "INDUS", "LO-FI", "STARFISH"
};
static const char *const DRUM_NAMES[SF_DRUM_VOICES] = {
    "BD", "SD", "CH", "OH", "CP", "TM", "RD", "RS"
};
static const char *const BUS_NAMES[SF_NBUS] = {"CLEAN", "WARM", "COMP", "SUB+", "ROOM", "SPRK"};
static const char *const DRUM_PRESET_NAMES[5] = {"TECHNO", "HOUSE", "ELECTRO", "BREAK", "CLEAR"};
static const char *const RAND_NAMES[SF_NRAND_STYLE] = {
    "ALL", "PAD", "LEAD", "BASS", "PLUCK", "ARP", "KEYS", "FX"
};
static const char *const MASCOT_TITLES[6] = {
    "BEACH CHILLIN", "MOON EXPLORER", "SUB CAPTAIN", "CAMPFIRE JAM", "SKATE KICKFLIP", "STARFISH CHEF"
};
static const char *const JINGLE_NAMES[6] = {
    "REEF CHIME", "LUNAR ARPS", "SONAR PING", "CAMPFIRE", "SKATE POP", "CHEF BOUNCE"
};
static const char *const NOTE12[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static uint32_t xorshift32(void)
{
    uint32_t x = rand_seed ^ plat_ticks24();
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
    while (n--) *d++ = *s++;
}

static void mem_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = 0u;
}

static void text_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}

static uint8_t bankx_is_used(uint32_t slot)
{
    if (slot >= SF_BANK_X_SLOTS) return 0u;
    return bankx_chunks[slot / SF_BANK_X_CHUNK].used[slot % SF_BANK_X_CHUNK];
}

static sf_preset_t *bankx_slot_ptr(uint32_t slot)
{
    if (slot >= SF_BANK_X_SLOTS) slot = 0u;
    return &bankx_chunks[slot / SF_BANK_X_CHUNK].slots[slot % SF_BANK_X_CHUNK];
}

static uint32_t bankx_used_count(void)
{
    uint32_t i, c = 0u;
    for (i = 0; i < SF_BANK_X_SLOTS; i++)
        if (bankx_is_used(i)) c++;
    return c;
}

static uint8_t bankx_next_free(void)
{
    uint32_t i;
    for (i = 0; i < SF_BANK_X_SLOTS; i++)
        if (!bankx_is_used(i)) return (uint8_t)i;
    return 0u;
}

static void sync_engine_cfg(void)
{
    uint32_t f = plat_lock();
    mem_copy(&eng.patch, &set.patch, (uint32_t)sizeof(sf_preset_t));
    eng.bpm = set.bpm;
    eng.swing = set.swing;
    eng.seq_length = set.seq_length;
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
    eng.arp_gate = set.arp_gate;
    eng.rand_chaos = set.rand_chaos;
    eng.rand_style = set.rand_style;
    mem_copy(eng.seq_pat, set.seq_pat, (uint32_t)sizeof(set.seq_pat));
    eng.drum_mute = set.drum_mute;
    mem_copy(eng.drum_tune, set.drum_tune, (uint32_t)sizeof(set.drum_tune));
    mem_copy(eng.drum_decay, set.drum_decay, (uint32_t)sizeof(set.drum_decay));
    mem_copy(eng.drum_tone, set.drum_tone, (uint32_t)sizeof(set.drum_tone));
    mem_copy(eng.drum_level, set.drum_level, (uint32_t)sizeof(set.drum_level));
    sf_dsp_set_cfg(&eng);
    plat_unlock(f);
}

static uint32_t bank_len(uint8_t bank)
{
    if (bank < 8u) return SF_BANK_SIZE;
    if (bank == 8u) return SF_BANK_I_SIZE;
    return SF_BANK_X_SLOTS;
}

static const sf_preset_t *get_preset_ptr(uint8_t bank, uint8_t prog)
{
    if (bank < 8u) return &SF_ROM[(uint32_t)bank * SF_BANK_SIZE + (prog % SF_BANK_SIZE)];
    if (bank == 8u) return &SF_ROM[1024u + (prog % SF_BANK_I_SIZE)];
    return bankx_slot_ptr(prog % SF_BANK_X_SLOTS);
}

static void load_preset(uint8_t bank, uint8_t prog)
{
    set.bank = bank % 10u;
    set.prog = (uint8_t)(prog % bank_len(set.bank));
    mem_copy(&set.patch, get_preset_ptr(set.bank, set.prog), (uint32_t)sizeof(sf_preset_t));
    set.arp_mode = sf_arp_mode(&set.patch);
    set.arp_rate = sf_arp_rate(&set.patch);
    set.arp_octs = sf_arp_octs(&set.patch);
    set.arp_latch = sf_arp_latch(&set.patch);
    arp_active = sf_arp_on(&set.patch);
    unsaved_random = 0u;
    sync_engine_cfg();
}

static void make_init_patch(sf_preset_t *p, const char *label)
{
    uint32_t i;
    mem_zero(p, (uint32_t)sizeof(*p));
    for (i = 0; i < 15u && label[i]; i++) p->name[i] = label[i];
    p->osc_meta = SF_OSC_HYPERSAW;
    p->pw = 64u;
    p->osc2_detune = 14u;
    p->osc_bal = 50u;
    p->sub_vol = 60u;
    p->unison_poly = 1u | (7u << 3);
    p->spread = 76u;
    p->uni_pan = 75u;
    p->f1_cut = 86u;
    p->f1_res = 26u;
    p->f2_cut = 112u;
    p->f2_res = 12u;
    p->f1_env_s = 42;
    p->f_bal = 50u;
    p->f_keyfollow = 35u;
    p->f2_type_route = (SF_FLT_LP) | (SF_FLT_LP << 2) | (SF_ROUTE_SER << 4) | (SF_SAT_SOFT << 6);
    p->a_atk = 4u;
    p->a_dec = 68u;
    p->a_sus = 96u;
    p->a_rel = 44u;
    p->f_atk = 4u;
    p->f_dec = 52u;
    p->f_sus = 45u;
    p->f_rel = 44u;
    p->lfo1_rate = 28u;
    p->lfo1_depth = 18u;
    p->lfo2_rate = 12u;
    p->lfo2_depth = 12u;
    p->lfo2_meta = (SF_LFO_PAN) | (1u << 4);
    p->swarm_depth = 22u;
    p->swarm_rate = 8u;
    p->fx_chorus = 45u;
    p->fx_phaser_rate = 5u;
    p->fx_del_mix = 28u;
    p->fx_del_time = 30u;
    p->fx_del_fb = 34u;
    p->fx_reverb = 42u;
    p->fx_drive = 15u;
    p->arp_meta = (SF_ARP_UPDN) | (1u << 2) | (0u << 4);
}

static void bankx_defaults(void)
{
    uint32_t c, i;
    mem_zero(bankx_chunks, (uint32_t)sizeof(bankx_chunks));
    for (c = 0; c < 3u; c++) bankx_chunks[c].magic = BANKX_MAGIC;
    for (i = 0; i < SF_BANK_X_SLOTS; i++) {
        char nm[16] = "USER ";
        char d[6];
        int_text((int32_t)(i + 1u), d);
        text_cat(nm, d);
        make_init_patch(bankx_slot_ptr(i), nm);
    }
}

static void apply_euclidean(uint8_t v, uint8_t pulses, uint8_t rot)
{
    uint8_t n = set.seq_length ? set.seq_length : 16u;
    uint8_t k = pulses > n ? n : pulses;
    uint8_t i, bucket = 0u;
    set.euclid_pulses[v] = k;
    set.euclid_rot[v] = (uint8_t)(rot % n);
    for (i = 0; i < SF_SEQ_STEPS; i++) set.seq_pat[v][i] = 0u;
    if (k == 0u) return;
    for (i = 0; i < n; i++) {
        uint8_t idx = (uint8_t)((i + set.euclid_rot[v]) % n);
        bucket = (uint8_t)(bucket + k);
        if (bucket >= n) {
            bucket = (uint8_t)(bucket - n);
            set.seq_pat[v][idx] = ((idx & 3u) == 0u) ? 2u : 1u;
        }
    }
}

static void apply_drum_preset(uint8_t preset_idx)
{
    static const uint16_t MASK[4][8] = {
        {0x1111u, 0x1010u, 0xD54Du, 0x0404u, 0x1010u, 0x4080u, 0x1111u, 0x0808u}, /* TECHNO */
        {0x1111u, 0x0000u, 0x3333u, 0x4444u, 0x1010u, 0x8808u, 0x4444u, 0x8080u}, /* HOUSE */
        {0x4449u, 0x1010u, 0xD555u, 0x0800u, 0x9010u, 0x0282u, 0x0101u, 0x2020u}, /* ELECTRO */
        {0x0449u, 0x9810u, 0x5555u, 0x0080u, 0x1010u, 0x2220u, 0x1041u, 0x4004u}  /* BREAK */
    };
    uint32_t v, s;
    set.drum_preset = (uint8_t)(preset_idx % 5u);
    mem_zero(set.seq_pat, (uint32_t)sizeof(set.seq_pat));
    for (v = 0; v < SF_DRUM_VOICES; v++) {
        uint8_t cnt = 0u;
        uint16_t m = (preset_idx < 4u) ? MASK[preset_idx][v] : 0u;
        for (s = 0; s < SF_SEQ_STEPS; s++) {
            uint8_t b = (uint8_t)(s & 15u);
            if ((m >> b) & 1u) {
                set.seq_pat[v][s] = (b == 0u || b == 4u || b == 12u) ? 2u : 1u;
                if (s < set.seq_length) cnt++;
            }
        }
        set.euclid_pulses[v] = cnt;
        set.euclid_rot[v] = 0u;
    }
}

static void default_drums(void)
{
    uint32_t v;
    set.drum_mute = 0u;
    for (v = 0; v < SF_DRUM_VOICES; v++) {
        set.drum_tune[v] = 0;
        set.drum_decay[v] = 100u;
        set.drum_tone[v] = 60u;
        set.drum_level[v] = 90u;
    }
    apply_drum_preset(4u); /* CLEAR on boot, matching simulator */
}

static void settings_defaults(void)
{
    mem_zero(&set, (uint32_t)sizeof(set));
    set.magic = SET_MAGIC;
    set.bank = 8u;
    set.prog = 0u;
    set.octave = 0;
    set.palette = 2u;
    set.ble_off = 0u;
    set.bpm = 124u;
    set.swing = 50u;
    set.seq_length = 16u;
    set.drum_kit = 0u;
    set.drum_vol = 85u;
    set.drum_bus = 0u;
    set.scale = SF_SCALE_CHR;
    set.root = 0u;
    set.chord_mode = SF_CHORD_OFF;
    set.arp_mode = SF_ARP_UPDN;
    set.arp_rate = 1u;
    set.arp_octs = 1u;
    set.arp_latch = 0u;
    set.arp_gate = 72u;
    set.rand_chaos = 60u;
    set.rand_style = SF_RAND_ALL;
    set.mascot_idx = 0u;
    default_drums();
    load_preset(8u, 0u);
    target_bankx_slot = bankx_next_free();
}

int starfish_ble_off(void) { return set.ble_off; }

static void load_all_from_store(void)
{
    uint32_t c, bad_bx = 0u;
    for (c = 0; c < 3u; c++) {
        if (plat_store_load(OBJ_BANKX0 + c, &bankx_chunks[c], (uint32_t)sizeof(sf_bankx_chunk_t))
            != (int)sizeof(sf_bankx_chunk_t) || bankx_chunks[c].magic != BANKX_MAGIC) {
            bad_bx = 1u;
        }
    }
    if (bad_bx) bankx_defaults();
    if (plat_store_load(OBJ_SETTINGS, &set, (uint32_t)sizeof(set)) != (int)sizeof(set) || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.bank >= 10u) {
        settings_defaults();
    } else {
        set.bpm = (uint16_t)clamp((int32_t)set.bpm, 40, 240);
        set.swing = (uint8_t)clamp((int32_t)set.swing, 50, 75);
        set.seq_length = (uint8_t)clamp((int32_t)set.seq_length, 1, 64);
        set.palette %= NPALETTES;
        palette_set(set.palette);
        target_bankx_slot = bankx_next_free();
        sync_engine_cfg();
    }
}

/* ----------------------------------------------------------- Scale/Chord --- */
static uint8_t quantize_note(int32_t midi)
{
    static const uint16_t MASKS[SF_NSCALE] = {
        0x0FFFu, /* Chromatic */
        0x0AB5u, /* Major */
        0x05ADu, /* Natural Minor */
        0x09ADu, /* Harmonic Minor */
        0x0AADu, /* Melodic Minor */
        0x06B5u, /* Dorian */
        0x05ABu, /* Phrygian */
        0x0AD5u, /* Lydian */
        0x06B5u, /* Mixolydian */
        0x056Bu, /* Locrian */
        0x0295u, /* Major Pentatonic */
        0x04A9u  /* Minor Pentatonic */
    };
    uint16_t mask = MASKS[set.scale % SF_NSCALE];
    int32_t d;
    if (set.scale == SF_SCALE_CHR) return (uint8_t)clamp(midi, 0, 127);
    for (d = 0; d <= 2; d++) {
        int32_t up = midi + d;
        int32_t pc = ((up - (int32_t)set.root) % 12 + 12) % 12;
        if ((mask >> pc) & 1u) return (uint8_t)clamp(up, 0, 127);
        int32_t dn = midi - d;
        pc = ((dn - (int32_t)set.root) % 12 + 12) % 12;
        if ((mask >> pc) & 1u) return (uint8_t)clamp(dn, 0, 127);
    }
    return (uint8_t)clamp(midi, 0, 127);
}

static uint32_t chord_intervals(int8_t out[4])
{
    out[0] = 0;
    if (set.chord_mode == SF_CHORD_OCT) { out[1] = 12; return 2u; }
    if (set.chord_mode == SF_CHORD_5TH) { out[1] = 7; out[2] = 12; return 3u; }
    if (set.chord_mode == SF_CHORD_MIN7) { out[1] = 3; out[2] = 7; out[3] = 10; return 4u; }
    if (set.chord_mode == SF_CHORD_MAJ7) { out[1] = 4; out[2] = 7; out[3] = 11; return 4u; }
    return 1u;
}

/* ----------------------------------------------------- Safe Randomizer --- */
static void randomize_patch(void)
{
    static const char *const R_PREFIX[6] = {"STAR ", "NEBULA ", "REEF ", "CORAL ", "PULSE ", "DRIFT "};
    static const int8_t SEMI_STD[5] = {0, 0, 7, 12, -12};
    uint32_t r = xorshift32(), r2 = xorshift32(), r3 = xorshift32();
    sf_preset_t *p = &set.patch;
    uint32_t c = clamp((int32_t)set.rand_chaos, 20, 100);
    uint8_t st = (set.rand_style == SF_RAND_ALL) ? (uint8_t)(1u + (r % 7u)) : set.rand_style;
    char num[6];

    rand_count++;
    mem_zero(p, (uint32_t)sizeof(*p));
    text_cat(p->name, R_PREFIX[(r >> 3) % 6u]);
    int_text((int32_t)(rand_count % 100u), num);
    text_cat(p->name, num);

    uint8_t osc = (uint8_t)((st == SF_RAND_BASS) ? ((r >> 5) & 3u) : ((r >> 5) % SF_NOSC));
    p->osc_meta = (uint8_t)((osc & 7u) | ((osc == SF_OSC_SYNC ? 1u : 0u) << 3) | (((r >> 8) % 3u) << 4));
    p->pw = (uint8_t)(24u + ((r >> 10) % 80u));
    p->osc2_semi = SEMI_STD[(r >> 12) % 5u];
    p->osc2_detune = (uint8_t)(6u + ((r >> 15) % (10u + c / 4u)));
    p->osc_bal = (uint8_t)(28u + ((r >> 18) % 48u));
    p->sub_vol = (uint8_t)((st == SF_RAND_BASS) ? (65u + ((r >> 20) % 30u)) : (25u + ((r >> 20) % 50u)));
    p->fm_amt = (uint8_t)((osc == SF_OSC_SYNC || st == SF_RAND_FX) ? (8u + (r2 % (c / 3u))) : 0u);
    p->ring_vol = (uint8_t)((st == SF_RAND_FX || st == SF_RAND_KEYS) ? ((r2 >> 4) % (c / 4u)) : 0u);
    uint8_t uni = (uint8_t)((st == SF_RAND_BASS) ? (1u + ((r2 >> 6) & 1u)) : (1u + ((r2 >> 6) % 4u)));
    p->unison_poly = (uint8_t)(((uni - 1u) & 7u) | (7u << 3));
    p->spread = (uint8_t)(40u + ((r2 >> 8) % 55u));
    p->uni_pan = 75u;

    uint8_t f1t = (uint8_t)((st == SF_RAND_BASS) ? SF_FLT_LP : ((c > 70u && ((r2 >> 10) & 3u) == 0u) ? SF_FLT_BP : SF_FLT_LP));
    uint8_t rt = (uint8_t)((c > 50u) ? ((r2 >> 12) % 3u) : ((r2 >> 12) & 1u));
    uint8_t f2t = (uint8_t)((rt == SF_ROUTE_SPL) ? ((r2 >> 14) % 3u) : ((r2 >> 14) & 1u));
    p->f2_type_route = (uint8_t)((f1t & 3u) | ((f2t & 3u) << 2) | ((rt & 3u) << 4) | (SF_SAT_SOFT << 6));
    p->f1_cut = (uint8_t)((st == SF_RAND_BASS) ? (38u + ((r2 >> 16) % 36u)) : (52u + ((r2 >> 16) % 54u)));
    uint8_t max_q = (uint8_t)((f1t == SF_FLT_BP) ? 46u : 58u);
    p->f1_res = (uint8_t)(12u + ((r2 >> 20) % (10u + (max_q * c) / 100u)));
    p->f2_cut = (uint8_t)(72u + ((r2 >> 24) % 48u));
    p->f2_res = 20u;
    p->f1_env_s = (int8_t)((st == SF_RAND_FX) ? ((int32_t)((r2 >> 26) % 90u) - 30) : (20 + (int32_t)((r2 >> 26) % 45u)));
    p->f_bal = 50u;
    p->f_keyfollow = 35u;

    p->a_atk = (uint8_t)((st == SF_RAND_PAD) ? (42u + (r3 % 48u)) : (2u + (r3 % 10u)));
    p->a_dec = (uint8_t)(38u + ((r3 >> 4) % 58u));
    p->a_sus = (uint8_t)((st == SF_RAND_PLUCK) ? (14u + ((r3 >> 8) % 24u)) : (64u + ((r3 >> 8) % 48u)));
    p->a_rel = (uint8_t)((st == SF_RAND_PAD) ? (58u + ((r3 >> 12) % 48u)) : (24u + ((r3 >> 12) % 38u)));
    p->f_atk = (uint8_t)((st == SF_RAND_PAD) ? 36u : 4u);
    p->f_dec = 48u;
    p->f_sus = 45u;
    p->f_rel = p->a_rel;

    uint8_t l1d = (uint8_t)((r3 >> 16) % SF_NLFO_DEST);
    uint8_t l2d = (uint8_t)((l1d + 1u + ((r3 >> 20) % (SF_NLFO_DEST - 1u))) % SF_NLFO_DEST);
    p->lfo1_rate = (uint8_t)(14u + ((r3 >> 22) % 62u));
    p->lfo1_depth = (uint8_t)(l1d == SF_LFO_PITCH ? (6u + (c / 10u)) : (18u + ((r3 >> 24) % (24u + c / 3u))));
    p->lfo1_meta = (uint8_t)((l1d & 15u) | (((r3 >> 26) & 3u) << 4));
    p->lfo2_rate = (uint8_t)(10u + ((r >> 24) % 48u));
    p->lfo2_depth = (uint8_t)(l2d == SF_LFO_PITCH ? 8u : (14u + ((r >> 26) % 36u)));
    p->lfo2_meta = (uint8_t)((l2d & 15u) | (1u << 4));
    p->swarm_depth = 22u;
    p->swarm_rate = 8u;
    p->fx_chorus = (uint8_t)((st == SF_RAND_BASS) ? (8u + (r % 28u)) : (28u + (r % 52u)));
    p->fx_phaser = (uint8_t)((st == SF_RAND_PAD || st == SF_RAND_FX) ? (20u + ((r >> 6) % 45u)) : 0u);
    p->fx_phaser_rate = 5u;
    p->fx_del_mix = (uint8_t)((st == SF_RAND_BASS) ? 12u : (22u + ((r >> 12) % 34u)));
    p->fx_del_time = 30u;
    p->fx_del_fb = 34u;
    p->fx_reverb = (uint8_t)((st == SF_RAND_BASS) ? 14u : (32u + ((r >> 16) % 44u)));
    p->fx_drive = 15u;

    unsaved_random = 1u;
    set_dirty = 1u;
    sync_engine_cfg();
}

static void save_to_bankx_slot(uint8_t slot)
{
    uint32_t c;
    slot %= SF_BANK_X_SLOTS;
    c = slot / SF_BANK_X_CHUNK;
    bankx_chunks[c].magic = BANKX_MAGIC;
    bankx_chunks[c].used[slot % SF_BANK_X_CHUNK] = 1u;
    mem_copy(bankx_slot_ptr(slot), &set.patch, (uint32_t)sizeof(sf_preset_t));
    set.bank = 9u;
    set.prog = slot;
    unsaved_random = 0u;
    target_bankx_slot = bankx_next_free();
    plat_store_save(OBJ_BANKX0 + c, &bankx_chunks[c], (uint32_t)sizeof(sf_bankx_chunk_t));
    plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
    set_dirty = 0u;
}

/* ------------------------------------------------------------- MIDI & Audio --- */
static volatile uint32_t midi_count;

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        (void)port;
        if (st != 0xF8u && st != 0xFEu) midi_count++;
        if ((st & 0xF0u) == 0x90u && d2) sf_dsp_note_on((uint8_t)d1, (uint8_t)d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) sf_dsp_note_off((uint8_t)d1);
        else if ((st & 0xF0u) == 0xA0u) sf_dsp_aftertouch((uint8_t)d1, (uint8_t)d2);
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
    rec_armed = 0u;
    if (loop_active_note >= 0) {
        sf_dsp_note_off((uint8_t)loop_active_note);
        loop_active_note = -1;
    }
    sf_dsp_panic();
}

/* ----------------------------------------------------- 1 kHz Sequencer --- */
void app_tick(void)
{
    uint32_t bpm = eng.bpm ? eng.bpm : 120u;
    uint32_t base_step_us = 15000000u / bpm;

    if (seq_playing) {
        uint32_t sw = clamp((int32_t)eng.swing, 50, 75) - 50;
        uint32_t step_us = (seq_step & 1u) ? (base_step_us - (base_step_us * sw) / 100u)
                                           : (base_step_us + (base_step_us * sw) / 100u);
        tick_acc_us += 1000u;
        if (tick_acc_us >= step_us) {
            uint8_t st = seq_step, v, len = eng.seq_length ? eng.seq_length : 16u;
            uint8_t i;
            tick_acc_us -= step_us;
            if (loop_active_note >= 0) {
                if (loop_active_left > 1u) {
                    loop_active_left--;
                } else {
                    sf_dsp_note_off((uint8_t)loop_active_note);
                    loop_active_note = -1;
                }
            }
            for (v = 0; v < SF_DRUM_VOICES; v++) {
                uint8_t hit = eng.seq_pat[v][st];
                if (hit) sf_dsp_trigger_drum(v, hit == 2u ? 127u : 96u);
            }
            for (i = 0; i < synth_loop_cnt; i++) {
                if (synth_loop[i].step == st) {
                    if (loop_active_note >= 0) sf_dsp_note_off((uint8_t)loop_active_note);
                    sf_dsp_note_on(synth_loop[i].note, synth_loop[i].vel);
                    loop_active_note = (int8_t)synth_loop[i].note;
                    loop_active_left = synth_loop[i].len_steps ? synth_loop[i].len_steps : 1u;
                    break;
                }
            }
            seq_step = (uint8_t)((st + 1u) % len);
        }
    }

    if (arp_active) {
        /* arp_rate: 0=1/8 (2x), 1=1/16 (1x), 2=1/16T (2/3x), 3=1/32 (0.5x) */
        uint32_t arp_us = (eng.arp_rate == 0u) ? (base_step_us * 2u)
                        : (eng.arp_rate == 2u) ? ((base_step_us * 2u) / 3u)
                        : (eng.arp_rate == 3u) ? (base_step_us >> 1)
                        : base_step_us;
        if (arp_cur_note >= 0 && arp_gate_us > 0u) {
            if (arp_gate_us <= 1000u) {
                sf_dsp_note_off((uint8_t)arp_cur_note);
                arp_cur_note = -1;
                arp_gate_us = 0u;
            } else {
                arp_gate_us -= 1000u;
            }
        }
        arp_tick_us += 1000u;
        if (arp_tick_us >= arp_us) {
            uint8_t pool[NKEYS * 3u], cnt = 0u, k, oct;
            uint8_t octs = eng.arp_octs ? eng.arp_octs : 1u;
            arp_tick_us -= arp_us;
            for (oct = 0; oct < octs; oct++) {
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
                arp_gate_us = (arp_us * (uint32_t)clamp((int32_t)eng.arp_gate, 20, 95)) / 100u;
            }
        }
    }
}
/* ---------------------------------------------------------- UI State & Boot --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t hud_until;
    uint32_t keys, btn;
    uint8_t vel[NKEYS], prs[NKEYS];
    uint8_t last_vel, last_prs;
    int8_t last_key;
    uint8_t page, sub_page;
    uint8_t seq_voice;
    uint8_t seq_bar;
    uint8_t seq_cursor;
    uint8_t save_choice;     /* 0=BANK X SLOT, 1=INIT PATCH, 2=RANDOMIZE */
    uint8_t save_slot;
    uint32_t arp_t, rec_t, save_t, b25_t;
    uint8_t arp_held_btn, rec_held_btn, save_held_btn, b25_held;
    uint8_t arp_combo;
    uint8_t last_knob;
    uint32_t knob_until;
    uint32_t save_flash_until;
    int32_t turns[NE];
    uint8_t redraw;
} ui;

static uint32_t boot_until;
static uint8_t boot_act;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1u;
}

static void note_name(uint8_t midi, char out[8])
{
    int32_t oct = ((int32_t)midi / 12) - 1;
    char ob[6];
    out[0] = 0;
    text_cat(out, NOTE12[midi % 12u]);
    int_text(oct, ob);
    text_cat(out, ob);
}

static void show_hud(uint8_t knob_id, const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.last_knob = knob_id;
    ui.knob_until = plat_ms() + 1000u;
    ui.hud_until = plat_ms() + 1000u;
    ui.redraw = 1u;
}

/* 6-Act Cute Starfish Mascot Pixel Art (matching BootSplashStage.tsx) */
static void draw_starfish_mascot(int32_t cx, int32_t cy, int32_t bob, uint8_t act, uint32_t phase)
{
    static const int8_t ARM_DX[5] = {0, 12, 8, -8, -12};
    static const int8_t ARM_DY[5] = {-13, -3, 11, 11, -3};
    uint16_t coral = RGB(255, 112, 112);
    uint16_t belly = RGB(255, 166, 154);
    uint16_t cheek = RGB(255, 130, 160);
    int32_t a, y = cy + bob;

    /* Starfish 5 arms */
    for (a = 0; a < 5; a++) {
        int32_t ax = cx + ARM_DX[a], ay = y + ARM_DY[a];
        cv_line(cx, y, ax, ay, coral);
        cv_line(cx - 1, y, ax - 1, ay, coral);
        cv_line(cx + 1, y, ax + 1, ay, coral);
        cv_rect(ax - 2, ay - 2, 5, 5, coral);
    }
    cv_rect(cx - 7, y - 6, 15, 13, coral);
    cv_rect(cx - 5, y - 4, 11, 9, belly);

    /* Sunglasses or cute glossy eyes */
    if (act == 0u || act == 4u) {
        cv_rect(cx - 6, y - 3, 5, 3, C_BLACK);
        cv_rect(cx + 2, y - 3, 5, 3, C_BLACK);
        cv_line(cx - 1, y - 2, cx + 1, y - 2, C_BLACK);
    } else {
        cv_rect(cx - 4, y - 3, 2, 3, C_BLACK);
        cv_rect(cx + 3, y - 3, 2, 3, C_BLACK);
        cv_pset(cx - 4, y - 3, C_WHITE);
        cv_pset(cx + 3, y - 3, C_WHITE);
    }
    /* Rosy cheeks + smile */
    cv_rect(cx - 6, y + 1, 2, 1, cheek);
    cv_rect(cx + 5, y + 1, 2, 1, cheek);
    cv_line(cx - 2, y + 2, cx + 2, y + 2, C_BLACK);

    /* Act accessories */
    if (act == 0u) { /* Beach Chillin: umbrella + wave */
        cv_line(cx + 16, y + 12, cx + 16, y - 14, RGB(210, 180, 120));
        cv_rect(cx + 6, y - 16, 20, 4, RGB(255, 80, 120));
        cv_rect(cx + 12, y - 18, 8, 3, C_WHITE);
    } else if (act == 1u) { /* Moon Explorer: helmet + visor + flag */
        cv_rect(cx - 10, y - 9, 21, 1, RGB(190, 235, 255));
        cv_rect(cx - 10, y + 8, 21, 1, RGB(190, 235, 255));
        cv_line(cx + 18, y + 10, cx + 18, y - 12, C_GRAY);
        cv_rect(cx + 19, y - 12, 10, 6, C_HI);
    } else if (act == 2u) { /* Yellow Sub: periscope + bubbles */
        cv_rect(cx - 18, y - 6, 36, 16, RGB(255, 210, 50));
        cv_rect(cx - 7, y - 12, 14, 7, RGB(255, 210, 50));
        cv_rect(cx - 5, y - 4, 11, 9, belly);
        cv_rect(cx - 4, y - 3, 2, 3, C_BLACK);
        cv_rect(cx + 3, y - 3, 2, 3, C_BLACK);
        cv_rect(cx + 22, y - 6 - (int32_t)(phase & 7u), 3, 3, C_HI);
        cv_rect(cx + 28, y - 12 - (int32_t)(phase & 3u), 2, 2, C_WHITE);
    } else if (act == 3u) { /* Campfire Jam: mini guitar + fire */
        cv_line(cx - 8, y + 5, cx + 12, y - 4, RGB(220, 160, 80));
        cv_rect(cx - 12, y + 3, 7, 5, RGB(200, 110, 50));
        cv_rect(cx + 18, y + 4, 6, 7, RGB(255, 120, 20));
        cv_rect(cx + 20, y + 1 - (int32_t)(phase & 3u), 3, 4, RGB(255, 220, 70));
    } else if (act == 4u) { /* Skate Kickflip: board under feet */
        cv_rect(cx - 15, y + 13, 30, 3, C_AMB);
        cv_rect(cx - 11, y + 16, 4, 3, C_WHITE);
        cv_rect(cx + 7, y + 16, 4, 3, C_WHITE);
    } else { /* Starfish Chef: toque hat + frying pan */
        cv_rect(cx - 6, y - 14, 12, 7, C_WHITE);
        cv_rect(cx - 8, y - 18, 16, 5, C_WHITE);
        cv_line(cx + 10, y + 5, cx + 22, y + 5, C_GRAY);
        cv_rect(cx + 18, y + 2, 9, 4, C_LINE);
    }
}

static void draw_boot_splash(uint32_t now)
{
    uint32_t phase = now / 40u;
    int32_t bob = ((phase & 3u) == 1u) ? -2 : ((phase & 3u) == 3u) ? 1 : 0;
    const char *act_nm = MASCOT_TITLES[boot_act % 6u];
    const char *j_nm = JINGLE_NAMES[boot_act % 6u];
    int32_t bar_w = (int32_t)((now * 160u) / 550u);
    if (bar_w > 160) bar_w = 160;

    /* Strip 0: Top 120px */
    cv_begin(240u, 120u, C_BLACK);
    cv_rect(0, 0, 240, 20, RGB(14, 18, 28));
    cv_text(8, 3, &FONT_XS, "STARFISH OS v2.0", C_HI);
    cv_text(232 - text_w(&FONT_XS, j_nm), 3, &FONT_XS, j_nm, C_AMB);
    cv_line(0, 20, 240, 20, C_LINE);
    /* Scene backdrop inside upper half */
    if (boot_act == 0u) {
        cv_rect(185, 30, 14, 14, RGB(255, 220, 90));
        cv_rect(0, 102, 240, 18, RGB(20, 80, 140));
    } else if (boot_act == 1u) {
        cv_pset(30, 38, C_WHITE); cv_pset(62, 54, C_HI); cv_pset(200, 42, C_WHITE);
        cv_rect(0, 102, 240, 18, RGB(46, 48, 56));
    } else if (boot_act == 2u) {
        cv_rect(0, 22, 240, 98, RGB(10, 34, 58));
    }
    draw_starfish_mascot(120, 72, bob, boot_act, phase);
    cv_blit(0u, 0u);

    /* Strip 1: Bottom 120px */
    cv_begin(240u, 120u, C_BLACK);
    cv_oy = -120;
    cv_text((240 - text_w(&FONT_L, "STARFISH")) / 2, 134, &FONT_L, "STARFISH", C_WHITE);
    cv_text((240 - text_w(&FONT_S, act_nm)) / 2, 174, &FONT_S, act_nm, C_HI);
    cv_text((240 - text_w(&FONT_XS, "VIRUS VA + 10 DRUM KITS + EUCLID")) / 2, 194, &FONT_XS,
            "VIRUS VA + 10 DRUM KITS + EUCLID", C_GRAY);
    cv_rect(40, 214, 160, 6, C_LINE);
    cv_rect(40, 214, bar_w, 6, C_HI);
    cv_blit(0u, 120u);
    lcd_sync();
}

void app_init(void)
{
    sf_dsp_init(&eng);
    load_all_from_store();
    boot_act = (uint8_t)(xorshift32() % 6u);
    set.mascot_idx = boot_act;
    boot_until = 550u;
    sf_dsp_start_jingle(boot_act, &set.patch);
}

void app_splash(void)
{
    draw_boot_splash(0u);
}

/* ----------------------------------------------------------- Bank/MIDI --- */
static void send_midi_all(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t cin = (st >> 4) & 0x0Fu;
    uint32_t pkt = ((uint32_t)d2 << 24) | ((uint32_t)d1 << 16) | ((uint32_t)st << 8) | cin;
    plat_midi_send(pkt, TO_USB | TO_BLE);
}

static void ble_toggle(void)
{
    set.ble_off = set.ble_off ? 0u : 1u;
    plat_ble_enable(!set.ble_off);
    plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
    app_say("BLE MIDI", set.ble_off ? "OFF" : "ON");
}

static void step_bank(int32_t d)
{
    if (unsaved_random) {
        pending_nav_dir = 1u;
        pending_nav_delta = (int8_t)d;
        ui.page = PAGE_RAND_ASK;
        ui.redraw = 1u;
        return;
    }
    uint8_t nb = (uint8_t)(((int32_t)set.bank + d + 10) % 10);
    uint8_t np = (nb == 9u && bankx_used_count() > 0u) ? 0u : set.prog;
    load_preset(nb, np);
    set_dirty = 1u;
    app_say("BANK", set.patch.name);
}

static void step_prog(int32_t d)
{
    if (unsaved_random) {
        pending_nav_dir = 2u;
        pending_nav_delta = (int8_t)d;
        ui.page = PAGE_RAND_ASK;
        ui.redraw = 1u;
        return;
    }
    uint32_t bl = bank_len(set.bank);
    uint8_t np = (uint8_t)(((int32_t)set.prog + d + (int32_t)bl) % (int32_t)bl);
    load_preset(set.bank, np);
    set_dirty = 1u;
    app_say("PRESET", set.patch.name);
}

static void commit_pending_nav(void)
{
    unsaved_random = 0u;
    if (pending_nav_dir == 1u) step_bank(pending_nav_delta);
    else if (pending_nav_dir == 2u) step_prog(pending_nav_delta);
    pending_nav_dir = 0u;
}

static void set_arp_flag_from_state(void)
{
    set.patch.arp_meta = (uint8_t)((set.arp_mode & 3u)
        | ((set.arp_rate & 3u) << 2)
        | (((clamp((int32_t)set.arp_octs, 1, 3) - 1) & 3u) << 4)
        | ((set.arp_latch & 1u) << 6)
        | ((arp_active & 1u) << 7));
}

/* ----------------------------------------------------------- Knob Routing --- */
static void handle_knob(uint32_t k, int32_t d)
{
    sf_preset_t *p = &set.patch;
    char vbuf[24];
    int32_t v;
    vbuf[0] = 0;

    if (ui.page == PAGE_SAVE) {
        if (k == EN_SELECT) {
            ui.save_choice = (uint8_t)(((int32_t)ui.save_choice + d + 3) % 3);
            show_hud((uint8_t)k, "SAVE MODE",
                     ui.save_choice == 0u ? "BANK X SLOT" : ui.save_choice == 1u ? "INIT PATCH" : "RANDOMIZE");
        } else if (k == EN_PRESET || k == EN_K1) {
            target_bankx_slot = (uint8_t)(((int32_t)target_bankx_slot + d + (int32_t)SF_BANK_X_SLOTS) % (int32_t)SF_BANK_X_SLOTS);
            int_text((int32_t)(target_bankx_slot + 1u), vbuf);
            show_hud((uint8_t)k, "BANK X SLOT", vbuf);
        }
        return;
    }

    if (ui.page == PAGE_BLE) {
        if (k == EN_ALGO || k == EN_PRESET || k == EN_K1) {
            int cnt = btdev_count();
            if (cnt > 0) {
                btdev_choose((btdev_pick() + d + cnt * 4) % cnt);
                ui.redraw = 1u;
            }
        }
        return;
    }

    if (ui.page == PAGE_SEQ) {
        if (k == EN_SELECT) {
            ui.seq_voice = (uint8_t)(((int32_t)ui.seq_voice + d + (int32_t)SF_DRUM_VOICES) % (int32_t)SF_DRUM_VOICES);
            show_hud((uint8_t)k, "DRUM VOICE", DRUM_NAMES[ui.seq_voice]);
            return;
        }
        if (k == EN_PRESET) {
            set.drum_kit = (uint8_t)(((int32_t)set.drum_kit + d + (int32_t)SF_DRUM_KITS) % (int32_t)SF_DRUM_KITS);
            set_dirty = 1u; sync_engine_cfg();
            show_hud((uint8_t)k, "DRUM KIT", KIT_NAMES[set.drum_kit]);
            return;
        }
        if (k == EN_ALGO) {
            set.drum_bus = (uint8_t)(((int32_t)set.drum_bus + d + (int32_t)SF_NBUS) % (int32_t)SF_NBUS);
            set_dirty = 1u; sync_engine_cfg();
            show_hud((uint8_t)k, "DRUM BUS", BUS_NAMES[set.drum_bus]);
            return;
        }
    }

    if (k == EN_SELECT) { step_bank(d); return; }
    if (k == EN_PRESET) { step_prog(d); return; }
    if (k == EN_ALGO) {
        uint8_t cur = sf_osc_mode(p), idx = 0u, i;
        for (i = 0; i < SF_NOSC; i++) if (OSC_ORDER[i] == cur) idx = i;
        idx = (uint8_t)(((int32_t)idx + d + (int32_t)SF_NOSC) % (int32_t)SF_NOSC);
        uint8_t nm = OSC_ORDER[idx];
        p->osc_meta = (uint8_t)((p->osc_meta & ~7u) | (nm & 7u));
        set_dirty = 1u; sync_engine_cfg();
        show_hud((uint8_t)k, "OSC ENGINE", OSC_NAMES[nm]);
        return;
    }

    uint32_t kn = k - EN_K1; /* 0..3 */
    uint8_t sub = ui.sub_page;
    set_dirty = 1u;

    if (ui.page == PAGE_HOME) {
        if (kn == 0u) { p->f1_cut = (uint8_t)clamp((int32_t)p->f1_cut + d * 3, 0, 127); int_text(p->f1_cut, vbuf); show_hud((uint8_t)k, "F1 CUTOFF", vbuf); }
        else if (kn == 1u) { p->f1_res = (uint8_t)clamp((int32_t)p->f1_res + d * 3, 0, 127); int_text(p->f1_res, vbuf); show_hud((uint8_t)k, "RESONANCE", vbuf); }
        else if (kn == 2u) { p->f2_cut = (uint8_t)clamp((int32_t)p->f2_cut + d * 3, 0, 127); int_text(p->f2_cut, vbuf); show_hud((uint8_t)k, "F2 CUTOFF", vbuf); }
        else { p->f_bal = (uint8_t)clamp((int32_t)p->f_bal + d * 3, 0, 100); int_text(p->f_bal, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "FLT BAL", vbuf); }
    } else if (ui.page == PAGE_FX) {
        if (sub == 0u) {
            if (kn == 0u) { p->fx_chorus = (uint8_t)clamp((int32_t)p->fx_chorus + d * 3, 0, 100); int_text(p->fx_chorus, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "CHORUS", vbuf); }
            else if (kn == 1u) { p->fx_phaser = (uint8_t)clamp((int32_t)p->fx_phaser + d * 3, 0, 100); int_text(p->fx_phaser, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "PHASER", vbuf); }
            else if (kn == 2u) { p->fx_del_time = (uint8_t)clamp((int32_t)p->fx_del_time + d * 2, 5, 100); int_text(p->fx_del_time * 10, vbuf); text_cat(vbuf, "ms"); show_hud((uint8_t)k, "DLY TIME", vbuf); }
            else { p->fx_reverb = (uint8_t)clamp((int32_t)p->fx_reverb + d * 3, 0, 100); int_text(p->fx_reverb, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "REVERB", vbuf); }
        } else {
            if (kn == 0u) { p->fx_del_mix = (uint8_t)clamp((int32_t)p->fx_del_mix + d * 3, 0, 100); int_text(p->fx_del_mix, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DLY MIX", vbuf); }
            else if (kn == 1u) { p->fx_del_fb = (uint8_t)clamp((int32_t)p->fx_del_fb + d * 3, 0, 95); int_text(p->fx_del_fb, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DLY FBK", vbuf); }
            else if (kn == 2u) { p->fx_phaser_rate = (uint8_t)clamp((int32_t)p->fx_phaser_rate + d * 2, 1, 60); int_text(p->fx_phaser_rate, vbuf); show_hud((uint8_t)k, "PHAS RATE", vbuf); }
            else { v = ((int32_t)sf_sat_mode(p) + d + 4) % 4; p->f2_type_route = (uint8_t)((p->f2_type_route & 0x3Fu) | ((v & 3) << 6)); show_hud((uint8_t)k, "SAT MODE", SAT_NAMES[v]); }
        }
    } else if (ui.page == PAGE_SEL) {
        if (sub == 0u) {
            if (kn == 0u) {
                uint8_t c = (uint8_t)(((int32_t)(sf_f1_type(p) * 3u + sf_f_route(p)) + d + 12) % 12);
                uint8_t ft = c / 3u, rt = c % 3u;
                p->f2_type_route = (uint8_t)((p->f2_type_route & 0xCCu) | (ft & 3u) | ((rt & 3u) << 4));
                text_cat(vbuf, FLT_NAMES[ft]); text_cat(vbuf, " "); text_cat(vbuf, ROUTE_NAMES[rt]);
                show_hud((uint8_t)k, "FLT COMBO", vbuf);
            } else if (kn == 1u) {
                v = ((int32_t)sf_f1_type(p) + d + 4) % 4; p->f2_type_route = (uint8_t)((p->f2_type_route & ~3u) | (v & 3));
                show_hud((uint8_t)k, "F1 TYPE", FLT_NAMES[v]);
            } else if (kn == 2u) {
                v = ((int32_t)sf_f2_type(p) + d + 4) % 4; p->f2_type_route = (uint8_t)((p->f2_type_route & ~0x0Cu) | ((v & 3) << 2));
                show_hud((uint8_t)k, "F2 TYPE", FLT_NAMES[v]);
            } else {
                v = ((int32_t)sf_f_route(p) + d + 3) % 3; p->f2_type_route = (uint8_t)((p->f2_type_route & ~0x30u) | ((v & 3) << 4));
                show_hud((uint8_t)k, "FLT ROUTE", ROUTE_NAMES[v]);
            }
        } else if (sub == 1u) {
            if (kn == 0u) { p->f1_cut = (uint8_t)clamp((int32_t)p->f1_cut + d * 3, 0, 127); int_text(p->f1_cut, vbuf); show_hud((uint8_t)k, "F1 CUT", vbuf); }
            else if (kn == 1u) { p->f1_res = (uint8_t)clamp((int32_t)p->f1_res + d * 3, 0, 127); int_text(p->f1_res, vbuf); show_hud((uint8_t)k, "F1 RES", vbuf); }
            else if (kn == 2u) { p->f2_cut = (uint8_t)clamp((int32_t)p->f2_cut + d * 3, 0, 127); int_text(p->f2_cut, vbuf); show_hud((uint8_t)k, "F2 CUT", vbuf); }
            else { p->f2_res = (uint8_t)clamp((int32_t)p->f2_res + d * 3, 0, 127); int_text(p->f2_res, vbuf); show_hud((uint8_t)k, "F2 RES", vbuf); }
        } else {
            if (kn == 0u) { p->f_bal = (uint8_t)clamp((int32_t)p->f_bal + d * 3, 0, 100); int_text(p->f_bal, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "FLT BAL", vbuf); }
            else if (kn == 1u) { p->f1_env_s = (int8_t)clamp((int32_t)p->f1_env_s + d * 4, -100, 100); int_text(p->f1_env_s, vbuf); show_hud((uint8_t)k, "ENV AMT", vbuf); }
            else if (kn == 2u) { p->f_rel = (uint8_t)clamp((int32_t)p->f_rel + d * 3, 0, 127); int_text(p->f_rel, vbuf); show_hud((uint8_t)k, "FLT REL", vbuf); }
            else { p->f_keyfollow = (uint8_t)clamp((int32_t)p->f_keyfollow + d * 3, 0, 100); int_text(p->f_keyfollow, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "KEYFLW", vbuf); }
        }
    } else if (ui.page == PAGE_ENV) {
        if (sub == 0u) {
            if (kn == 0u) { p->a_atk = (uint8_t)clamp((int32_t)p->a_atk + d * 3, 0, 127); int_text(p->a_atk, vbuf); show_hud((uint8_t)k, "AMP ATK", vbuf); }
            else if (kn == 1u) { p->a_dec = (uint8_t)clamp((int32_t)p->a_dec + d * 3, 0, 127); int_text(p->a_dec, vbuf); show_hud((uint8_t)k, "AMP DEC", vbuf); }
            else if (kn == 2u) { p->a_sus = (uint8_t)clamp((int32_t)p->a_sus + d * 3, 0, 127); int_text(p->a_sus, vbuf); show_hud((uint8_t)k, "AMP SUS", vbuf); }
            else { p->a_rel = (uint8_t)clamp((int32_t)p->a_rel + d * 3, 0, 127); int_text(p->a_rel, vbuf); show_hud((uint8_t)k, "AMP REL", vbuf); }
        } else {
            if (kn == 0u) { p->f_atk = (uint8_t)clamp((int32_t)p->f_atk + d * 3, 0, 127); int_text(p->f_atk, vbuf); show_hud((uint8_t)k, "FLT ATK", vbuf); }
            else if (kn == 1u) { p->f_dec = (uint8_t)clamp((int32_t)p->f_dec + d * 3, 0, 127); int_text(p->f_dec, vbuf); show_hud((uint8_t)k, "FLT DEC", vbuf); }
            else if (kn == 2u) { p->f_sus = (uint8_t)clamp((int32_t)p->f_sus + d * 3, 0, 127); int_text(p->f_sus, vbuf); show_hud((uint8_t)k, "FLT SUS", vbuf); }
            else { p->f1_env_s = (int8_t)clamp((int32_t)p->f1_env_s + d * 4, -100, 100); int_text(p->f1_env_s, vbuf); show_hud((uint8_t)k, "FLT ENV", vbuf); }
        }
    } else if (ui.page == PAGE_LFO) {
        if (sub == 0u) {
            if (kn == 0u) { p->lfo1_rate = (uint8_t)clamp((int32_t)p->lfo1_rate + d * 3, 0, 127); int_text(p->lfo1_rate, vbuf); show_hud((uint8_t)k, "LFO1 RATE", vbuf); }
            else if (kn == 1u) { p->lfo1_depth = (uint8_t)clamp((int32_t)p->lfo1_depth + d * 3, 0, 100); int_text(p->lfo1_depth, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "LFO1 DPTH", vbuf); }
            else if (kn == 2u) { v = ((int32_t)sf_lfo1_dest(p) + d + (int32_t)SF_NLFO_DEST) % (int32_t)SF_NLFO_DEST; p->lfo1_meta = (uint8_t)((p->lfo1_meta & ~15u) | (v & 15)); show_hud((uint8_t)k, "LFO1 DST", LFO_NAMES[v]); }
            else { v = ((int32_t)sf_lfo1_shape(p) + d + 4) % 4; p->lfo1_meta = (uint8_t)((p->lfo1_meta & ~0x30u) | ((v & 3) << 4)); show_hud((uint8_t)k, "LFO1 WAVE", LFO_SHAPES[v]); }
        } else if (sub == 1u) {
            if (kn == 0u) { p->lfo2_rate = (uint8_t)clamp((int32_t)p->lfo2_rate + d * 3, 0, 127); int_text(p->lfo2_rate, vbuf); show_hud((uint8_t)k, "LFO2 RATE", vbuf); }
            else if (kn == 1u) { p->lfo2_depth = (uint8_t)clamp((int32_t)p->lfo2_depth + d * 3, 0, 100); int_text(p->lfo2_depth, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "LFO2 DPTH", vbuf); }
            else if (kn == 2u) { v = ((int32_t)sf_lfo2_dest(p) + d + (int32_t)SF_NLFO_DEST) % (int32_t)SF_NLFO_DEST; p->lfo2_meta = (uint8_t)((p->lfo2_meta & ~15u) | (v & 15)); show_hud((uint8_t)k, "LFO2 DST", LFO_NAMES[v]); }
            else { v = ((int32_t)sf_lfo2_shape(p) + d + 4) % 4; p->lfo2_meta = (uint8_t)((p->lfo2_meta & ~0x30u) | ((v & 3) << 4)); show_hud((uint8_t)k, "LFO2 WAVE", LFO_SHAPES[v]); }
        } else {
            if (kn == 0u) { p->swarm_depth = (uint8_t)clamp((int32_t)p->swarm_depth + d * 3, 0, 100); int_text(p->swarm_depth, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "SWARM DPT", vbuf); }
            else if (kn == 1u) { p->swarm_rate = (uint8_t)clamp((int32_t)p->swarm_rate + d * 2, 1, 80); int_text(p->swarm_rate, vbuf); show_hud((uint8_t)k, "SWARM RTE", vbuf); }
            else if (kn == 2u) { p->glide = (uint8_t)clamp((int32_t)p->glide + d * 3, 0, 85); int_text(p->glide, vbuf); show_hud((uint8_t)k, "GLIDE", vbuf); }
            else { p->f_keyfollow = (uint8_t)clamp((int32_t)p->f_keyfollow + d * 3, 0, 100); int_text(p->f_keyfollow, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "KEYFLW", vbuf); }
        }
    } else if (ui.page == PAGE_EDIT) {
        if (sub == 0u) {
            if (kn == 0u) { p->osc_bal = (uint8_t)clamp((int32_t)p->osc_bal + d * 3, 0, 100); int_text(p->osc_bal, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "OSC BAL", vbuf); }
            else if (kn == 1u) { p->osc2_semi = (int8_t)clamp((int32_t)p->osc2_semi + d, -24, 24); int_text(p->osc2_semi, vbuf); text_cat(vbuf, "st"); show_hud((uint8_t)k, "OSC2 SEMI", vbuf); }
            else if (kn == 2u) { p->pw = (uint8_t)clamp((int32_t)p->pw + d * 3, 0, 127); int_text(p->pw, vbuf); show_hud((uint8_t)k, sf_osc_mode(p) == SF_OSC_WT ? "WT POS" : "PW/SHAPE", vbuf); }
            else { v = clamp((int32_t)sf_unison(p) + d, 1, 5); p->unison_poly = (uint8_t)((p->unison_poly & ~7u) | ((v - 1) & 7)); int_text(v, vbuf); text_cat(vbuf, "V"); show_hud((uint8_t)k, "UNISON", vbuf); }
        } else if (sub == 1u) {
            if (kn == 0u) { p->sub_vol = (uint8_t)clamp((int32_t)p->sub_vol + d * 3, 0, 100); int_text(p->sub_vol, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "SUB VOL", vbuf); }
            else if (kn == 1u) { v = ((int32_t)sf_sub_wave(p) + d + 3) % 3; p->osc_meta = (uint8_t)((p->osc_meta & ~0x30u) | ((v & 3) << 4)); show_hud((uint8_t)k, "SUB WAVE", SUB_NAMES[v]); }
            else if (kn == 2u) { p->noise_vol = (uint8_t)clamp((int32_t)p->noise_vol + d * 3, 0, 100); int_text(p->noise_vol, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "NOISE VOL", vbuf); }
            else { p->ring_vol = (uint8_t)clamp((int32_t)p->ring_vol + d * 3, 0, 100); int_text(p->ring_vol, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "RING MOD", vbuf); }
        } else {
            if (kn == 0u) { v = sf_osc_sync(p) ^ ((d & 1) ? 1u : 0u); p->osc_meta = (uint8_t)((p->osc_meta & ~8u) | ((v & 1) << 3)); show_hud((uint8_t)k, "OSC SYNC", v ? "ON" : "OFF"); }
            else if (kn == 1u) { p->fm_amt = (uint8_t)clamp((int32_t)p->fm_amt + d * 3, 0, 100); int_text(p->fm_amt, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "FM AMT", vbuf); }
            else if (kn == 2u) { p->uni_pan = (uint8_t)clamp((int32_t)p->uni_pan + d * 3, 0, 100); int_text(p->uni_pan, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "UNI PAN", vbuf); }
            else { p->fx_drive = (uint8_t)clamp((int32_t)p->fx_drive + d * 3, 0, 100); int_text(p->fx_drive, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DRIVE", vbuf); }
        }
    } else if (ui.page == PAGE_ARP) {
        if (sub == 0u) {
            if (kn == 0u) { if (d) arp_active = (uint8_t)!arp_active; set_arp_flag_from_state(); show_hud((uint8_t)k, "ARP", arp_active ? "ON" : "OFF"); }
            else if (kn == 1u) { if (d) set.arp_latch = (uint8_t)!set.arp_latch; set_arp_flag_from_state(); show_hud((uint8_t)k, "ARP LATCH", set.arp_latch ? "ON" : "OFF"); }
            else if (kn == 2u) { set.arp_mode = (uint8_t)(((int32_t)set.arp_mode + d + (int32_t)SF_NARP_MODE) % (int32_t)SF_NARP_MODE); set_arp_flag_from_state(); show_hud((uint8_t)k, "ARP MODE", ARP_MODE_NAMES[set.arp_mode]); }
            else { set.arp_rate = (uint8_t)(((int32_t)set.arp_rate + d + 4) % 4); set_arp_flag_from_state(); show_hud((uint8_t)k, "ARP RATE", ARP_RATE_NAMES[set.arp_rate]); }
        } else {
            if (kn == 0u) { set.arp_octs = (uint8_t)clamp((int32_t)set.arp_octs + d, 1, 3); set_arp_flag_from_state(); int_text(set.arp_octs, vbuf); show_hud((uint8_t)k, "ARP OCTS", vbuf); }
            else if (kn == 1u) { set.arp_gate = (uint8_t)clamp((int32_t)set.arp_gate + d * 3, 20, 95); int_text(set.arp_gate, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "ARP GATE", vbuf); }
            else if (kn == 2u) { set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75); int_text(set.swing, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "SWING", vbuf); }
            else { set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240); int_text(set.bpm, vbuf); show_hud((uint8_t)k, "TEMPO BPM", vbuf); }
        }
    } else if (ui.page == PAGE_SEQ) {
        uint8_t dv = ui.seq_voice % SF_DRUM_VOICES;
        if (sub == 0u) {
            if (kn == 0u) { set.drum_tune[dv] = (int8_t)clamp((int32_t)set.drum_tune[dv] + d, -12, 12); int_text(set.drum_tune[dv], vbuf); text_cat(vbuf, "st"); show_hud((uint8_t)k, DRUM_NAMES[dv], vbuf); }
            else if (kn == 1u) { set.drum_decay[dv] = (uint8_t)clamp((int32_t)set.drum_decay[dv] + d * 5, 25, 200); int_text(set.drum_decay[dv], vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DRUM DEC", vbuf); }
            else if (kn == 2u) { set.drum_tone[dv] = (uint8_t)clamp((int32_t)set.drum_tone[dv] + d * 4, 0, 100); int_text(set.drum_tone[dv], vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DRUM TONE", vbuf); }
            else { set.drum_level[dv] = (uint8_t)clamp((int32_t)set.drum_level[dv] + d * 4, 0, 100); int_text(set.drum_level[dv], vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DRUM LVL", vbuf); }
        } else if (sub == 1u) {
            if (kn == 0u) { uint8_t ep = (uint8_t)clamp((int32_t)set.euclid_pulses[dv] + d, 0, (int32_t)set.seq_length); apply_euclidean(dv, ep, set.euclid_rot[dv]); int_text(ep, vbuf); show_hud((uint8_t)k, "EUCLID HIT", vbuf); }
            else if (kn == 1u) { uint8_t er = (uint8_t)(((int32_t)set.euclid_rot[dv] + d + (int32_t)set.seq_length) % (int32_t)set.seq_length); apply_euclidean(dv, set.euclid_pulses[dv], er); int_text(er, vbuf); show_hud((uint8_t)k, "EUCLID ROT", vbuf); }
            else if (kn == 2u) { set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75); int_text(set.swing, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "SWING", vbuf); }
            else { set.seq_length = (uint8_t)clamp((int32_t)set.seq_length + d * 4, 4, 64); int_text(set.seq_length, vbuf); show_hud((uint8_t)k, "SEQ LEN", vbuf); }
        } else {
            if (kn == 0u) { set.drum_vol = (uint8_t)clamp((int32_t)set.drum_vol + d * 3, 0, 100); int_text(set.drum_vol, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "DRUM VOL", vbuf); }
            else if (kn == 1u) { set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240); int_text(set.bpm, vbuf); show_hud((uint8_t)k, "BPM", vbuf); }
            else if (kn == 2u) { uint8_t nb = (uint8_t)(((int32_t)ui.seq_bar + d + 4) % 4); ui.seq_bar = nb; int_text(nb + 1, vbuf); show_hud((uint8_t)k, "VIEW BAR", vbuf); }
            else { uint8_t dp = (uint8_t)(((int32_t)set.drum_preset + d + 5) % 5); apply_drum_preset(dp); show_hud((uint8_t)k, "PATTERN", DRUM_PRESET_NAMES[dp]); }
        }
    } else if (ui.page == PAGE_GLO) {
        if (sub == 0u) {
            if (kn == 0u) { set.root = (uint8_t)(((int32_t)set.root + d + 12) % 12); show_hud((uint8_t)k, "ROOT KEY", NOTE12[set.root]); }
            else if (kn == 1u) { set.scale = (uint8_t)(((int32_t)set.scale + d + (int32_t)SF_NSCALE) % (int32_t)SF_NSCALE); show_hud((uint8_t)k, "SCALE", SCALE_NAMES[set.scale]); }
            else if (kn == 2u) { set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240); int_text(set.bpm, vbuf); show_hud((uint8_t)k, "BPM", vbuf); }
            else { set.chord_mode = (uint8_t)(((int32_t)set.chord_mode + d + (int32_t)SF_NCHORD) % (int32_t)SF_NCHORD); show_hud((uint8_t)k, "CHORD MEM", CHORD_NAMES[set.chord_mode]); }
        } else if (sub == 1u) {
            if (kn == 0u) { set.seq_length = (uint8_t)clamp((int32_t)set.seq_length + d * 4, 4, 64); int_text(set.seq_length, vbuf); show_hud((uint8_t)k, "SEQ STEPS", vbuf); }
            else if (kn == 1u) { v = clamp((int32_t)sf_polyphony(p) + d, 1, 8); p->unison_poly = (uint8_t)((p->unison_poly & 7u) | (((v - 1) & 7) << 3)); int_text(v, vbuf); text_cat(vbuf, "V"); show_hud((uint8_t)k, "POLYPHONY", vbuf); }
            else if (kn == 2u) { set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75); int_text(set.swing, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "SWING", vbuf); }
            else { if (d > 0) { ui.page = PAGE_BLE; ui.redraw = 1u; return; } else if (d < 0) { ble_toggle(); return; } }
        } else {
            if (kn == 0u) { set.rand_style = (uint8_t)(((int32_t)set.rand_style + d + (int32_t)SF_NRAND_STYLE) % (int32_t)SF_NRAND_STYLE); show_hud((uint8_t)k, "RND STYLE", RAND_NAMES[set.rand_style]); }
            else if (kn == 1u) { if (d != 0) { randomize_patch(); ui.page = PAGE_RAND_ASK; app_say("RANDOMIZED", set.patch.name); return; } }
            else if (kn == 2u) { set.rand_chaos = (uint8_t)clamp((int32_t)set.rand_chaos + d * 5, 20, 100); int_text(set.rand_chaos, vbuf); text_cat(vbuf, "%"); show_hud((uint8_t)k, "RND CHAOS", vbuf); }
            else { set.palette = (uint8_t)(((int32_t)set.palette + d + (int32_t)NPALETTES) % (int32_t)NPALETTES); palette_set(set.palette); show_hud((uint8_t)k, "OLED THEME", PALETTES[set.palette].name); }
        }
    }
    sync_engine_cfg();
}

/* ----------------------------------------------------------- Key & Buttons --- */
static void key_note_action(uint32_t k, uint8_t on, uint8_t vel)
{
    uint32_t i, nc;
    int8_t iv[4];
    int32_t raw_midi = 53 + (int32_t)k + 12 * (int32_t)set.octave;
    uint8_t root_note = quantize_note(raw_midi);

    if (on) {
        ui.vel[k] = vel ? vel : 100u;
        ui.last_vel = ui.vel[k];
        ui.last_key = (int8_t)k;
        if (arp_active) {
            arp_held[k] = 1u;
            if (set.arp_latch) {
                uint8_t any = 0u;
                for (i = 0; i < NKEYS; i++) if (arp_held[i] && i != k) any = 1u;
                if (!any) mem_zero((void *)arp_latch_buf, NKEYS);
                arp_latch_buf[k] = 1u;
            }
            return;
        }
        nc = chord_intervals(iv);
        for (i = 0; i < nc; i++) {
            uint8_t mn = quantize_note((int32_t)root_note + (int32_t)iv[i]);
            sf_dsp_note_on(mn, ui.vel[k]);
            send_midi_all(0x90u, mn, ui.vel[k]);
        }
        if (rec_armed && synth_loop_cnt < SYNTH_LOOP_MAX) {
            synth_loop[synth_loop_cnt].step = seq_step % (set.seq_length ? set.seq_length : 16u);
            synth_loop[synth_loop_cnt].note = root_note;
            synth_loop[synth_loop_cnt].vel = ui.vel[k];
            synth_loop[synth_loop_cnt].len_steps = 2u;
            synth_loop_cnt++;
        }
    } else {
        arp_held[k] = 0u;
        if (arp_active) return;
        nc = chord_intervals(iv);
        for (i = 0; i < nc; i++) {
            uint8_t mn = quantize_note((int32_t)root_note + (int32_t)iv[i]);
            sf_dsp_note_off(mn);
            send_midi_all(0x80u, mn, 0u);
        }
    }
}

static void toggle_page_cycle(uint8_t pg)
{
    if (ui.page != pg) {
        ui.page = pg;
        ui.sub_page = 0u;
    } else if (ui.sub_page + 1u < PAGE_SUBS[pg]) {
        ui.sub_page++;
    } else {
        ui.page = PAGE_HOME;
        ui.sub_page = 0u;
    }
    ui.redraw = 1u;
}

static void execute_save_action(void)
{
    if (ui.save_choice == 0u) {
        save_to_bankx_slot(target_bankx_slot);
        ui.save_flash_until = plat_ms() + 600u;
        ui.page = PAGE_HOME;
        ui.sub_page = 0u;
        app_say("SAVED BANK X", set.patch.name);
    } else if (ui.save_choice == 1u) {
        make_init_patch(&set.patch, "INIT STAR");
        set_dirty = 1u;
        sync_engine_cfg();
        ui.page = PAGE_HOME;
        app_say("INIT PATCH", "LOADED");
    } else {
        randomize_patch();
        ui.page = PAGE_RAND_ASK;
        app_say("RANDOMIZED", set.patch.name);
    }
}

static void input(uint32_t now)
{
    static const uint8_t WKEYS[16] = PLAT_WHITE_KEYS;
    static const uint8_t BKEYS[11] = PLAT_BLACK_KEYS;
    uint32_t b = plat_buttons();
    uint32_t bp = b & ~ui.btn;
    uint32_t br = ui.btn & ~b;
    uint32_t i;

    touch_poll();
    uint32_t ks = touch_keys();
    uint32_t kp = ks & ~ui.keys;
    uint32_t kr = ui.keys & ~ks;

    if ((bp || kp) && now < boot_until) {
        boot_until = now;
        sf_dsp_stop_jingle();
        ui.redraw = 1u;
    }

    for (i = 0; i < NE; i++) {
        int32_t d = plat_enc((int)i);
        if (d) {
            if (now < boot_until) { boot_until = now; sf_dsp_stop_jingle(); }
            ui.turns[i] += d;
            handle_knob(i, d);
        }
    }

    /* Long-press timers */
    if ((b & (1u << B_ARP)) && ui.arp_held_btn && (now - ui.arp_t >= 450u) && ui.page != PAGE_ARP) {
        ui.page = PAGE_ARP;
        ui.sub_page = 0u;
        ui.arp_combo = 1u;
        ui.redraw = 1u;
    }
    if ((b & (1u << B_SAVE)) && ui.save_held_btn && (now - ui.save_t >= 1500u) && ui.page != PAGE_RESET) {
        ui.page = PAGE_RESET;
        ui.save_held_btn = 0u;
        ui.redraw = 1u;
    }
    if ((b & (1u << B_REC)) && ui.rec_held_btn && (now - ui.rec_t >= 1200u)) {
        synth_loop_cnt = 0u;
        rec_armed = 0u;
        ui.rec_held_btn = 0u;
        app_say("SYNTH LOOP", "CLEARED");
    }

    /* Button presses */
    if (bp & (1u << B_HOME)) {
        ui.page = PAGE_HOME; ui.sub_page = 0u;
        if (unsaved_random) commit_pending_nav();
        ui.redraw = 1u;
    }
    if (bp & (1u << B_FX)) toggle_page_cycle(PAGE_FX);
    if (bp & (1u << B_SEL)) {
        if (ui.page == PAGE_SAVE) { ui.save_choice = (uint8_t)((ui.save_choice + 1u) % 3u); ui.redraw = 1u; }
        else if (ui.page == PAGE_RAND_ASK) { unsaved_random = 0u; ui.page = PAGE_HOME; app_say("KEEP EDITING", set.patch.name); }
        else toggle_page_cycle(PAGE_SEL);
    }
    if (bp & (1u << B_ENV)) toggle_page_cycle(PAGE_ENV);
    if (bp & (1u << B_LFO)) toggle_page_cycle(PAGE_LFO);
    if (bp & (1u << B_EDIT)) toggle_page_cycle(PAGE_EDIT);
    if (bp & (1u << B_GLO)) toggle_page_cycle(PAGE_GLO);
    if (bp & (1u << B_SEQ)) {
        if (ui.page != PAGE_SEQ) { ui.page = PAGE_SEQ; ui.sub_page = 0u; }
        else if (ui.sub_page + 1u < PAGE_SUBS[PAGE_SEQ]) ui.sub_page++;
        else { ui.page = PAGE_HOME; ui.sub_page = 0u; }
        ui.redraw = 1u;
    }
    if (bp & (1u << B_PLAY)) {
        if (seq_playing) {
            seq_playing = 0u;
            if (loop_active_note >= 0) { sf_dsp_note_off((uint8_t)loop_active_note); loop_active_note = -1; }
        } else {
            seq_step = 0u;
            tick_acc_us = 0u;
            seq_playing = 1u;
        }
        ui.redraw = 1u;
    }
    if (bp & (1u << B_REC)) {
        if (ui.page == PAGE_BLE) {
            int ev = btdev_save();
            app_say("BLE DEV", btdev_word(ev));
        } else {
            ui.rec_t = now;
            ui.rec_held_btn = 1u;
        }
    }
    if (br & (1u << B_REC)) {
        if (ui.rec_held_btn && ui.page != PAGE_BLE) {
            rec_armed = (uint8_t)!rec_armed;
            app_say("LOOP REC", rec_armed ? "ARMED" : "OFF");
        }
        ui.rec_held_btn = 0u;
    }

    if (bp & (1u << B_ARP)) { ui.arp_t = now; ui.arp_held_btn = 1u; ui.arp_combo = 0u; }
    if (br & (1u << B_ARP)) {
        if (ui.arp_held_btn && !ui.arp_combo) {
            arp_active = (uint8_t)!arp_active;
            set_arp_flag_from_state();
            if (!arp_active && arp_cur_note >= 0) {
                sf_dsp_note_off((uint8_t)arp_cur_note);
                arp_cur_note = -1;
            }
            app_say("ARP", arp_active ? "ON" : "OFF");
        }
        ui.arp_held_btn = 0u;
    }

    if (bp & (1u << B_SAVE)) {
        ui.save_t = now;
        ui.save_held_btn = 1u;
        if (ui.page == PAGE_SAVE) {
            execute_save_action();
            ui.save_held_btn = 0u;
        } else if (ui.page == PAGE_BLE) {
            int ev = btdev_save();
            app_say("BLE DEV", btdev_word(ev));
            ui.save_held_btn = 0u;
        }
    }
    if (br & (1u << B_SAVE)) {
        if (ui.save_held_btn && ui.page != PAGE_SAVE && ui.page != PAGE_RESET && ui.page != PAGE_BLE) {
            ui.page = PAGE_SAVE;
            ui.sub_page = 0u;
            ui.save_choice = 0u;
            ui.redraw = 1u;
        }
        ui.save_held_btn = 0u;
    }

    if (bp & (1u << B_OCTUP)) {
        if (b & (1u << B_ARP)) {
            set.arp_latch = (uint8_t)!set.arp_latch;
            set_arp_flag_from_state();
            ui.arp_combo = 1u;
            app_say("ARP LATCH", set.arp_latch ? "ON" : "OFF");
        } else if (ui.page == PAGE_SAVE) {
            execute_save_action();
        } else if (ui.page == PAGE_RAND_ASK) {
            save_to_bankx_slot(target_bankx_slot);
            ui.page = PAGE_HOME;
            app_say("SAVED BANK X", set.patch.name);
        } else if (ui.page == PAGE_RESET) {
            settings_defaults();
            bankx_defaults();
            plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
            ui.page = PAGE_HOME;
            app_say("FACTORY RESET", "DONE");
        } else if (set.octave < 2) {
            set.octave++;
            set_dirty = 1u;
            app_say("OCTAVE", set.octave > 0 ? "+1/+2" : "0");
        }
    }
    if (bp & (1u << B_OCTDN)) {
        if (b & (1u << B_ARP)) {
            set.arp_latch = 0u;
            mem_zero((void *)arp_latch_buf, NKEYS);
            set_arp_flag_from_state();
            ui.arp_combo = 1u;
            app_say("ARP LATCH", "CLR");
        } else if (ui.page == PAGE_SAVE || ui.page == PAGE_RESET) {
            ui.page = PAGE_HOME;
            app_say("CANCELLED", "");
        } else if (ui.page == PAGE_RAND_ASK) {
            commit_pending_nav();
            ui.page = PAGE_HOME;
        } else if (set.octave > -2) {
            set.octave--;
            set_dirty = 1u;
            app_say("OCTAVE", set.octave < 0 ? "-1/-2" : "0");
        }
    }

    /* Track velocity & pressure across all keys cleanly */
    for (i = 0; i < NKEYS; i++) {
        if (kp & (1u << i)) {
            uint8_t tv = touch_vel(i);
            ui.vel[i] = tv ? tv : 100u;
            ui.last_vel = ui.vel[i];
            ui.last_key = (int8_t)i;
        }
        if (ks & (1u << i)) {
            uint8_t pval = touch_pressure(i);
            if (pval != ui.prs[i]) {
                ui.prs[i] = pval;
                ui.last_prs = pval;
                if (ui.page != PAGE_SEQ) {
                    uint8_t mn = quantize_note(53 + (int32_t)i + 12 * (int32_t)set.octave);
                    sf_dsp_aftertouch(mn, pval);
                    send_midi_all(0xA0u, mn, pval);
                }
            }
        } else if (kr & (1u << i)) {
            ui.prs[i] = 0u;
        }
    }

    /* Keys handling: Drum grid in SEQ mode vs Synth performance */
    if (ui.page == PAGE_SEQ) {
        uint8_t dv = ui.seq_voice % SF_DRUM_VOICES;
        for (i = 0; i < 16u; i++) {
            uint32_t k = WKEYS[i];
            if (kp & (1u << k)) {
                uint32_t st = (uint32_t)ui.seq_bar * 16u + i;
                if (st < SF_SEQ_STEPS) {
                    uint8_t cur = set.seq_pat[dv][st];
                    set.seq_pat[dv][st] = cur ? 0u : (ui.vel[k] >= 100u ? 2u : 1u);
                    set_dirty = 1u;
                    sync_engine_cfg();
                    ui.redraw = 1u;
                }
            }
        }
        for (i = 0; i < 8u; i++) {
            uint32_t k = BKEYS[i];
            if (kp & (1u << k)) {
                ui.seq_voice = (uint8_t)i;
                sf_dsp_trigger_drum((uint8_t)i, ui.vel[k]);
                ui.redraw = 1u;
            }
        }
        if (kp & (1u << BKEYS[10])) { ui.b25_t = now; ui.b25_held = 1u; }
        if ((ks & (1u << BKEYS[10])) && ui.b25_held && (now - ui.b25_t >= 600u)) {
            mem_zero(set.seq_pat[dv], SF_SEQ_STEPS);
            set.euclid_pulses[dv] = 0u;
            sync_engine_cfg();
            ui.b25_held = 0u;
            app_say(DRUM_NAMES[dv], "CLEARED");
        }
        if (kr & (1u << BKEYS[10])) {
            if (ui.b25_held) {
                set.drum_mute ^= (uint8_t)(1u << dv);
                sync_engine_cfg();
                app_say(DRUM_NAMES[dv], (set.drum_mute & (1u << dv)) ? "MUTED" : "UNMUTED");
            }
            ui.b25_held = 0u;
        }
    } else {
        for (i = 0; i < NKEYS; i++) {
            if (kp & (1u << i)) key_note_action(i, 1u, ui.vel[i]);
            else if (kr & (1u << i)) key_note_action(i, 0u, 0u);
        }
    }

    ui.btn = b;
    ui.keys = ks;
}
/* ----------------------------------------------------------- OLED UI Draw --- */
static void format_bank_prog(char out[8])
{
    char nbuf[6];
    if (ui.page == PAGE_SEQ) {
        out[0] = 'K'; out[1] = 0;
        int_text((int32_t)(set.drum_kit + 1u), nbuf);
        text_cat(out, nbuf);
        return;
    }
    out[0] = BANK_CHAR[set.bank % 10u];
    out[1] = 0;
    int_text((int32_t)(set.prog + 1u), nbuf);
    text_cat(out, nbuf);
}

static void draw_mini_mascot(int32_t cx, int32_t cy, uint32_t now)
{
    uint32_t beat = (now * (set.bpm ? set.bpm : 120u)) / 15000u;
    if (ui.page == PAGE_SEQ) {
        /* Little Drummer Boy in SEQ mode: cap + eyes + alternating sticks + snare */
        int32_t arm = (beat & 1u) ? -2 : 2;
        cv_rect(cx - 5, cy - 8, 10, 3, C_AMB);      /* cap */
        cv_rect(cx - 5, cy - 6, 12, 2, C_HI);       /* brim */
        cv_rect(cx - 4, cy - 4, 8, 6, RGB(255, 210, 175));
        cv_pset(cx - 2, cy - 2, C_BLACK);
        cv_pset(cx + 2, cy - 2, C_BLACK);
        cv_line(cx - 8, cy + 2 + arm, cx - 2, cy + 6, C_WHITE);
        cv_line(cx + 8, cy + 2 - arm, cx + 2, cy + 6, C_WHITE);
        cv_rect(cx - 6, cy + 5, 12, 5, C_GRAY);     /* snare */
        cv_rect(cx - 6, cy + 5, 12, 1, C_WHITE);
    } else {
        int32_t bob = (beat & 1u) ? -1 : 0;
        uint16_t coral = RGB(255, 112, 112);
        cv_rect(cx - 5, cy - 5 + bob, 11, 10, coral);
        cv_rect(cx - 8, cy - 2 + bob, 17, 4, coral);
        cv_rect(cx - 2, cy - 9 + bob, 5, 16, coral);
        cv_pset(cx - 2, cy - 2 + bob, C_BLACK);
        cv_pset(cx + 2, cy - 2 + bob, C_BLACK);
        cv_line(cx - 2, cy + 1 + bob, cx + 2, cy + 1 + bob, C_BLACK);
    }
}

static void draw_header_and_tabs(uint32_t now)
{
    char bp[8], bpm_s[8], sub_s[12];
    format_bank_prog(bp);
    int32_t pw = text_w(&FONT_S, bp) + 8;

    cv_rect(0, 0, 240, 32, RGB(12, 14, 20));
    cv_rect(4, 5, pw, 22, ui.page == PAGE_SEQ ? C_AMB : C_HI);
    cv_text(8, 8, &FONT_S, bp, C_BLACK);

    const char *title = (ui.page == PAGE_SEQ) ? KIT_NAMES[set.drum_kit] : set.patch.name;
    cv_text(10 + pw, 6, &FONT_B, title, C_WHITE);

    if (set_dirty && ui.page != PAGE_SEQ) cv_text(6 + pw, 19, &FONT_T, "*", C_AMB);

    bpm_s[0] = 0;
    int_text((int32_t)set.bpm, bpm_s);
    text_cat(bpm_s, " BPM");
    cv_text(182 - text_w(&FONT_XS, bpm_s), 3, &FONT_XS, bpm_s, C_GRAY);

    const char *st_badge = rec_armed ? "REC" : seq_playing ? "PLAY" : arp_active ? "ARP" : unsaved_random ? "NEW!" : "VA";
    uint16_t st_col = rec_armed ? RGB(255, 70, 70) : seq_playing ? C_HI : arp_active ? C_AMB : C_DIM;
    cv_text(182 - text_w(&FONT_XS, st_badge), 17, &FONT_XS, st_badge, st_col);

    draw_mini_mascot(216, 16, now);
    cv_line(0, 32, 240, 32, C_LINE);

    /* Page title + sub-page micro dots (y = 34..50) */
    cv_text(8, 36, &FONT_S, PAGE_NAME[ui.page], C_HI);
    uint8_t nsub = PAGE_SUBS[ui.page], s;
    for (s = 0; s < nsub; s++) {
        cv_rect(84 + (int32_t)s * 10, 41, 6, 6, (s == ui.sub_page) ? C_WHITE : C_DIM);
    }
    sub_s[0] = 0;
    if (ui.page == PAGE_HOME) text_cat(sub_s, OSC_NAMES[sf_osc_mode(&set.patch)]);
    else if (ui.page == PAGE_SEQ) { text_cat(sub_s, "BAR "); char b1[4]; int_text(ui.seq_bar + 1, b1); text_cat(sub_s, b1); }
    else if (ui.page == PAGE_GLO) text_cat(sub_s, SCALE_NAMES[set.scale]);
    else if (ui.page == PAGE_ARP) text_cat(sub_s, ARP_MODE_NAMES[set.arp_mode]);
    cv_text(232 - text_w(&FONT_XS, sub_s), 38, &FONT_XS, sub_s, C_AMB);
    cv_line(0, 52, 240, 52, C_LINE);
}

static void draw_kcard(int32_t x, int32_t y, int32_t w, uint8_t kid, const char *lbl, const char *val, int32_t pct)
{
    uint8_t hot = (plat_ms() < ui.knob_until && ui.last_knob == kid) ? 1u : 0u;
    int32_t bw = (w - 8) * clamp(pct, 0, 100) / 100;
    cv_rect(x, y, w, 24, RGB(14, 16, 22));
    cv_rect(x, y, w, 1, hot ? C_WHITE : C_LINE);
    cv_rect(x, y + 23, w, 1, hot ? C_WHITE : C_LINE);
    cv_text(x + 4, y + 2, &FONT_T, lbl, hot ? C_WHITE : C_GRAY);
    cv_text(x + w - 4 - text_w(&FONT_XS, val), y + 2, &FONT_XS, val, hot ? C_HI : C_AMB);
    cv_rect(x + 4, y + 17, w - 8, 4, C_LINE);
    if (bw > 0) cv_rect(x + 4, y + 17, bw, 4, hot ? C_WHITE : C_HI);
}

static void knob_label_val(uint8_t kn, const char **lbl, char val[12], int32_t *pct)
{
    const sf_preset_t *p = &set.patch;
    uint8_t sub = ui.sub_page;
    *lbl = "";
    val[0] = 0;
    *pct = 50;

    if (ui.page == PAGE_HOME) {
        if (kn == 0u) { *lbl = "F1 CUT"; int_text(p->f1_cut, val); *pct = (p->f1_cut * 100) / 127; }
        else if (kn == 1u) { *lbl = "F1 RES"; int_text(p->f1_res, val); *pct = (p->f1_res * 100) / 127; }
        else if (kn == 2u) { *lbl = "F2 CUT"; int_text(p->f2_cut, val); *pct = (p->f2_cut * 100) / 127; }
        else { *lbl = "F BAL"; int_text(p->f_bal, val); text_cat(val, "%"); *pct = p->f_bal; }
    } else if (ui.page == PAGE_FX) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "CHORUS"; int_text(p->fx_chorus, val); *pct = p->fx_chorus; }
            else if (kn == 1u) { *lbl = "PHASER"; int_text(p->fx_phaser, val); *pct = p->fx_phaser; }
            else if (kn == 2u) { *lbl = "DLY TM"; int_text(p->fx_del_time * 10, val); *pct = p->fx_del_time; }
            else { *lbl = "REVERB"; int_text(p->fx_reverb, val); *pct = p->fx_reverb; }
        } else {
            if (kn == 0u) { *lbl = "DLY MIX"; int_text(p->fx_del_mix, val); *pct = p->fx_del_mix; }
            else if (kn == 1u) { *lbl = "DLY FBK"; int_text(p->fx_del_fb, val); *pct = p->fx_del_fb; }
            else if (kn == 2u) { *lbl = "PHS RT"; int_text(p->fx_phaser_rate, val); *pct = (p->fx_phaser_rate * 100) / 60; }
            else { *lbl = "SATUR"; text_cat(val, SAT_NAMES[sf_sat_mode(p)]); *pct = (sf_sat_mode(p) * 100) / 3; }
        }
    } else if (ui.page == PAGE_SEL) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "COMBO"; text_cat(val, FLT_NAMES[sf_f1_type(p)]); text_cat(val, "+"); text_cat(val, ROUTE_NAMES[sf_f_route(p)]); *pct = 65; }
            else if (kn == 1u) { *lbl = "F1 TYP"; text_cat(val, FLT_NAMES[sf_f1_type(p)]); *pct = (sf_f1_type(p) * 100) / 3; }
            else if (kn == 2u) { *lbl = "F2 TYP"; text_cat(val, FLT_NAMES[sf_f2_type(p)]); *pct = (sf_f2_type(p) * 100) / 3; }
            else { *lbl = "ROUTE"; text_cat(val, ROUTE_NAMES[sf_f_route(p)]); *pct = (sf_f_route(p) * 100) / 2; }
        } else if (sub == 1u) {
            if (kn == 0u) { *lbl = "F1 CUT"; int_text(p->f1_cut, val); *pct = (p->f1_cut * 100) / 127; }
            else if (kn == 1u) { *lbl = "F1 RES"; int_text(p->f1_res, val); *pct = (p->f1_res * 100) / 127; }
            else if (kn == 2u) { *lbl = "F2 CUT"; int_text(p->f2_cut, val); *pct = (p->f2_cut * 100) / 127; }
            else { *lbl = "F2 RES"; int_text(p->f2_res, val); *pct = (p->f2_res * 100) / 127; }
        } else {
            if (kn == 0u) { *lbl = "F BAL"; int_text(p->f_bal, val); *pct = p->f_bal; }
            else if (kn == 1u) { *lbl = "ENV AMT"; int_text(p->f1_env_s, val); *pct = (p->f1_env_s + 100) / 2; }
            else if (kn == 2u) { *lbl = "FLT REL"; int_text(p->f_rel, val); *pct = (p->f_rel * 100) / 127; }
            else { *lbl = "KEYFLW"; int_text(p->f_keyfollow, val); *pct = p->f_keyfollow; }
        }
    } else if (ui.page == PAGE_ENV) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "A ATK"; int_text(p->a_atk, val); *pct = (p->a_atk * 100) / 127; }
            else if (kn == 1u) { *lbl = "A DEC"; int_text(p->a_dec, val); *pct = (p->a_dec * 100) / 127; }
            else if (kn == 2u) { *lbl = "A SUS"; int_text(p->a_sus, val); *pct = (p->a_sus * 100) / 127; }
            else { *lbl = "A REL"; int_text(p->a_rel, val); *pct = (p->a_rel * 100) / 127; }
        } else {
            if (kn == 0u) { *lbl = "F ATK"; int_text(p->f_atk, val); *pct = (p->f_atk * 100) / 127; }
            else if (kn == 1u) { *lbl = "F DEC"; int_text(p->f_dec, val); *pct = (p->f_dec * 100) / 127; }
            else if (kn == 2u) { *lbl = "F SUS"; int_text(p->f_sus, val); *pct = (p->f_sus * 100) / 127; }
            else { *lbl = "F ENV"; int_text(p->f1_env_s, val); *pct = (p->f1_env_s + 100) / 2; }
        }
    } else if (ui.page == PAGE_LFO) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "L1 RTE"; int_text(p->lfo1_rate, val); *pct = (p->lfo1_rate * 100) / 127; }
            else if (kn == 1u) { *lbl = "L1 DPT"; int_text(p->lfo1_depth, val); *pct = p->lfo1_depth; }
            else if (kn == 2u) { *lbl = "L1 DST"; text_cat(val, LFO_NAMES[sf_lfo1_dest(p)]); *pct = (sf_lfo1_dest(p) * 100) / SF_NLFO_DEST; }
            else { *lbl = "L1 SHP"; text_cat(val, LFO_SHAPES[sf_lfo1_shape(p)]); *pct = (sf_lfo1_shape(p) * 100) / 3; }
        } else if (sub == 1u) {
            if (kn == 0u) { *lbl = "L2 RTE"; int_text(p->lfo2_rate, val); *pct = (p->lfo2_rate * 100) / 127; }
            else if (kn == 1u) { *lbl = "L2 DPT"; int_text(p->lfo2_depth, val); *pct = p->lfo2_depth; }
            else if (kn == 2u) { *lbl = "L2 DST"; text_cat(val, LFO_NAMES[sf_lfo2_dest(p)]); *pct = (sf_lfo2_dest(p) * 100) / SF_NLFO_DEST; }
            else { *lbl = "L2 SHP"; text_cat(val, LFO_SHAPES[sf_lfo2_shape(p)]); *pct = (sf_lfo2_shape(p) * 100) / 3; }
        } else {
            if (kn == 0u) { *lbl = "SWARM"; int_text(p->swarm_depth, val); *pct = p->swarm_depth; }
            else if (kn == 1u) { *lbl = "SW RTE"; int_text(p->swarm_rate, val); *pct = (p->swarm_rate * 100) / 80; }
            else if (kn == 2u) { *lbl = "GLIDE"; int_text(p->glide, val); *pct = (p->glide * 100) / 85; }
            else { *lbl = "KEYFLW"; int_text(p->f_keyfollow, val); *pct = p->f_keyfollow; }
        }
    } else if (ui.page == PAGE_EDIT) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "OSC BAL"; int_text(p->osc_bal, val); *pct = p->osc_bal; }
            else if (kn == 1u) { *lbl = "SEMI"; int_text(p->osc2_semi, val); *pct = ((p->osc2_semi + 24) * 100) / 48; }
            else if (kn == 2u) { *lbl = "PW/WT"; int_text(p->pw, val); *pct = (p->pw * 100) / 127; }
            else { *lbl = "UNISON"; int_text(sf_unison(p), val); text_cat(val, "V"); *pct = (sf_unison(p) * 100) / 5; }
        } else if (sub == 1u) {
            if (kn == 0u) { *lbl = "SUB VOL"; int_text(p->sub_vol, val); *pct = p->sub_vol; }
            else if (kn == 1u) { *lbl = "SUB WV"; text_cat(val, SUB_NAMES[sf_sub_wave(p)]); *pct = (sf_sub_wave(p) * 100) / 2; }
            else if (kn == 2u) { *lbl = "NOISE"; int_text(p->noise_vol, val); *pct = p->noise_vol; }
            else { *lbl = "RING"; int_text(p->ring_vol, val); *pct = p->ring_vol; }
        } else {
            if (kn == 0u) { *lbl = "SYNC"; text_cat(val, sf_osc_sync(p) ? "ON" : "OFF"); *pct = sf_osc_sync(p) ? 100 : 0; }
            else if (kn == 1u) { *lbl = "FM AMT"; int_text(p->fm_amt, val); *pct = p->fm_amt; }
            else if (kn == 2u) { *lbl = "PAN SP"; int_text(p->uni_pan, val); *pct = p->uni_pan; }
            else { *lbl = "DRIVE"; int_text(p->fx_drive, val); *pct = p->fx_drive; }
        }
    } else if (ui.page == PAGE_ARP) {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "ARP"; text_cat(val, arp_active ? "ON" : "OFF"); *pct = arp_active ? 100 : 0; }
            else if (kn == 1u) { *lbl = "LATCH"; text_cat(val, set.arp_latch ? "ON" : "OFF"); *pct = set.arp_latch ? 100 : 0; }
            else if (kn == 2u) { *lbl = "MODE"; text_cat(val, ARP_MODE_NAMES[set.arp_mode]); *pct = (set.arp_mode * 100) / 3; }
            else { *lbl = "RATE"; text_cat(val, ARP_RATE_NAMES[set.arp_rate]); *pct = (set.arp_rate * 100) / 3; }
        } else {
            if (kn == 0u) { *lbl = "OCTS"; int_text(set.arp_octs, val); *pct = (set.arp_octs * 100) / 3; }
            else if (kn == 1u) { *lbl = "GATE"; int_text(set.arp_gate, val); text_cat(val, "%"); *pct = set.arp_gate; }
            else if (kn == 2u) { *lbl = "SWING"; int_text(set.swing, val); text_cat(val, "%"); *pct = (set.swing - 50) * 4; }
            else { *lbl = "BPM"; int_text(set.bpm, val); *pct = ((set.bpm - 40) * 100) / 200; }
        }
    } else if (ui.page == PAGE_SEQ) {
        uint8_t dv = ui.seq_voice % SF_DRUM_VOICES;
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "TUNE"; int_text(set.drum_tune[dv], val); *pct = ((set.drum_tune[dv] + 12) * 100) / 24; }
            else if (kn == 1u) { *lbl = "DECAY"; int_text(set.drum_decay[dv], val); *pct = (set.drum_decay[dv] * 100) / 200; }
            else if (kn == 2u) { *lbl = "TONE"; int_text(set.drum_tone[dv], val); *pct = set.drum_tone[dv]; }
            else { *lbl = "LEVEL"; int_text(set.drum_level[dv], val); *pct = set.drum_level[dv]; }
        } else if (sub == 1u) {
            if (kn == 0u) { *lbl = "EUCL H"; int_text(set.euclid_pulses[dv], val); *pct = (set.euclid_pulses[dv] * 100) / (set.seq_length ? set.seq_length : 16); }
            else if (kn == 1u) { *lbl = "EUCL R"; int_text(set.euclid_rot[dv], val); *pct = (set.euclid_rot[dv] * 100) / (set.seq_length ? set.seq_length : 16); }
            else if (kn == 2u) { *lbl = "SWING"; int_text(set.swing, val); *pct = (set.swing - 50) * 4; }
            else { *lbl = "STEPS"; int_text(set.seq_length, val); *pct = (set.seq_length * 100) / 64; }
        } else {
            if (kn == 0u) { *lbl = "DRM LV"; int_text(set.drum_vol, val); *pct = set.drum_vol; }
            else if (kn == 1u) { *lbl = "BPM"; int_text(set.bpm, val); *pct = ((set.bpm - 40) * 100) / 200; }
            else if (kn == 2u) { *lbl = "BAR"; int_text(ui.seq_bar + 1, val); *pct = ((ui.seq_bar + 1) * 100) / 4; }
            else { *lbl = "PRESET"; text_cat(val, DRUM_PRESET_NAMES[set.drum_preset % 5u]); *pct = (set.drum_preset * 100) / 4; }
        }
    } else {
        if (sub == 0u) {
            if (kn == 0u) { *lbl = "ROOT"; text_cat(val, NOTE12[set.root % 12u]); *pct = (set.root * 100) / 11; }
            else if (kn == 1u) { *lbl = "SCALE"; text_cat(val, SCALE_NAMES[set.scale]); *pct = (set.scale * 100) / (SF_NSCALE - 1); }
            else if (kn == 2u) { *lbl = "BPM"; int_text(set.bpm, val); *pct = ((set.bpm - 40) * 100) / 200; }
            else { *lbl = "CHORD"; text_cat(val, CHORD_NAMES[set.chord_mode]); *pct = (set.chord_mode * 100) / (SF_NCHORD - 1); }
        } else if (sub == 1u) {
            if (kn == 0u) { *lbl = "LEN"; int_text(set.seq_length, val); *pct = (set.seq_length * 100) / 64; }
            else if (kn == 1u) { *lbl = "POLY"; int_text(sf_polyphony(p), val); text_cat(val, "V"); *pct = (sf_polyphony(p) * 100) / 8; }
            else if (kn == 2u) { *lbl = "SWING"; int_text(set.swing, val); *pct = (set.swing - 50) * 4; }
            else { *lbl = "BLE"; text_cat(val, set.ble_off ? "OFF" : "ON"); *pct = set.ble_off ? 0 : 100; }
        } else {
            if (kn == 0u) { *lbl = "STYLE"; text_cat(val, RAND_NAMES[set.rand_style]); *pct = (set.rand_style * 100) / (SF_NRAND_STYLE - 1); }
            else if (kn == 1u) { *lbl = "RANDOM"; text_cat(val, "TURN!"); *pct = 100; }
            else if (kn == 2u) { *lbl = "CHAOS"; int_text(set.rand_chaos, val); text_cat(val, "%"); *pct = set.rand_chaos; }
            else { *lbl = "THEME"; text_cat(val, PALETTES[set.palette].name); *pct = (set.palette * 100) / (NPALETTES - 1); }
        }
    }
}

static void draw_center_stage_top(void)
{
    const sf_preset_t *p = &set.patch;
    int32_t i;

    if (ui.page == PAGE_SAVE) {
        char sbuf[16];
        sbuf[0] = 0; text_cat(sbuf, "SLOT X"); int_text(target_bankx_slot + 1, sbuf + 6);
        cv_text(14, 60, &FONT_B, "SAVE / UTILITY", C_WHITE);
        cv_rect(12, 80, 216, 18, ui.save_choice == 0u ? C_HI : C_LINE);
        cv_text(18, 82, &FONT_S, "SAVE PATCH TO BANK X", ui.save_choice == 0u ? C_BLACK : C_WHITE);
        cv_text(168, 82, &FONT_XS, sbuf, ui.save_choice == 0u ? C_BLACK : C_AMB);
        cv_rect(12, 100, 216, 16, ui.save_choice == 1u ? C_HI : C_LINE);
        cv_text(18, 102, &FONT_XS, "LOAD INIT / CLEAN PATCH", ui.save_choice == 1u ? C_BLACK : C_GRAY);
    } else if (ui.page == PAGE_RAND_ASK) {
        cv_text(14, 60, &FONT_B, "UNSAVED RANDOM!", C_AMB);
        cv_text(14, 80, &FONT_S, p->name, C_WHITE);
        cv_text(14, 100, &FONT_XS, "OCT+ SAVE X  |  OCT- SKIP", C_HI);
    } else if (ui.page == PAGE_RESET) {
        cv_text(14, 64, &FONT_B, "FACTORY RESET?", RGB(255, 90, 90));
        cv_text(14, 86, &FONT_XS, "WIPES SETTINGS & BANK X", C_GRAY);
        cv_text(14, 102, &FONT_XS, "OCT+ CONFIRM | OCT- CANCEL", C_WHITE);
    } else if (ui.page == PAGE_BLE) {
        char t[34];
        btdev_title(t);
        cv_text(12, 58, &FONT_S, t, C_WHITE);
        int cnt = btdev_count();
        for (i = 0; i < 3 && i < cnt; i++) {
            char dnm[22]; int rssi = 0;
            if (btdev_found(i, dnm, &rssi)) {
                cv_rect(10, 76 + i * 14, 220, 13, (i == btdev_pick()) ? C_HI : C_LINE);
                cv_text(14, 77 + i * 14, &FONT_XS, dnm, (i == btdev_pick()) ? C_BLACK : C_WHITE);
            }
        }
        if (cnt == 0) cv_text(12, 86, &FONT_XS, "SCANNING FOR MIDI DEVICES...", C_GRAY);
    } else if (ui.page == PAGE_SEQ) {
        /* 8 Drum track tabs (y=58..82) + Euclidean/Bus strip (y=86..118) */
        for (i = 0; i < (int32_t)SF_DRUM_VOICES; i++) {
            int32_t tx = 6 + i * 29;
            uint8_t muted = (set.drum_mute >> i) & 1u;
            uint8_t sel = (ui.seq_voice == (uint8_t)i) ? 1u : 0u;
            cv_rect(tx, 58, 27, 22, sel ? C_HI : muted ? C_LINE : RGB(24, 28, 38));
            cv_text(tx + 5, 62, &FONT_XS, DRUM_NAMES[i], sel ? C_BLACK : muted ? C_DIM : C_WHITE);
            if (!muted) cv_rect(tx + 3, 76, 21, 2, sel ? C_BLACK : C_AMB);
        }
        char info[32], ib[8];
        info[0] = 0;
        text_cat(info, "BUS:"); text_cat(info, BUS_NAMES[set.drum_bus]);
        text_cat(info, " EUC:"); int_text(set.euclid_pulses[ui.seq_voice], ib); text_cat(info, ib);
        cv_text(8, 90, &FONT_XS, info, C_AMB);
        cv_text(150, 90, &FONT_XS, "BK10:MUTE/CLR", C_DIM);
        cv_text(8, 104, &FONT_T, "WHITE KEYS: STEPS  |  BLACK 1-8: VOICE", C_GRAY);
    } else if (ui.page == PAGE_ENV) {
        /* Dual ADSR preview inside Strip 0 (y=56..116) */
        int32_t x0 = 14, yb = 114, w = 212, h = 54;
        int32_t ax = x0 + (p->a_atk * w) / 512;
        int32_t dx = ax + 24 + (p->a_dec * w) / 512;
        int32_t sy = yb - (p->a_sus * h) / 127;
        int32_t rx = dx + 45;
        int32_t ex = rx + 20 + (p->a_rel * w) / 512;
        cv_line(x0, yb, ax, yb - h, C_HI);
        cv_line(ax, yb - h, dx, sy, C_HI);
        cv_line(dx, sy, rx, sy, C_HI);
        cv_line(rx, sy, ex > x0 + w ? x0 + w : ex, yb, C_HI);
        cv_text(12, 58, &FONT_T, "AMP + FLT ENVELOPE", C_GRAY);
    } else {
        /* HOME / FX / SEL / LFO / EDIT / ARP / GLO: Live Spectrum & Cutoff (y=56..118) */
        int32_t x1 = 10 + (p->f1_cut * 220) / 127;
        int32_t x2 = 10 + (p->f2_cut * 220) / 127;
        for (i = 0; i < 32; i++) {
            int32_t bh = (vis.bands[i] * 56) / 100;
            if (bh > 56) bh = 56;
            if (bh < 2) bh = 2;
            cv_rect(10 + i * 7, 116 - bh, 5, bh, (bh > 38) ? C_WHITE : C_AMB);
        }
        cv_line(x1, 56, x1, 118, C_HI);
        cv_line(x2, 58, x2, 118, C_GRAY);
    }
}

static void draw_center_stage_bot(void)
{
    if (ui.page == PAGE_SEQ) {
        /* 16-Step Matrix for current bar (y=122..154) */
        uint8_t dv = ui.seq_voice % SF_DRUM_VOICES;
        int32_t i;
        for (i = 0; i < 16; i++) {
            uint32_t st = (uint32_t)ui.seq_bar * 16u + (uint32_t)i;
            uint8_t hit = (st < SF_SEQ_STEPS) ? set.seq_pat[dv][st] : 0u;
            uint8_t ph = (seq_playing && seq_step == (uint8_t)st) ? 1u : 0u;
            int32_t sx = 6 + i * 14;
            uint16_t col = ph ? C_WHITE : (hit == 2u) ? C_AMB : (hit == 1u) ? C_HI : RGB(28, 32, 42);
            cv_rect(sx, 124, 12, 26, col);
            if ((i & 3) == 0) cv_rect(sx, 121, 12, 2, C_GRAY);
            if (hit == 2u && !ph) cv_rect(sx + 4, 127, 4, 4, C_WHITE);
        }
    } else if (ui.page == PAGE_SAVE) {
        cv_rect(12, 122, 216, 16, ui.save_choice == 2u ? C_HI : C_LINE);
        cv_text(18, 124, &FONT_XS, "GENERATE RANDOM PATCH", ui.save_choice == 2u ? C_BLACK : C_GRAY);
        cv_text(14, 143, &FONT_T, "SAVE / OCT+ CONFIRM  |  OCT- CANCEL", C_AMB);
    } else {
        /* Live Oscilloscope (y=122..154) */
        int32_t i, mid = 138;
        cv_line(8, mid, 232, mid, C_LINE);
        for (i = 0; i < 126; i++) {
            int32_t y0 = mid - ((int32_t)vis.scope[i] * 14) / 127;
            int32_t y1 = mid - ((int32_t)vis.scope[i + 1] * 14) / 127;
            int32_t x0 = 10 + (i * 220) / 127;
            int32_t x1 = 10 + ((i + 1) * 220) / 127;
            cv_line(x0, y0, x1, y1, C_HI);
        }
    }
}

static void draw_screen(uint32_t now)
{
    uint32_t kn;
    const char *lbl[4] = {"", "", "", ""};
    char val[4][12];
    int32_t pct[4] = {0, 0, 0, 0};

    sf_dsp_get_vis(&vis);
    for (kn = 0; kn < 4u; kn++) knob_label_val((uint8_t)kn, &lbl[kn], val[kn], &pct[kn]);

    /* Strip 0: y = 0..119 */
    cv_begin(240u, 120u, C_BLACK);
    draw_header_and_tabs(now);
    draw_center_stage_top();
    cv_blit(0u, 0u);

    /* Strip 1: y = 120..239 */
    cv_begin(240u, 120u, C_BLACK);
    cv_oy = -120;
    draw_center_stage_bot();

    /* K1..K4 2x2 Parameter Dock (y=160..212) */
    draw_kcard(6, 160, 112, EN_K1, lbl[0], val[0], pct[0]);
    draw_kcard(122, 160, 112, EN_K2, lbl[1], val[1], pct[1]);
    draw_kcard(6, 186, 112, EN_K3, lbl[2], val[2], pct[2]);
    draw_kcard(122, 186, 112, EN_K4, lbl[3], val[3], pct[3]);

    /* Footer Toast / Mini Keybed Strip (y=215..239) */
    cv_line(0, 214, 240, 214, C_LINE);
    if (now < ui.msg_until && ui.msg[0][0]) {
        int32_t x = cv_text(8, 218, &FONT_S, ui.msg[0], C_WHITE);
        cv_text(x + 6, 218, &FONT_S, ui.msg[1], C_HI);
    } else {
        char ft[28], ob[6];
        ft[0] = 0;
        text_cat(ft, "OCT:");
        if (set.octave > 0) text_cat(ft, "+");
        int_text(set.octave, ob); text_cat(ft, ob);
        if (ui.last_key >= 0) {
            char nn[8];
            note_name(quantize_note(53 + ui.last_key + 12 * set.octave), nn);
            text_cat(ft, " "); text_cat(ft, nn);
        }
        if (synth_loop_cnt > 0u) {
            char lb[6]; text_cat(ft, " LOOP:"); int_text(synth_loop_cnt, lb); text_cat(ft, lb);
        }
        cv_text(8, 219, &FONT_XS, ft, C_GRAY);

        /* Micro 16-key activity indicator on right */
        static const uint8_t WK[16] = PLAT_WHITE_KEYS;
        uint32_t k;
        for (k = 0; k < 16u; k++) {
            uint8_t held = (ui.keys >> WK[k]) & 1u;
            cv_rect(140 + (int32_t)k * 6, 219, 4, 14, held ? C_WHITE : C_DIM);
        }
    }
    cv_blit(0u, 120u);
    lcd_sync();
}

static void update_leds(uint32_t now)
{
    static const uint8_t WKEYS[16] = PLAT_WHITE_KEYS;
    static const uint8_t BKEYS[11] = PLAT_BLACK_KEYS;
    uint32_t btn = 0u, keys = 0u, i;

    if (ui.page == PAGE_HOME) btn |= (1u << B_HOME);
    else if (ui.page == PAGE_FX) btn |= (1u << B_FX);
    else if (ui.page == PAGE_SEL) btn |= (1u << B_SEL);
    else if (ui.page == PAGE_ENV) btn |= (1u << B_ENV);
    else if (ui.page == PAGE_LFO) btn |= (1u << B_LFO);
    else if (ui.page == PAGE_EDIT) btn |= (1u << B_EDIT);
    else if (ui.page == PAGE_GLO) btn |= (1u << B_GLO);
    else if (ui.page == PAGE_SEQ) btn |= (1u << B_SEQ);
    else if (ui.page == PAGE_SAVE) btn |= (1u << B_SAVE) | (1u << B_OCTUP) | (1u << B_OCTDN);
    else if (ui.page == PAGE_RAND_ASK || ui.page == PAGE_RESET) btn |= (1u << B_OCTUP) | (1u << B_OCTDN);

    if (arp_active) btn |= (1u << B_ARP);
    if (seq_playing) btn |= (1u << B_PLAY);
    if (rec_armed) btn |= (1u << B_REC);
    if (set_dirty && (now & 256u)) btn |= (1u << B_SAVE);

    if (ui.page == PAGE_SEQ) {
        uint8_t dv = ui.seq_voice % SF_DRUM_VOICES;
        for (i = 0; i < 16u; i++) {
            uint32_t st = (uint32_t)ui.seq_bar * 16u + i;
            uint8_t ph = (seq_playing && seq_step == (uint8_t)st) ? 1u : 0u;
            if ((st < SF_SEQ_STEPS && set.seq_pat[dv][st]) || ph) keys |= (1u << WKEYS[i]);
        }
        for (i = 0; i < 8u; i++) {
            if (ui.seq_voice == (uint8_t)i || !(set.drum_mute & (1u << i))) keys |= (1u << BKEYS[i]);
        }
    } else {
        keys = ui.keys;
    }

    plat_play_red(rec_armed ? 1 : 0);
    plat_glow(1);
    plat_leds(btn, keys);
}

void app_ui_init(void)
{
    touch_on(1);
    ui.page = PAGE_HOME;
    ui.last_key = -1;
    ui.redraw = 1u;
    draw_boot_splash(0u);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    btdev_page(ui.page == PAGE_BLE);
    while (btdev_event() != BTDEV_NONE) ui.redraw = 1u;

    input(now);
    update_leds(now);

    if (now < boot_until) {
        draw_boot_splash(now);
    } else {
        draw_screen(now);
    }
}

void app_input_only(void)
{
    input(plat_ms());
}

/* ----------------------------------------------------- Backup & Host Tests --- */
int app_backup_playing(void) { return seq_playing ? 1 : 0; }
void app_backup_flush(void)
{
    if (set_dirty) {
        plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set));
        set_dirty = 0u;
    }
}
void app_backup_hold(int on) { backup_hold = (uint8_t)(on ? 1u : 0u); }
void app_backup_restored(void) { load_all_from_store(); ui.redraw = 1u; }

#ifdef PLAT_HOST
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

int app_expect(const char *what, int *got)
{
    if (str_eq(what, "bank")) { *got = (int)set.bank; return 1; }
    if (str_eq(what, "prog")) { *got = (int)set.prog; return 1; }
    if (str_eq(what, "octave")) { *got = (int)set.octave; return 1; }
    if (str_eq(what, "dirty")) { *got = (int)set_dirty; return 1; }
    if (str_eq(what, "page")) { *got = (int)ui.page; return 1; }
    if (str_eq(what, "playing")) { *got = (int)seq_playing; return 1; }
    if (str_eq(what, "arp")) { *got = (int)arp_active; return 1; }
    if (str_eq(what, "note")) { *got = (int)sf_dsp_last_note(); return 1; }
    if (str_eq(what, "vel")) { *got = (int)ui.last_vel; return 1; }
    if (str_eq(what, "prs")) { *got = (int)ui.last_prs; return 1; }
    if (str_eq(what, "midi_out")) { *got = (int)plat_midi_count(PORT_USB, 1); return 1; }
    if (str_eq(what, "turns5")) { *got = (int)ui.turns[EN_K3]; return 1; }
    return 0;
}
#endif
