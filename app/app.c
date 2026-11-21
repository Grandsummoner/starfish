/* SPDX-License-Identifier: MPL-2.0 */
#include "fastmath.h"
#include "dsp.h"
#include "presets_rom.h"

#define SET_MAGIC      0x53545334u
#define BANKX_MAGIC    0x53545832u
#define SYNTH_LOOP_MAX 256u
#define BOOT_SPLASH_MS 3400u

enum {
    PAGE_HOME = 0, PAGE_FX, PAGE_SEL, PAGE_ENV, PAGE_LFO, PAGE_EDIT,
    PAGE_GLO, PAGE_ARP, PAGE_SEQ, PAGE_SAVE, PAGE_RAND_ASK, PAGE_BLE, PAGE_RESET
};

enum {
    HUD_NONE = 0, HUD_BANK, HUD_PRESET, HUD_DRUMKIT, HUD_PARAM, HUD_BADGE
};

enum {
    CONF_NONE = 0, CONF_SAVE, CONF_RESET, CONF_RAND_LEAVE, CONF_CLEAR_LOOP
};

/* Simulator OLED Colors (RGB565 matched to Fm1BasePanel.tsx) */
#define COL_BG       RGB(7, 9, 14)
#define COL_BG_HUD   RGB(11, 17, 28)
#define COL_TOPBAR   RGB(13, 19, 31)
#define COL_DOCK     RGB(13, 19, 32)
#define COL_CELL     RGB(20, 27, 41)
#define COL_BAR_BG   RGB(30, 41, 59)
#define COL_SLATE_D  RGB(51, 65, 85)
#define COL_DIM      RGB(71, 85, 105)
#define COL_MUTED    RGB(100, 116, 139)
#define COL_LABEL    RGB(148, 163, 184)
#define COL_LIGHT    RGB(203, 213, 225)
#define COL_TEXT_HI  RGB(248, 250, 252)
#define COL_CYAN     RGB(34, 211, 238)
#define COL_CYAN_DIM RGB(14, 72, 92)
#define COL_AMBER    RGB(251, 191, 36)
#define COL_AMB_DIM  RGB(84, 65, 18)
#define COL_GREEN    RGB(52, 211, 153)
#define COL_GRN_DIM  RGB(16, 74, 56)
#define COL_ORANGE   RGB(249, 115, 22)
#define COL_RED      RGB(239, 68, 68)
#define COL_RED_TXT  RGB(252, 165, 165)
#define COL_RED_DIM  RGB(69, 10, 10)
#define COL_PURPLE   RGB(167, 139, 250)
#define COL_HAIRLINE RGB(32, 41, 58)

typedef struct {
    uint32_t magic;
    uint8_t bank;                       /* 0..7=A..H, 8=I, 9=X */
    uint8_t prog;                       /* 0..127 */
    int8_t octave;                      /* -2..+2 */
    uint8_t palette;                    /* 0..4 */
    uint8_t ble_off;                    /* 0..1 */
    uint16_t bpm;                       /* 40..240 */
    uint8_t swing;                      /* 50..75 (%) */
    uint16_t seq_length;                /* 1..256 (synth up to 16 bars) */
    uint8_t drum_kit;                   /* 0..9 */
    uint8_t synth_vol;                  /* 0..100 */
    uint8_t drum_vol;                   /* 0..100 */
    uint8_t duck_amt;                   /* 0..100 */
    uint8_t duck_src;                   /* 0..7 drum voice index */
    uint8_t drum_bus;                   /* 0..5 */
    uint8_t drum_preset;                /* 0=TECHNO,1=HOUSE,2=ELECTRO,3=BREAK,4=CLEAR */
    uint8_t midi_synth_ch;              /* 0=OMNI, 1..16 */
    uint8_t midi_drum_ch;               /* 0=OFF, 1..16 */
    uint8_t bt_mode;                    /* 0=AUTO DUAL, 1=HOST SMK37, 2=DEVICE PC, 3=BT OFF */
    uint8_t audio_out_mode;             /* 0=AUTO/HP, 1=LINE -6dB, 2=HI +3dB */
    uint8_t scale;                      /* 0..11 */
    uint8_t root;                       /* 0..11 */
    uint8_t chord_mode;                 /* 0..4 */
    uint8_t arp_mode;                   /* 0..5 */
    uint8_t arp_rate;                   /* 0..9 */
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
    uint8_t rec_quant;                  /* 1=quantized 16th grid (default), 0=unquantized */
    uint8_t drum_loop_bars;             /* 0=1 BAR (16), 1=2 BAR (32), 2=3 BAR (48), 3=4 BAR (64), 4=SYNC */
    uint8_t recent_presets[6][2];       /* Last 6 presets {bank, prog} for boot jingle */
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
    uint16_t step;
    uint8_t note;
    uint8_t vel;
    uint16_t len_steps;
    uint32_t off_us;
    uint32_t dur_us;
    uint16_t holdoff_ms;
    uint32_t rec_start_ms;
    uint8_t quant;
    uint8_t active_src;                 /* 0=released, 1=panel key, 2=MIDI note */
    uint8_t active_key;
} sf_loop_ev_t;

static sf_settings_t set;
static sf_preset_t patch_baseline;
static uint8_t patch_compare_ab;
static sf_bankx_chunk_t bankx_chunks[3] PLAT_POOL;
static sf_engine_cfg_t eng;
static sf_dsp_vis_t vis;

static volatile uint8_t seq_playing;
static volatile uint16_t seq_step;
static volatile uint32_t tick_acc_us;
static volatile uint32_t loop_cycle_us;
static volatile uint32_t rec_tick_ms;
static volatile uint8_t count_in_active;
static volatile uint8_t count_in_beat;
static volatile uint32_t count_in_acc_us;
static volatile uint8_t arp_active;
static volatile uint8_t arp_held[NKEYS];
static volatile uint8_t arp_latch_buf[NKEYS];
static volatile uint32_t arp_midi_mask[4];
static volatile uint32_t arp_midi_latch_mask[4];
static volatile uint16_t arp_order_buf[24];
static volatile uint8_t arp_order_cnt;
static volatile int8_t arp_cur_note = -1;
static volatile uint8_t arp_pos;
static volatile uint8_t arp_walk_idx;
static volatile uint8_t arp_dir_dn;
static volatile uint32_t arp_tick_us;
static volatile uint32_t arp_gate_us;

static sf_loop_ev_t synth_loop[SYNTH_LOOP_MAX] PLAT_POOL;
static volatile uint16_t synth_loop_cnt;
static volatile uint8_t rec_armed;
static volatile uint8_t loop_play_active[SF_MAX_VOICES];
static volatile uint8_t loop_play_note[SF_MAX_VOICES];
static volatile uint32_t loop_play_rem_us[SF_MAX_VOICES];

static uint8_t set_dirty;
static uint8_t drum_store_dirty;
static uint32_t drum_edit_ms;
static uint8_t unsaved_random;
static uint8_t pending_nav_dir;
static int8_t pending_nav_delta;
static uint16_t rand_count;
static uint8_t target_bankx_slot;
static volatile uint8_t backup_hold;
static uint32_t rand_seed = 0x9E3779B9u;

static const uint8_t PAGE_SUBS[13] = {2u, 2u, 3u, 2u, 3u, 3u, 5u, 2u, 3u, 1u, 1u, 1u, 1u};
static const char *const BANK_CHAR = "ABCDEFGHIX";
static const char *const BANK_SUBS[10] = {
    "PADS & ATMOS", "LEADS & MONO", "BASS & ACID", "PLUCKS & BELL",
    "KEYS & CHORDS", "ARP & MOTION", "WOBBLE & DUB", "FX & CINEMA",
    "STARFISH ORIGINALS", "USER PRESETS"
};
static const uint8_t OSC_ORDER[SF_NOSC] = {
    SF_OSC_HYPERSAW, SF_OSC_SAW, SF_OSC_PULSE, SF_OSC_WT, SF_OSC_SYNC, SF_OSC_TRI
};
static const char *const OSC_NAMES[SF_NOSC] = {"HYPERSAW", "SAW", "PWM", "TRIANGLE", "SYNC", "WAVETABL"};
static const char *const SUB_NAMES[3] = {"SQUARE", "SINE", "SAWTOOTH"};
static const char *const FLT_NAMES[4] = {"LOWPASS", "BANDPASS", "HIGHPASS", "NOTCH"};
static const char *const FLT_SHORT[4] = {"LP", "BP", "HP", "BS"};
static const char *const ROUTE_NAMES[3] = {"SERIAL", "PARALLEL", "SPLIT"};
static const char *const SAT_NAMES[4] = {"SOFT", "TUBE", "HARD", "OFF"};
static const char *const CAT_NAMES[10] = {
    "LEAD", "BASS", "PAD", "PLUCK", "SYNC", "KEYS", "WOBBLE", "FX", "CHORD", "ATMOS"
};
static const char *const LFO_NAMES[SF_NLFO_DEST] = {
    "FLT1 CUTOFF", "FLT2 CUTOFF", "RESONANCE", "PITCH", "PANNING", "SPREAD", "AMP LEVEL", "CHORUS", "PHASER", "DELAY", "REVERB"
};
static const char *const LFO_SHAPES[4] = {"SINE", "TRIANGLE", "SAWTOOTH", "SQUARE"};
static const char *const PALETTE_NAMES[5] = {"EMERALD", "AMBER", "CYAN", "CRIMSON", "MONO"};
static const char *const MASCOT_COLOR_NAMES[6] = {"CORAL", "LAGOON", "SUNBURST", "NEBULA", "REEF", "TANGERIN"};
static const char *const DRUM_LOOP_NAMES[5] = {"1 BAR (16)", "2 BAR (32)", "3 BAR (48)", "4 BAR (64)", "SYNC SEQ"};
static const char *const SCALE_NAMES[SF_NSCALE] = {
    "CHROMATIC", "MAJOR", "NAT MINOR", "HARM MIN", "MEL MIN", "DORIAN",
    "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN", "MAJ PENT", "MIN PENT"
};
static const char *const CHORD_NAMES[SF_NCHORD] = {"OFF", "OCTAVE", "FIFTH", "MINOR 7", "MAJOR 7"};
static const char *const ARP_MODE_NAMES[SF_NARP_MODE] = {"UP", "DOWN", "UP/DN", "RAND", "RND WLK", "ORDER"};
static const char *const ARP_RATE_NAMES[SF_NARP_RATE] = {"1/4", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32", "1/32T"};
static const char *const DLY_SYNC_NAMES[6] = {"SYNC 1/16", "SYNC 1/8T", "SYNC 1/8", "SYNC 1/8D", "SYNC 1/4", "SYNC 1/4D"};
static const char *const BT_MODE_NAMES[4] = {"AUTO DUAL", "HOST SMK37", "DEVICE PC", "BT OFF"};
static const char *const OUT_MODE_NAMES[3] = {"AUTO/HP", "LINE -6dB", "HI +3dB"};
static const char *const KIT_NAMES[SF_DRUM_KITS] = {
    "TR-808 CLASSIC", "TR-909 CLUB", "CR-78 VINTAGE", "LINNDRUM PCM", "STAR ELECTRO",
    "HOUSE CHICAGO", "ACOUSTIC JAZZ", "FM PERC DX", "LO-FI DUST", "TRAP / UK BASS"
};
static const char *const KIT_SUBS[SF_DRUM_KITS] = {
    "DEEP SUB TR-808", "PUNCHY HOUSE / TECHNO", "BRUSH SNARE & BONGO", "80S GATED STUDIO PCM", "CRISP MODULAR PERC",
    "WARM 707 / 909 MIX", "WOOD KIT & BRONZE RIDE", "GLASSY 2-OP FM PERC", "12-BIT WARM SAMPLER", "LONG 808 + SHARP HAT"
};
static const char *const DRUM_NAMES[SF_DRUM_VOICES] = {
    "BD", "SD", "CH", "OH", "CP", "TM", "RD", "RS"
};
static const char *const BUS_NAMES[SF_NBUS] = {
    "CLEAN PCM", "WARM CONSOLE", "PUNCH COMP", "SUB BASS +", "CLUB ROOM", "FM SPARKLE"
};
static const char *const DRUM_PRESET_NAMES[5] = {"TECHNO", "HOUSE", "ELECTRO", "BREAK", "CLEAR"};
static const char *const RAND_NAMES[SF_NRAND_STYLE] = {
    "ALL", "PAD", "LEAD", "BASS", "PLUCK", "ARP", "KEYS", "FX"
};
static const char *const MASCOT_TITLES[6] = {
    "BEACH CHILLIN'", "MOON EXPLORER", "SUB CAPTAIN", "CAMPFIRE JAM", "ROCKET SKATER", "STARFISH CHEF"
};
static const char *const JINGLE_NAMES[6] = {
    "CORAL MAJ9", "LUNAR FIFTHS", "LAGOON WAVE", "STAR HOP", "REEF DREAM", "TURBO BOOT"
};
static const char *const NOTE12[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

typedef struct {
    uint16_t body;
    uint16_t belly;
    uint16_t cheek;
} sf_star_pal_t;

static const sf_star_pal_t STAR_PALS[6] = {
    {RGB(255, 107, 139), RGB(255, 155, 178), RGB(255, 62, 108)}, /* Coral Pink */
    {RGB(34, 211, 238),  RGB(103, 232, 249), RGB(6, 182, 212)},  /* Lagoon Cyan */
    {RGB(251, 191, 36),  RGB(253, 230, 138), RGB(245, 158, 11)}, /* Sunburst Amber */
    {RGB(167, 139, 250), RGB(196, 181, 253), RGB(139, 92, 246)}, /* Nebula Violet */
    {RGB(52, 211, 153),  RGB(110, 231, 183), RGB(16, 185, 129)}, /* Reef Emerald */
    {RGB(251, 146, 60),  RGB(253, 186, 116), RGB(234, 88, 12)},  /* Tangerine */
};

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

static void text_copy(char *d, const char *s, uint32_t max)
{
    uint32_t i;
    if (!max) return;
    for (i = 0; i + 1u < max && s && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static void text_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void text_upper(char *d, const char *s, uint32_t max)
{
    uint32_t i;
    if (!max) return;
    for (i = 0; i + 1u < max && s && s[i]; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        else if ((uint8_t)c < 32u || (uint8_t)c > 95u) c = ' ';
        d[i] = c;
    }
    d[i] = 0;
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

static void slot3_text(uint32_t n, char out[8])
{
    out[0] = 'X';
    out[1] = (char)('0' + (n / 100u) % 10u);
    out[2] = (char)('0' + (n / 10u) % 10u);
    out[3] = (char)('0' + n % 10u);
    out[4] = 0;
}

static uint16_t theme_accent(void)
{
    if (set.palette == 0u) return COL_GREEN;
    if (set.palette == 1u) return COL_AMBER;
    if (set.palette == 3u) return RGB(251, 113, 133);
    if (set.palette == 4u) return RGB(226, 232, 240);
    return COL_CYAN;
}

static uint16_t theme_dim(void)
{
    if (set.palette == 0u) return COL_GRN_DIM;
    if (set.palette == 1u) return COL_AMB_DIM;
    if (set.palette == 3u) return RGB(82, 22, 40);
    if (set.palette == 4u) return RGB(51, 65, 85);
    return COL_CYAN_DIM;
}

static uint16_t theme_glow(void)
{
    if (set.palette == 0u) return RGB(167, 243, 208);
    if (set.palette == 1u) return RGB(254, 243, 199);
    if (set.palette == 3u) return RGB(254, 205, 211);
    if (set.palette == 4u) return C_WHITE;
    return RGB(224, 242, 254);
}

static uint16_t theme_band(void)
{
    if (set.palette == 0u) return RGB(16, 110, 82);
    if (set.palette == 1u) return RGB(138, 98, 22);
    if (set.palette == 3u) return RGB(148, 44, 72);
    if (set.palette == 4u) return RGB(100, 116, 139);
    return RGB(24, 118, 138);
}

static uint32_t eff_drum_loop_len(uint32_t seq_len)
{
    uint32_t sl = seq_len < 1u ? 1u : (seq_len > SF_SEQ_STEPS ? SF_SEQ_STEPS : seq_len);
    if (set.drum_loop_bars == 1u) return 32u;
    if (set.drum_loop_bars == 2u) return 48u;
    if (set.drum_loop_bars == 3u) return 64u;
    if (set.drum_loop_bars >= 4u) return sl;
    return 16u;
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
    int hp = plat_headphones();
    uint32_t f = plat_lock();
    mem_copy(&eng.patch, &set.patch, (uint32_t)sizeof(sf_preset_t));
    eng.bpm = set.bpm;
    eng.swing = set.swing;
    eng.seq_length = set.seq_length;
    eng.drum_kit = set.drum_kit;
    eng.synth_vol = set.synth_vol;
    eng.drum_vol = set.drum_vol;
    eng.duck_amt = set.duck_amt;
    eng.duck_src = set.duck_src;
    eng.audio_out_mode = set.audio_out_mode;
    eng.hp_plugged = (uint8_t)(hp == 0 ? 0u : (hp == 1 ? 1u : 255u));
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
    if (bank == 8u) return &SF_ROM[256u + (prog % SF_BANK_I_SIZE)];
    return bankx_slot_ptr(prog % SF_BANK_X_SLOTS);
}

static void seed_recent_presets(void)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        set.recent_presets[i][0] = 8u;
        set.recent_presets[i][1] = (uint8_t)(26u + i); /* Last 6 Bank I originals (I027..I032) */
    }
}

static void record_recent_preset(uint8_t bank, uint8_t prog)
{
    uint32_t i, j;
    if (bank >= 10u) return;
    prog = (uint8_t)(prog % bank_len(bank));
    for (i = 0; i < 6u; i++) {
        if (set.recent_presets[i][0] == bank && set.recent_presets[i][1] == prog) {
            for (j = i; j > 0u; j--) {
                set.recent_presets[j][0] = set.recent_presets[j - 1u][0];
                set.recent_presets[j][1] = set.recent_presets[j - 1u][1];
            }
            set.recent_presets[0][0] = bank;
            set.recent_presets[0][1] = prog;
            return;
        }
    }
    for (j = 5u; j > 0u; j--) {
        set.recent_presets[j][0] = set.recent_presets[j - 1u][0];
        set.recent_presets[j][1] = set.recent_presets[j - 1u][1];
    }
    set.recent_presets[0][0] = bank;
    set.recent_presets[0][1] = prog;
}

static void mark_drum_mix_dirty(uint32_t now)
{
    drum_store_dirty = 1u;
    drum_edit_ms = now;
}

static void load_preset(uint8_t bank, uint8_t prog)
{
    static const uint8_t PRESET_ARP_RATE_MAP[4] = {2u, 5u, 7u, 8u};
    uint8_t keep_arp = arp_active;
    uint8_t keep_latch = set.arp_latch;
    set.bank = bank % 10u;
    set.prog = (uint8_t)(prog % bank_len(set.bank));
    mem_copy(&set.patch, get_preset_ptr(set.bank, set.prog), (uint32_t)sizeof(sf_preset_t));
    mem_copy(&patch_baseline, &set.patch, (uint32_t)sizeof(sf_preset_t));
    patch_compare_ab = 0u;
    record_recent_preset(set.bank, set.prog);
    set.arp_mode = sf_arp_mode(&set.patch);
    set.arp_rate = PRESET_ARP_RATE_MAP[sf_arp_rate(&set.patch) & 3u];
    set.arp_octs = sf_arp_octs(&set.patch);
    if (keep_arp || keep_latch) {
        arp_active = 1u;
        set.arp_latch = keep_latch;
    } else {
        arp_active = sf_arp_on(&set.patch);
        set.arp_latch = sf_arp_latch(&set.patch);
    }
    set_dirty = 0u;
    unsaved_random = 0u;
    mark_drum_mix_dirty(plat_ms());
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
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        mem_zero(&bankx_chunks[i], (uint32_t)sizeof(sf_bankx_chunk_t));
        bankx_chunks[i].magic = BANKX_MAGIC;
    }
    for (i = 0; i < SF_BANK_X_SLOTS; i++) {
        char lbl[16];
        uint32_t v = i + 1u;
        lbl[0] = 'U'; lbl[1] = 'S'; lbl[2] = 'E'; lbl[3] = 'R'; lbl[4] = ' ';
        lbl[5] = (char)('0' + (v / 100u) % 10u);
        lbl[6] = (char)('0' + (v / 10u) % 10u);
        lbl[7] = (char)('0' + v % 10u);
        lbl[8] = 0;
        make_init_patch(bankx_slot_ptr(i), lbl);
    }
}

static void expand_drum_bars(uint32_t old_len, uint32_t new_len)
{
    uint32_t t, s;
    uint32_t ol = old_len < 1u ? 1u : (old_len > SF_SEQ_STEPS ? SF_SEQ_STEPS : old_len);
    uint32_t nl = new_len < 1u ? 1u : (new_len > SF_SEQ_STEPS ? SF_SEQ_STEPS : new_len);
    if (nl <= ol) return;
    for (t = 0; t < SF_DRUM_VOICES; t++) {
        uint8_t has_existing = 0u;
        uint8_t has_new = 0u;
        for (s = 0; s < ol; s++) if (set.seq_pat[t][s]) { has_existing = 1u; break; }
        for (s = ol; s < nl; s++) if (set.seq_pat[t][s]) { has_new = 1u; break; }
        if (has_existing && !has_new) {
            for (s = ol; s < nl; s++) set.seq_pat[t][s] = set.seq_pat[t][s % ol];
        }
    }
}

static void apply_euclidean(uint32_t track, uint32_t pulses, uint32_t rot)
{
    uint32_t len = eff_drum_loop_len(set.seq_length);
    uint32_t s;
    if (track >= SF_DRUM_VOICES) return;
    if (len < 1u) len = 1u;
    if (len > SF_SEQ_STEPS) len = SF_SEQ_STEPS;
    if (pulses > len) pulses = len;
    set.euclid_pulses[track] = (uint8_t)pulses;
    set.euclid_rot[track] = (uint8_t)(rot % len);
    for (s = 0; s < SF_SEQ_STEPS; s++) set.seq_pat[track][s] = 0u;
    if (pulses == 0u) return;
    for (s = 0; s < len; s++) {
        uint32_t cur = (s * pulses) / len;
        if (s == 0u || cur != (((s - 1u) * pulses) / len)) {
            uint32_t pos = (s + set.euclid_rot[track]) % len;
            set.seq_pat[track][pos] = (s % 4u == 0u) ? 2u : 1u;
        }
    }
}

static void apply_drum_preset(uint8_t preset)
{
    uint32_t t, s;
    set.drum_preset = preset % 5u;
    mem_zero(set.seq_pat, (uint32_t)sizeof(set.seq_pat));
    mem_zero(set.euclid_pulses, (uint32_t)sizeof(set.euclid_pulses));
    mem_zero(set.euclid_rot, (uint32_t)sizeof(set.euclid_rot));
    if (set.drum_preset == 4u) { sync_engine_cfg(); return; }
    for (s = 0; s < SF_SEQ_STEPS; s++) {
        uint32_t st = s & 15u;
        if (set.drum_preset == 0u) { /* TECHNO */
            if ((st & 3u) == 0u) set.seq_pat[0][s] = (st == 0u) ? 2u : 1u;
            if (st == 4u || st == 12u) set.seq_pat[1][s] = 2u;
            if ((st & 3u) == 2u) set.seq_pat[2][s] = 1u;
            if (st == 10u) set.seq_pat[3][s] = 1u;
            if (st == 12u) set.seq_pat[4][s] = 1u;
            if (st == 7u || st == 15u) set.seq_pat[7][s] = 1u;
        } else if (set.drum_preset == 1u) { /* HOUSE */
            if ((st & 3u) == 0u) set.seq_pat[0][s] = (st == 0u || st == 8u) ? 2u : 1u;
            if (st == 4u || st == 12u) set.seq_pat[4][s] = 2u;
            if ((st & 3u) == 2u) set.seq_pat[3][s] = 2u;
            if ((st & 1u) == 0u) set.seq_pat[2][s] = 1u;
            if (st == 14u) set.seq_pat[1][s] = 1u;
        } else if (set.drum_preset == 2u) { /* ELECTRO */
            if (st == 0u || st == 6u || st == 10u) set.seq_pat[0][s] = 2u;
            if (st == 4u || st == 12u) set.seq_pat[1][s] = 2u;
            if ((st & 1u) == 0u) set.seq_pat[2][s] = 1u;
            if (st == 3u || st == 11u) set.seq_pat[7][s] = 1u;
            if (st == 14u) set.seq_pat[5][s] = 1u;
        } else { /* BREAK */
            if (st == 0u || st == 3u || st == 10u) set.seq_pat[0][s] = 2u;
            if (st == 4u || st == 7u || st == 9u || st == 12u) set.seq_pat[1][s] = (st == 4u || st == 12u) ? 2u : 1u;
            if ((st & 1u) == 0u) set.seq_pat[2][s] = 1u;
            if (st == 14u) set.seq_pat[3][s] = 1u;
        }
    }
    for (t = 0; t < SF_DRUM_VOICES; t++) {
        uint8_t cnt = 0u;
        for (s = 0; s < 16u; s++) if (set.seq_pat[t][s]) cnt++;
        set.euclid_pulses[t] = cnt;
    }
    sync_engine_cfg();
}

static void default_drums(void)
{
    uint32_t t;
    static const uint8_t DEF_DEC[8] = {105u, 100u, 85u, 110u, 105u, 100u, 115u, 90u};
    static const uint8_t DEF_TON[8] = {55u, 62u, 68u, 70u, 65u, 55u, 64u, 60u};
    static const uint8_t DEF_LVL[8] = {96u, 88u, 74u, 70u, 82u, 78u, 66u, 72u};
    for (t = 0; t < SF_DRUM_VOICES; t++) {
        set.drum_tune[t] = 0;
        set.drum_decay[t] = DEF_DEC[t];
        set.drum_tone[t] = DEF_TON[t];
        set.drum_level[t] = DEF_LVL[t];
    }
    set.drum_mute = 0u;
    apply_drum_preset(4u);
}

static void settings_defaults(void)
{
    mem_zero(&set, (uint32_t)sizeof(set));
    set.magic = SET_MAGIC;
    set.bank = 8u; /* Bank I: STARFISH ORIGINALS */
    set.prog = 0u; /* Star Phaser Pad */
    set.octave = 0;
    set.palette = 2u; /* Cyan */
    set.bpm = 122u;
    set.swing = 50u;
    set.seq_length = 16u;
    set.drum_kit = 0u;
    set.synth_vol = 90u;
    set.drum_vol = 82u;
    set.duck_amt = 35u;
    set.duck_src = 0u;
    set.drum_bus = 0u;
    set.midi_synth_ch = 1u;
    set.midi_drum_ch = 10u;
    set.bt_mode = 0u;
    set.audio_out_mode = 0u;
    set.scale = 0u;
    set.root = 0u;
    set.chord_mode = 0u;
    set.arp_mode = SF_ARP_UPDN;
    set.arp_rate = 5u; /* 1/16 */
    set.arp_octs = 1u;
    set.arp_gate = 72u;
    set.rand_chaos = 55u;
    set.rand_style = SF_RAND_ALL;
    set.rec_quant = 1u;
    set.drum_loop_bars = 0u; /* Default 1 BAR (16) so 16-step drums repeat cleanly under 2-4 bar synth loops */
    seed_recent_presets();
    default_drums();
}

int starfish_ble_off(void)
{
    sf_settings_t tmp;
    if (plat_store_head(OBJ_SETTINGS, &tmp, (uint32_t)sizeof(tmp)) == (int)sizeof(tmp) && tmp.magic == SET_MAGIC)
        return (tmp.ble_off || tmp.bt_mode == 3u) ? 1 : 0;
    return 0;
}

static void load_all_from_store(void)
{
    uint32_t i;
    static const uint32_t bx_objs[3] = {OBJ_BANKX0, OBJ_BANKX1, OBJ_BANKX2};
    bankx_defaults();
    for (i = 0; i < 3u; i++) {
        sf_bankx_chunk_t tmp;
        if (plat_store_load(bx_objs[i], &tmp, (uint32_t)sizeof(tmp)) == (int)sizeof(tmp) && tmp.magic == BANKX_MAGIC)
            mem_copy(&bankx_chunks[i], &tmp, (uint32_t)sizeof(tmp));
    }
    if (plat_store_load(OBJ_SETTINGS, &set, (uint32_t)sizeof(set)) != (int)sizeof(set) || set.magic != SET_MAGIC) {
        settings_defaults();
        load_preset(8u, 0u);
    } else {
        uint8_t valid_rp = 0u;
        if (set.bank >= 10u) set.bank = 8u;
        set.prog = (uint8_t)(set.prog % bank_len(set.bank));
        set.octave = (int8_t)clamp(set.octave, -2, 2);
        if (set.palette > 4u) set.palette = 2u;
        if (set.bpm < 40u || set.bpm > 240u) set.bpm = 122u;
        if (set.swing < 50u || set.swing > 75u) set.swing = 50u;
        if (set.seq_length < 1u || set.seq_length > SF_SYNTH_MAX_STEPS) set.seq_length = 16u;
        if (set.drum_kit >= SF_DRUM_KITS) set.drum_kit = 0u;
        if (set.synth_vol > 100u) set.synth_vol = 90u;
        if (set.drum_vol > 100u) set.drum_vol = 82u;
        if (set.duck_amt > 100u) set.duck_amt = 35u;
        if (set.duck_src >= SF_DRUM_VOICES) set.duck_src = 0u;
        if (set.drum_bus >= SF_NBUS) set.drum_bus = 0u;
        if (set.midi_synth_ch > 16u) set.midi_synth_ch = 1u;
        if (set.midi_drum_ch > 16u) set.midi_drum_ch = 10u;
        if (set.bt_mode > 3u) set.bt_mode = 0u;
        set.ble_off = (set.bt_mode == 3u) ? 1u : 0u;
        if (set.audio_out_mode > 2u) set.audio_out_mode = 0u;
        if (set.scale >= SF_NSCALE) set.scale = 0u;
        if (set.root >= 12u) set.root = 0u;
        if (set.chord_mode >= SF_NCHORD) set.chord_mode = 0u;
        if (set.arp_mode >= SF_NARP_MODE) set.arp_mode = SF_ARP_UPDN;
        if (set.arp_rate >= SF_NARP_RATE) set.arp_rate = 5u;
        if (set.arp_octs < 1u || set.arp_octs > 3u) set.arp_octs = 1u;
        if (set.arp_gate < 20u || set.arp_gate > 95u) set.arp_gate = 72u;
        if (set.rand_chaos < 20u || set.rand_chaos > 100u) set.rand_chaos = 55u;
        if (set.rand_style >= SF_NRAND_STYLE) set.rand_style = 0u;
        if (set.drum_preset > 4u) set.drum_preset = 4u;
        if (set.drum_loop_bars > 4u) set.drum_loop_bars = 0u;
        set.rec_quant = set.rec_quant ? 1u : 0u;
        for (i = 0; i < 6u; i++) {
            if (set.recent_presets[i][0] < 10u && set.recent_presets[i][1] < bank_len(set.recent_presets[i][0])) {
                if (set.recent_presets[i][0] != 0u || set.recent_presets[i][1] != 0u) valid_rp = 1u;
            }
        }
        if (!valid_rp) seed_recent_presets();
        for (i = 0; i < 6u; i++) {
            if (set.recent_presets[i][0] >= 10u) set.recent_presets[i][0] = 8u;
            set.recent_presets[i][1] = (uint8_t)(set.recent_presets[i][1] % bank_len(set.recent_presets[i][0]));
        }
        for (i = 0; i < SF_DRUM_VOICES; i++) {
            if (set.drum_tune[i] < -12 || set.drum_tune[i] > 12) set.drum_tune[i] = 0;
            if (set.drum_decay[i] < 25u || set.drum_decay[i] > 200u) set.drum_decay[i] = 100u;
            if (set.drum_tone[i] > 100u) set.drum_tone[i] = 60u;
            if (set.drum_level[i] > 100u) set.drum_level[i] = 80u;
        }
        mem_copy(&patch_baseline, &set.patch, (uint32_t)sizeof(sf_preset_t));
        patch_compare_ab = 0u;
        arp_active = sf_arp_on(&set.patch);
        plat_ble_enable(set.ble_off ? 0 : 1);
        sync_engine_cfg();
    }
    target_bankx_slot = bankx_next_free();
    set_dirty = 0u;
    drum_store_dirty = 0u;
    unsaved_random = 0u;
}

static void save_settings_to_store(void)
{
    if (backup_hold) return;
    set.magic = SET_MAGIC;
    if (plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set)) == 0) {
        set_dirty = 0u;
        drum_store_dirty = 0u;
    }
}


static uint8_t quantize_note(int32_t note)
{
    static const uint16_t MASKS[SF_NSCALE] = {
        0x0FFFu, 0x0AB5u, 0x0AD5u, 0x0CE5u, 0x0CEAu, 0x0AA9u,
        0x05ADu, 0x0B55u, 0x0AA5u, 0x056Du, 0x0295u, 0x04A9u
    };
    uint16_t m = MASKS[set.scale % SF_NSCALE];
    int32_t n = clamp(note, 12, 119);
    int32_t d, best = n, best_d = 99;
    if (set.scale == 0u) return (uint8_t)n;
    for (d = -6; d <= 6; d++) {
        int32_t cand = n + d;
        int32_t pc = ((cand - (int32_t)set.root) % 12 + 12) % 12;
        if ((m >> (uint32_t)pc) & 1u) {
            int32_t ad = d < 0 ? -d : d;
            if (ad < best_d) { best_d = ad; best = cand; }
        }
    }
    return (uint8_t)clamp(best, 12, 119);
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

static void randomize_patch(void)
{
    static const char *const ADJ[8] = {"STAR", "NEON", "COSMIC", "VELVET", "SOLAR", "PRISM", "DEEP", "TIDAL"};
    static const char *const NOUN[8] = {"DRIFT", "WAVE", "PULSE", "BLOOM", "SPARK", "CLOUD", "ECHO", "CHORD"};
    uint32_t r = xorshift32();
    uint32_t chaos = set.rand_chaos;
    sf_preset_t p;
    uint32_t st = set.rand_style % SF_NRAND_STYLE;
    char nm[16];
    mem_zero(&p, (uint32_t)sizeof(p));
    text_copy(nm, ADJ[r & 7u], 15u);
    text_cat(nm, " ");
    text_cat(nm, NOUN[(r >> 3) & 7u]);
    text_copy(p.name, nm, 16u);
    p.cat = (uint8_t)(st == SF_RAND_PAD ? 2u : st == SF_RAND_BASS ? 1u : st == SF_RAND_PLUCK ? 3u : 0u);
    p.osc_meta = (uint8_t)(OSC_ORDER[(xorshift32() % SF_NOSC)] | ((xorshift32() % 3u) << 4));
    p.pw = (uint8_t)(20u + (xorshift32() % 90u));
    p.osc2_semi = (st == SF_RAND_BASS) ? (int8_t)((xorshift32() & 1u) ? -12 : 0) : (int8_t)((xorshift32() % 3u) * 7 - 7);
    p.osc2_detune = (uint8_t)(6u + (xorshift32() % (12u + chaos / 3u)));
    p.osc_bal = (uint8_t)(25u + (xorshift32() % 55u));
    p.sub_vol = (uint8_t)(st == SF_RAND_BASS ? 75u : (20u + (xorshift32() % 55u)));
    p.unison_poly = (uint8_t)((st == SF_RAND_BASS ? 2u : (xorshift32() % 3u)) | (7u << 3));
    p.spread = (uint8_t)(30u + (xorshift32() % 65u));
    p.uni_pan = (uint8_t)(40u + (xorshift32() % 60u));
    p.f1_cut = (uint8_t)(38u + (xorshift32() % 68u));
    p.f1_res = (uint8_t)(10u + (xorshift32() % (25u + chaos / 3u)));
    p.f2_cut = (uint8_t)(75u + (xorshift32() % 50u));
    p.f2_res = (uint8_t)(8u + (xorshift32() % 28u));
    p.f1_env_s = (int8_t)(18 + (int32_t)(xorshift32() % 65u));
    p.f_bal = 50u;
    p.f_keyfollow = 35u;
    p.f2_type_route = (uint8_t)((xorshift32() % 3u) | (0u << 2) | ((xorshift32() % 3u) << 4) | (SF_SAT_SOFT << 6));
    if (st == SF_RAND_PAD) { p.a_atk = 35u + (xorshift32() % 45u); p.a_rel = 60u + (xorshift32() % 45u); }
    else if (st == SF_RAND_PLUCK || st == SF_RAND_ARP) { p.a_atk = 2u; p.a_dec = 35u + (xorshift32() % 35u); p.a_sus = 15u + (xorshift32() % 25u); p.a_rel = 25u + (xorshift32() % 30u); }
    else { p.a_atk = 2u + (xorshift32() % 14u); p.a_dec = 50u + (xorshift32() % 50u); p.a_sus = 65u + (xorshift32() % 45u); p.a_rel = 28u + (xorshift32() % 40u); }
    p.f_atk = p.a_atk;
    p.f_dec = (uint8_t)(30u + (xorshift32() % 65u));
    p.f_sus = (uint8_t)(20u + (xorshift32() % 60u));
    p.f_rel = p.a_rel;
    p.lfo1_rate = (uint8_t)(12u + (xorshift32() % 48u));
    p.lfo1_depth = (uint8_t)(8u + (xorshift32() % (16u + chaos / 4u)));
    p.lfo1_meta = (uint8_t)(xorshift32() % 5u);
    p.lfo2_rate = (uint8_t)(8u + (xorshift32() % 24u));
    p.lfo2_depth = (uint8_t)(8u + (xorshift32() % 25u));
    p.lfo2_meta = (uint8_t)(SF_LFO_PAN | (1u << 4));
    p.swarm_depth = (uint8_t)(14u + (xorshift32() % 32u));
    p.swarm_rate = 10u;
    p.fx_chorus = (uint8_t)(20u + (xorshift32() % 55u));
    p.fx_phaser = (uint8_t)(st == SF_RAND_PAD ? 45u : (xorshift32() % 35u));
    p.fx_phaser_rate = 6u;
    p.fx_del_mix = (uint8_t)(18u + (xorshift32() % 38u));
    p.fx_del_time = 28u;
    p.fx_del_fb = 36u;
    p.fx_reverb = (uint8_t)(28u + (xorshift32() % 52u));
    p.fx_drive = (uint8_t)(xorshift32() % 30u);
    p.arp_meta = (uint8_t)((SF_ARP_UPDN) | (1u << 2) | ((st == SF_RAND_ARP ? 1u : 0u) << 7));
    mem_copy(&set.patch, &p, (uint32_t)sizeof(p));
    rand_count++;
    unsaved_random = 1u;
    set_dirty = 1u;
    sync_engine_cfg();
}

static void save_to_bankx_slot(uint32_t slot)
{
    uint32_t ci = slot / SF_BANK_X_CHUNK;
    uint32_t sub = slot % SF_BANK_X_CHUNK;
    static const uint32_t bx_objs[3] = {OBJ_BANKX0, OBJ_BANKX1, OBJ_BANKX2};
    if (slot >= SF_BANK_X_SLOTS || backup_hold) return;
    bankx_chunks[ci].magic = BANKX_MAGIC;
    bankx_chunks[ci].used[sub] = 1u;
    mem_copy(&bankx_chunks[ci].slots[sub], &set.patch, (uint32_t)sizeof(sf_preset_t));
    plat_store_save(bx_objs[ci], &bankx_chunks[ci], (uint32_t)sizeof(sf_bankx_chunk_t));
    set.bank = 9u;
    set.prog = (uint8_t)slot;
    unsaved_random = 0u;
    target_bankx_slot = bankx_next_free();
    save_settings_to_store();
}

static void save_to_current_slot(void)
{
    if (set.bank == 9u) save_to_bankx_slot(set.prog);
    else { unsaved_random = 0u; save_settings_to_store(); }
}

static void revert_to_factory(uint8_t init_mode)
{
    if (init_mode) {
        make_init_patch(&set.patch, "STARFISH INIT");
        unsaved_random = 0u;
        set_dirty = 1u;
        sync_engine_cfg();
        save_settings_to_store();
    } else if (set.bank == 9u) {
        uint32_t ci = set.prog / SF_BANK_X_CHUNK;
        uint32_t sub = set.prog % SF_BANK_X_CHUNK;
        static const uint32_t bx_objs[3] = {OBJ_BANKX0, OBJ_BANKX1, OBJ_BANKX2};
        char lbl[16];
        uint32_t v = set.prog + 1u;
        lbl[0] = 'U'; lbl[1] = 'S'; lbl[2] = 'E'; lbl[3] = 'R'; lbl[4] = ' ';
        lbl[5] = (char)('0' + (v / 100u) % 10u);
        lbl[6] = (char)('0' + (v / 10u) % 10u);
        lbl[7] = (char)('0' + v % 10u);
        lbl[8] = 0;
        bankx_chunks[ci].used[sub] = 0u;
        make_init_patch(&bankx_chunks[ci].slots[sub], lbl);
        plat_store_save(bx_objs[ci], &bankx_chunks[ci], (uint32_t)sizeof(sf_bankx_chunk_t));
        load_preset(9u, set.prog);
        save_settings_to_store();
    } else {
        load_preset(set.bank, set.prog);
        save_settings_to_store();
    }
}

static void send_midi_note_on(uint8_t note, uint8_t vel)
{
    plat_midi_send(0x09u | (0x90u << 8) | ((uint32_t)note << 16) | ((uint32_t)vel << 24), TO_USB | TO_BLE);
}

static void send_midi_note_off(uint8_t note)
{
    plat_midi_send(0x08u | (0x80u << 8) | ((uint32_t)note << 16), TO_USB | TO_BLE);
}

static void arp_order_add(uint16_t tok)
{
    uint32_t i;
    for (i = 0; i < arp_order_cnt; i++) {
        if (arp_order_buf[i] == tok) return;
    }
    if (arp_order_cnt < 24u)
        arp_order_buf[arp_order_cnt++] = tok;
}

static void arp_order_remove(uint16_t tok)
{
    uint32_t i, j;
    for (i = 0; i < arp_order_cnt; i++) {
        if (arp_order_buf[i] == tok) {
            for (j = i + 1u; j < arp_order_cnt; j++)
                arp_order_buf[j - 1u] = arp_order_buf[j];
            arp_order_cnt--;
            return;
        }
    }
}

static uint8_t gm_note_to_drum(uint8_t note)
{
    if (note == 35u || note == 36u) return 0u; /* BD */
    if (note == 38u || note == 40u) return 1u; /* SD */
    if (note == 42u || note == 44u) return 2u; /* CH */
    if (note == 46u) return 3u;                /* OH */
    if (note == 39u) return 4u;                /* CP */
    if (note == 41u || note == 43u || note == 45u || note == 47u || note == 48u || note == 50u) return 5u; /* TM */
    if (note == 49u || note == 51u || note == 52u || note == 55u || note == 57u || note == 59u) return 6u; /* RD */
    if (note == 37u || note == 56u) return 7u; /* RS */
    if (note >= 53u && note < 61u) return (uint8_t)(note - 53u);
    return (uint8_t)(note & 7u);
}

static void midi_in(uint32_t pkt, int port)
{
    uint8_t st = (uint8_t)(pkt >> 8);
    uint8_t d1 = (uint8_t)(pkt >> 16);
    uint8_t d2 = (uint8_t)(pkt >> 24);
    uint8_t type = st & 0xF0u;
    uint8_t ch = (uint8_t)((st & 0x0Fu) + 1u);
    uint8_t is_drum_ch = (set.midi_drum_ch > 0u && ch == set.midi_drum_ch) ? 1u : 0u;
    uint8_t is_synth_ch = (set.midi_synth_ch == 0u || ch == set.midi_synth_ch) ? 1u : 0u;
    (void)port;

    if (is_drum_ch) {
        if (type == 0x90u && d2 > 0u) {
            uint8_t dv = gm_note_to_drum(d1);
            sf_dsp_trigger_drum(dv, d2);
            if (rec_armed && seq_playing) {
                uint32_t dlen = eff_drum_loop_len(set.seq_length);
                set.seq_pat[dv][seq_step % dlen] = (d2 > 115u) ? 2u : 1u;
                mark_drum_mix_dirty(plat_ms());
                sync_engine_cfg();
            }
        }
        return;
    }

    if (!is_synth_ch)
        return;

    if (type == 0x90u && d2 > 0u) {
        uint8_t q = quantize_note((int32_t)d1);
        uint32_t w = (uint32_t)(d1 & 127u) >> 5;
        uint32_t bbit = 1u << (uint32_t)(d1 & 31u);
        uint32_t f = plat_lock();
        arp_midi_mask[w] |= bbit;
        if (set.arp_latch) arp_midi_latch_mask[w] |= bbit;
        arp_order_add((uint16_t)(0x0200u | (d1 & 127u)));
        plat_unlock(f);

        if (!arp_active) {
            int8_t ivs[4];
            uint32_t cnt = chord_intervals(ivs), i;
            if (rec_armed && !count_in_active) {
                uint32_t base_step_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
                uint32_t len = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
                uint32_t lf = plat_lock();
                if (!seq_playing) {
                    seq_step = 0u;
                    tick_acc_us = 0u;
                    loop_cycle_us = 0u;
                    seq_playing = 1u;
                }
                {
                    uint32_t sw = set.swing < 50u ? 50u : (set.swing > 75u ? 75u : set.swing);
                    uint32_t step_us = ((seq_step & 1u) == 0u) ? (base_step_us * sw) / 50u : (base_step_us * (100u - sw)) / 50u;
                    uint32_t q_step = (tick_acc_us * 2u >= step_us) ? ((seq_step + 1u) % len) : seq_step;
                    uint32_t total_us = base_step_us * len;
                    uint32_t off_us = loop_cycle_us < total_us ? loop_cycle_us : (total_us > 1000u ? total_us - 1000u : 0u);
                    for (i = 0; i < cnt && synth_loop_cnt < SYNTH_LOOP_MAX; i++) {
                        uint8_t nn = (uint8_t)clamp((int32_t)q + ivs[i], 12, 119);
                        sf_loop_ev_t *ev = &synth_loop[synth_loop_cnt++];
                        ev->step = (uint16_t)q_step;
                        ev->note = nn;
                        ev->vel = d2;
                        ev->len_steps = 2u;
                        ev->off_us = off_us;
                        ev->dur_us = base_step_us * 2u;
                        ev->holdoff_ms = 220u;
                        ev->rec_start_ms = rec_tick_ms;
                        ev->quant = set.rec_quant ? 1u : 0u;
                        ev->active_src = 2u;
                        ev->active_key = d1 & 127u;
                    }
                }
                plat_unlock(lf);
            }
            for (i = 0; i < cnt; i++) {
                uint8_t nn = (uint8_t)clamp((int32_t)q + ivs[i], 12, 119);
                sf_dsp_note_on(nn, d2);
            }
        }
    } else if (type == 0x80u || (type == 0x90u && d2 == 0u)) {
        int8_t ivs[4];
        uint32_t cnt = chord_intervals(ivs), i;
        uint8_t q = quantize_note((int32_t)d1);
        uint32_t base_step_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        uint32_t len = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
        uint32_t max_dur = base_step_us * len;
        uint32_t w = (uint32_t)(d1 & 127u) >> 5;
        uint32_t bbit = 1u << (uint32_t)(d1 & 31u);
        uint32_t f = plat_lock();
        arp_midi_mask[w] &= ~bbit;
        if (!set.arp_latch)
            arp_order_remove((uint16_t)(0x0200u | (d1 & 127u)));
        for (i = 0; i < synth_loop_cnt; i++) {
            if (synth_loop[i].active_src == 2u && synth_loop[i].active_key == (d1 & 127u)) {
                uint32_t held_ms = rec_tick_ms - synth_loop[i].rec_start_ms;
                uint32_t dur_us = held_ms * 1000u;
                uint32_t min_d = base_step_us / 2u;
                if (dur_us < min_d) dur_us = min_d;
                if (dur_us > max_dur) dur_us = max_dur;
                synth_loop[i].dur_us = dur_us;
                synth_loop[i].len_steps = (uint16_t)clamp((int32_t)((dur_us + base_step_us / 2u) / base_step_us), 1, (int32_t)len);
                synth_loop[i].active_src = 0u;
                synth_loop[i].active_key = 0xFFu;
            }
        }
        plat_unlock(f);
        for (i = 0; i < cnt; i++) sf_dsp_note_off((uint8_t)clamp((int32_t)q + ivs[i], 12, 119));
    } else if (type == 0xA0u) {
        sf_dsp_aftertouch(quantize_note((int32_t)d1), d2);
    } else if (type == 0xB0u) {
        if (d1 == 74u) { set.patch.f1_cut = d2 & 127u; set_dirty = 1u; sync_engine_cfg(); }
        else if (d1 == 71u) { set.patch.f1_res = d2 & 127u; set_dirty = 1u; sync_engine_cfg(); }
        else if (d1 == 7u) { set.synth_vol = (uint8_t)(((uint32_t)(d2 & 127u) * 100u) / 127u); sync_engine_cfg(); }
        else if (d1 == 120u || d1 == 123u) sf_dsp_panic();
    } else if (type == 0xC0u) {
        load_preset(set.bank, d1);
    }
}

static void stop_loop_playback(void)
{
    uint32_t i;
    uint8_t notes[SF_MAX_VOICES];
    uint8_t cnt = 0u;
    uint32_t f = plat_lock();
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (loop_play_active[i]) {
            notes[cnt++] = loop_play_note[i];
            loop_play_active[i] = 0u;
            loop_play_rem_us[i] = 0u;
        }
    }
    plat_unlock(f);
    for (i = 0; i < cnt; i++) sf_dsp_note_off(notes[i]);
}

static void loop_trigger_note(uint8_t nn, uint8_t vel, uint32_t dur_us)
{
    uint32_t i;
    int32_t pick = -1;
    uint32_t min_rem = 0xFFFFFFFFu;
    uint8_t prev_off = 0u;
    uint8_t prev_n = 0u;
    for (i = 0; i < SF_MAX_VOICES; i++) {
        if (loop_play_active[i] && loop_play_note[i] == nn) {
            pick = (int32_t)i;
            prev_off = 1u;
            prev_n = nn;
            break;
        }
    }
    if (pick < 0) {
        for (i = 0; i < SF_MAX_VOICES; i++) {
            if (!loop_play_active[i]) { pick = (int32_t)i; break; }
            if (loop_play_rem_us[i] < min_rem) { min_rem = loop_play_rem_us[i]; pick = (int32_t)i; }
        }
        if (pick >= 0 && loop_play_active[pick]) {
            prev_off = 1u;
            prev_n = loop_play_note[pick];
        }
    }
    if (prev_off) sf_dsp_note_off(prev_n);
    if (pick >= 0) {
        loop_play_active[pick] = 1u;
        loop_play_note[pick] = nn;
        loop_play_rem_us[pick] = dur_us < 20000u ? 20000u : dur_us;
    }
    sf_dsp_note_on(nn, vel);
}

void app_init(void)
{
    uint32_t i;
    for (i = 0; i < SF_MAX_VOICES; i++) { loop_play_active[i] = 0u; loop_play_note[i] = 0u; loop_play_rem_us[i] = 0u; }
    for (i = 0; i < SYNTH_LOOP_MAX; i++) { synth_loop[i].active_src = 0u; synth_loop[i].active_key = 0xFFu; }
    sf_dsp_init(&eng);
    load_all_from_store();
}

void app_splash(void)
{
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        plat_midi_send(pkt, (TO_USB | TO_BLE) & ~plat_midi_in_from());
        midi_in(pkt, port);
    }
    PLAT_FAR(sf_dsp_render)(lr, n, master_q12);
}

void app_panic(void)
{
    stop_loop_playback();
    sf_dsp_panic();
}

void app_tick(void)
{
    uint32_t len = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
    uint32_t i;
    rec_tick_ms++;
    for (i = 0; i < synth_loop_cnt; i++) {
        if (synth_loop[i].holdoff_ms > 0u) synth_loop[i].holdoff_ms--;
    }
    if (count_in_active) {
        uint32_t beat_us = 60000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        count_in_acc_us += 1000u;
        if (count_in_acc_us >= beat_us) {
            count_in_acc_us -= beat_us;
            if (count_in_beat < 4u) {
                count_in_beat++;
                sf_dsp_click(0u);
            } else {
                count_in_active = 0u;
                count_in_beat = 0u;
                seq_step = 0u;
                tick_acc_us = 0u;
                loop_cycle_us = 0u;
                seq_playing = 1u;
                {
                    uint32_t d, dlen = eff_drum_loop_len(len);
                    (void)dlen;
                    for (d = 0; d < SF_DRUM_VOICES; d++) {
                        if ((set.drum_mute >> d) & 1u) continue;
                        if (set.seq_pat[d][0]) sf_dsp_trigger_drum((uint8_t)d, set.seq_pat[d][0] > 1u ? 124u : 100u);
                    }
                }
            }
        }
    } else if (seq_playing) {
        uint32_t base_step_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        uint32_t sw = set.swing < 50u ? 50u : (set.swing > 75u ? 75u : set.swing);
        uint32_t step_us = ((seq_step & 1u) == 0u)
            ? (base_step_us * sw) / 50u
            : (base_step_us * (100u - sw)) / 50u;
        uint32_t prev_cyc = loop_cycle_us;
        loop_cycle_us += 1000u;

        for (i = 0; i < SF_MAX_VOICES; i++) {
            if (loop_play_active[i]) {
                if (loop_play_rem_us[i] <= 1000u) {
                    uint8_t off_n = loop_play_note[i];
                    loop_play_active[i] = 0u;
                    loop_play_rem_us[i] = 0u;
                    sf_dsp_note_off(off_n);
                } else {
                    loop_play_rem_us[i] -= 1000u;
                }
            }
        }

        for (i = 0; i < synth_loop_cnt; i++) {
            if (!synth_loop[i].quant && synth_loop[i].holdoff_ms == 0u) {
                uint32_t o = synth_loop[i].off_us;
                if (o >= prev_cyc && o < loop_cycle_us) {
                    loop_trigger_note(synth_loop[i].note, synth_loop[i].vel, synth_loop[i].dur_us);
                }
            }
        }

        tick_acc_us += 1000u;
        if (tick_acc_us >= step_us) {
            uint32_t d;
            uint32_t dlen = eff_drum_loop_len(len);
            uint32_t dstep = (uint32_t)seq_step % dlen;
            tick_acc_us -= step_us;
            for (d = 0; d < SF_DRUM_VOICES; d++) {
                if ((set.drum_mute >> d) & 1u) continue;
                uint8_t hit = set.seq_pat[d][dstep];
                if (hit) sf_dsp_trigger_drum((uint8_t)d, hit > 1u ? 124u : 100u);
            }
            for (i = 0; i < synth_loop_cnt; i++) {
                if (synth_loop[i].quant && synth_loop[i].holdoff_ms == 0u && synth_loop[i].step == (uint16_t)seq_step) {
                    uint32_t dur = (uint32_t)synth_loop[i].len_steps * base_step_us;
                    if (dur < 20000u) dur = base_step_us;
                    loop_trigger_note(synth_loop[i].note, synth_loop[i].vel, dur);
                }
            }
            seq_step = (uint16_t)((seq_step + 1u) % len);
            if (seq_step == 0u) loop_cycle_us = tick_acc_us;
        }
    }
    if (arp_active) {
        uint8_t notes_sorted[40], notes_ord[40];
        uint32_t k, n_notes = 0u, n_ord = 0u;
        static const uint32_t RATE_DIV[SF_NARP_RATE] = {
            60000000u, 40000000u, 30000000u, 45000000u, 20000000u,
            15000000u, 22500000u, 10000000u, 7500000u, 5000000u
        };
        uint32_t rate_us = RATE_DIV[set.arp_rate % SF_NARP_RATE] / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        uint32_t octs = set.arp_octs < 1u ? 1u : (set.arp_octs > 3u ? 3u : set.arp_octs);
        for (k = 0; k < NKEYS && n_notes < 40u; k++) {
            if (arp_held[k] || (set.arp_latch && arp_latch_buf[k]))
                notes_sorted[n_notes++] = (uint8_t)clamp(53 + (int32_t)k + (int32_t)set.octave * 12, 12, 119);
        }
        for (k = 0; k < 128u && n_notes < 40u; k++) {
            uint32_t w = k >> 5, bbit = 1u << (k & 31u);
            if ((arp_midi_mask[w] & bbit) || (set.arp_latch && (arp_midi_latch_mask[w] & bbit)))
                notes_sorted[n_notes++] = (uint8_t)clamp((int32_t)k, 12, 119);
        }
        for (k = 0; k + 1u < n_notes; k++) {
            uint32_t m;
            for (m = k + 1u; m < n_notes; m++) {
                if (notes_sorted[m] < notes_sorted[k]) {
                    uint8_t tmp = notes_sorted[k]; notes_sorted[k] = notes_sorted[m]; notes_sorted[m] = tmp;
                }
            }
        }
        for (k = 0; k < arp_order_cnt && n_ord < 40u; k++) {
            uint16_t tok = arp_order_buf[k];
            if ((tok & 0xFF00u) == 0x0100u) {
                uint8_t pk = (uint8_t)(tok & 0xFFu);
                if (pk < NKEYS && (arp_held[pk] || set.arp_latch))
                    notes_ord[n_ord++] = (uint8_t)clamp(53 + (int32_t)pk + (int32_t)set.octave * 12, 12, 119);
            } else if ((tok & 0xFF00u) == 0x0200u) {
                uint8_t mn = (uint8_t)(tok & 127u);
                uint32_t w = mn >> 5, bbit = 1u << (mn & 31u);
                if ((arp_midi_mask[w] & bbit) || set.arp_latch)
                    notes_ord[n_ord++] = (uint8_t)clamp((int32_t)mn, 12, 119);
            }
        }
        if (arp_cur_note >= 0) {
            arp_gate_us += 1000u;
            uint32_t g_lim = (rate_us * (uint32_t)set.arp_gate) / 100u;
            if (arp_gate_us >= g_lim) {
                sf_dsp_note_off((uint8_t)arp_cur_note);
                arp_cur_note = -1;
            }
        }
        if (n_notes == 0u) {
            arp_tick_us = 0u;
            arp_pos = 0u;
            arp_walk_idx = 0u;
        } else {
            arp_tick_us += 1000u;
            if (arp_tick_us >= rate_us) {
                uint32_t total = n_notes * octs, idx = 0u;
                uint8_t base_n, pick_oct;
                arp_tick_us = 0u;
                arp_gate_us = 0u;
                if (arp_cur_note >= 0) {
                    sf_dsp_note_off((uint8_t)arp_cur_note);
                    arp_cur_note = -1;
                }
                if (set.arp_mode == SF_ARP_RND) {
                    idx = xorshift32() % total;
                } else if (set.arp_mode == SF_ARP_RND_WALK) {
                    int32_t step = (xorshift32() & 1u) ? 1 : -1;
                    int32_t nw = (int32_t)arp_walk_idx + step;
                    if (nw < 0) nw = (int32_t)total - 1;
                    if (nw >= (int32_t)total) nw = 0;
                    arp_walk_idx = (uint8_t)nw;
                    idx = (uint32_t)nw;
                } else if (set.arp_mode == SF_ARP_DN) {
                    idx = (total - 1u) - (arp_pos % total);
                } else if (set.arp_mode == SF_ARP_UPDN && total > 1u) {
                    uint32_t cyc = (total - 1u) * 2u;
                    uint32_t p = arp_pos % cyc;
                    idx = p < total ? p : cyc - p;
                } else {
                    idx = arp_pos % total;
                }
                arp_pos = (uint8_t)((arp_pos + 1u) % (total * 2u));
                if (set.arp_mode == SF_ARP_ORDER && n_ord > 0u) {
                    uint32_t ot = n_ord * octs;
                    uint32_t oidx = idx % ot;
                    base_n = notes_ord[oidx % n_ord];
                    pick_oct = (uint8_t)(oidx / n_ord);
                } else {
                    base_n = notes_sorted[idx % n_notes];
                    pick_oct = (uint8_t)(idx / n_notes);
                }
                uint8_t nn = quantize_note((int32_t)base_n + (int32_t)pick_oct * 12);
                sf_dsp_note_on(nn, 106u);
                arp_cur_note = (int8_t)nn;
            }
        }
    } else if (arp_cur_note >= 0) {
        sf_dsp_note_off((uint8_t)arp_cur_note);
        arp_cur_note = -1;
    }
}

static struct {
    uint8_t page;
    uint8_t sub[13];
    uint8_t sel_drum;
    uint8_t sel_step;
    uint8_t mute_held;
    uint8_t mute_latched;
    uint8_t mute_used;
    uint32_t drum_clr_start;
    int8_t drum_clr_track;
    uint8_t drum_clr_done;
    uint32_t save_hold_start;
    uint8_t save_did_long;
    uint32_t rec_hold_start;
    uint8_t rec_did_long;
    uint32_t arp_hold_start;
    uint8_t arp_did_long;
    uint8_t arp_combo_used;
    uint32_t seq_hold_start;
    uint8_t seq_did_long;
    uint8_t seq_entered_on_down;
    uint8_t disco_mode;
    uint32_t last_activity_ms;
    uint32_t boot_start;
    uint32_t boot_until;
    uint8_t boot_act;
    uint8_t boot_jingle;
    char boot_preset_name[16];
    uint8_t redraw;
    uint32_t prev_btn;
    uint32_t prev_keys;
    uint32_t last_menu_ms;
    uint32_t last_draw_ms;
    uint8_t last_env_seg; /* 0=none,1=A,2=D,3=S,4=R */
    uint32_t last_env_until;
    uint32_t save_flash_until;
    char save_banner[24];

    /* Overlays & Confirmation Modals */
    uint8_t hud_kind;
    uint32_t hud_until;
    char hud_title[28];
    char hud_sub[36];
    char hud_prev[20];
    char hud_next[20];
    char hud_code[12];
    uint16_t hud_accent;
    uint8_t hud_knob;
    int32_t hud_val_pct;

    uint8_t conf_type;
    uint8_t conf_dest_slot;   /* 0=bankX, 1=current slot */
    uint8_t conf_reset_init;  /* 0=factory, 1=init */
    uint8_t conf_clear_target;/* 0=SYNTH LOOP, 1=DRUM PATS, 2=BOTH */
    uint8_t conf_bx_slot;

    /* Starfish Top-Right Animation & Physics */
    uint32_t star_note_seq;
    uint32_t star_drum_seq;
    float star_bounce;
    float star_swivel;
    float star_swivel_targ;
    float star_dance_x;
    float star_dance_xtarg;
    float star_spin_cur;
    float star_spin_targ;
    int8_t star_step_dir;
    int8_t star_stick_side;
    uint8_t star_last_drum;
    uint32_t star_happy_until;
    uint32_t star_drum_until;
    uint32_t star_next_blink;
    uint32_t star_pulse_cnt;

    /* Host / Touch / Midi Test Diagnostics */
    int32_t last_vel;
    int32_t last_prs;
    int32_t last_turns5;
    uint8_t k3_turns;
    uint8_t held_chord_cnt[NKEYS];
    uint8_t held_notes[NKEYS][4];
} ui;
static void show_hud_badge(uint32_t now, const char *title, const char *sub, uint16_t accent, uint32_t ms)
{
    ui.hud_kind = HUD_BADGE;
    ui.hud_until = now + ms;
    text_copy(ui.hud_title, title, sizeof(ui.hud_title));
    text_copy(ui.hud_sub, sub, sizeof(ui.hud_sub));
    ui.hud_accent = accent;
    ui.redraw = 1u;
}

static void show_hud_param(uint32_t now, uint8_t knob, const char *lbl, const char *val, int32_t pct, uint16_t accent)
{
    ui.hud_kind = HUD_PARAM;
    ui.hud_until = now + 900u;
    ui.hud_knob = knob;
    text_copy(ui.hud_title, lbl, sizeof(ui.hud_title));
    text_copy(ui.hud_sub, val, sizeof(ui.hud_sub));
    ui.hud_val_pct = clamp(pct, 0, 100);
    ui.hud_accent = accent;
    ui.redraw = 1u;
}

static void show_hud_bank(uint32_t now, uint8_t bank)
{
    char bnum[12];
    ui.hud_kind = HUD_BANK;
    ui.hud_until = now + 1250u;
    ui.hud_code[0] = 'B'; ui.hud_code[1] = 'A'; ui.hud_code[2] = 'N'; ui.hud_code[3] = 'K';
    ui.hud_code[4] = ' '; ui.hud_code[5] = BANK_CHAR[bank % 10u]; ui.hud_code[6] = 0;
    if (bank == 9u) {
        int_text((int32_t)bankx_used_count(), bnum);
        text_copy(ui.hud_sub, "USER PRESETS ", sizeof(ui.hud_sub));
        text_cat(ui.hud_sub, bnum);
        text_cat(ui.hud_sub, "/128 SAVED");
    } else {
        text_copy(ui.hud_sub, BANK_SUBS[bank % 10u], sizeof(ui.hud_sub));
    }
    ui.hud_accent = COL_CYAN;
    ui.redraw = 1u;
}

static void show_hud_preset(uint32_t now)
{
    uint32_t blen = bank_len(set.bank);
    uint8_t prev_i = (uint8_t)((set.prog + blen - 1u) % blen);
    uint8_t next_i = (uint8_t)((set.prog + 1u) % blen);
    const sf_preset_t *pp = get_preset_ptr(set.bank, prev_i);
    const sf_preset_t *np = get_preset_ptr(set.bank, next_i);
    char bnum[8];
    uint32_t pv = (uint32_t)set.prog + 1u;
    ui.hud_kind = HUD_PRESET;
    ui.hud_until = now + 1050u;
    if (set.bank == 9u) slot3_text(set.prog, ui.hud_code);
    else {
        ui.hud_code[0] = BANK_CHAR[set.bank % 10u];
        ui.hud_code[1] = (char)('0' + (pv / 100u) % 10u);
        ui.hud_code[2] = (char)('0' + (pv / 10u) % 10u);
        ui.hud_code[3] = (char)('0' + pv % 10u);
        ui.hud_code[4] = 0;
    }
    text_copy(ui.hud_title, set.patch.name, sizeof(ui.hud_title));
    text_copy(ui.hud_prev, pp->name, sizeof(ui.hud_prev));
    text_copy(ui.hud_next, np->name, sizeof(ui.hud_next));
    text_copy(ui.hud_sub, CAT_NAMES[set.patch.cat % 10u], sizeof(ui.hud_sub));
    text_cat(ui.hud_sub, " | ");
    text_cat(ui.hud_sub, OSC_NAMES[sf_osc_mode(&set.patch) % SF_NOSC]);
    text_cat(ui.hud_sub, " | ");
    text_cat(ui.hud_sub, ROUTE_NAMES[sf_f_route(&set.patch) % 3u]);
    if (set.bank == 9u) {
        int_text((int32_t)bankx_used_count(), bnum);
        text_copy(ui.hud_code + 5, bnum, 6u);
    }
    ui.hud_accent = COL_CYAN;
    ui.redraw = 1u;
}

static void show_hud_drumkit(uint32_t now)
{
    uint8_t prev_k = (uint8_t)((set.drum_kit + SF_DRUM_KITS - 1u) % SF_DRUM_KITS);
    uint8_t next_k = (uint8_t)((set.drum_kit + 1u) % SF_DRUM_KITS);
    char bnum[8];
    ui.hud_kind = HUD_DRUMKIT;
    ui.hud_until = now + 1150u;
    text_copy(ui.hud_code, "KIT ", sizeof(ui.hud_code));
    int_text((int32_t)set.drum_kit + 1, bnum);
    text_cat(ui.hud_code, bnum);
    text_cat(ui.hud_code, "/10");
    text_copy(ui.hud_title, KIT_NAMES[set.drum_kit % SF_DRUM_KITS], sizeof(ui.hud_title));
    text_copy(ui.hud_sub, KIT_SUBS[set.drum_kit % SF_DRUM_KITS], sizeof(ui.hud_sub));
    text_copy(ui.hud_prev, KIT_NAMES[prev_k], sizeof(ui.hud_prev));
    text_copy(ui.hud_next, KIT_NAMES[next_k], sizeof(ui.hud_next));
    ui.hud_accent = COL_GREEN;
    ui.redraw = 1u;
}

static void release_all_keys(void)
{
    uint32_t k, i;
    for (k = 0; k < NKEYS; k++) {
        for (i = 0; i < ui.held_chord_cnt[k]; i++) {
            uint8_t nn = ui.held_notes[k][i];
            sf_dsp_note_off(nn);
            send_midi_note_off(nn);
        }
        ui.held_chord_cnt[k] = 0u;
        arp_held[k] = 0u;
    }
}

static void handle_octave_delta(uint32_t now, int32_t d)
{
    int32_t next_oct = clamp((int32_t)set.octave + d, -2, 2);
    char sbuf[12];
    if (next_oct != (int32_t)set.octave) {
        release_all_keys();
        set.octave = (int8_t)next_oct;
        set_dirty = 1u;
    }
    sbuf[0] = 'O'; sbuf[1] = 'C'; sbuf[2] = 'T'; sbuf[3] = ' ';
    if (set.octave >= 0) { sbuf[4] = '+'; sbuf[5] = (char)('0' + set.octave); sbuf[6] = 0; }
    else { sbuf[4] = '-'; sbuf[5] = (char)('0' - set.octave); sbuf[6] = 0; }
    show_hud_badge(now, sbuf, arp_active ? "ARP & KEYBED OCT" : "KEYBED TRANSPOSE", COL_AMBER, 800u);
}

static void execute_pending_nav(uint32_t now)
{
    if (pending_nav_dir == 1u) {
        uint32_t blen = bank_len(set.bank);
        uint8_t next_p = (uint8_t)(((int32_t)set.prog + pending_nav_delta + (int32_t)blen * 4) % (int32_t)blen);
        release_all_keys();
        load_preset(set.bank, next_p);
        show_hud_preset(now);
    } else if (pending_nav_dir == 2u) {
        uint8_t next_b = (uint8_t)(((int32_t)set.bank + pending_nav_delta + 10) % 10);
        uint32_t blen = bank_len(next_b);
        uint8_t tp = set.prog < blen ? set.prog : (uint8_t)(blen - 1u);
        release_all_keys();
        load_preset(next_b, tp);
        show_hud_bank(now, next_b);
    }
    pending_nav_dir = 0u;
}

static void open_confirm(uint32_t now, uint8_t type)
{
    uint8_t prev_page = ui.page;
    ui.conf_type = type;
    if (type == CONF_SAVE) {
        ui.page = PAGE_SAVE;
        ui.conf_dest_slot = 0u;
        ui.conf_bx_slot = (set.bank == 9u && !unsaved_random) ? set.prog : target_bankx_slot;
    } else if (type == CONF_RESET) {
        ui.page = PAGE_RESET;
        ui.conf_reset_init = 0u;
    } else if (type == CONF_RAND_LEAVE) {
        ui.page = PAGE_RAND_ASK;
        ui.conf_bx_slot = target_bankx_slot;
    } else if (type == CONF_CLEAR_LOOP) {
        ui.conf_clear_target = (prev_page == PAGE_SEQ) ? 1u : 0u;
        ui.page = PAGE_SAVE;
    }
    ui.last_menu_ms = now;
    ui.redraw = 1u;
}

static void execute_confirm_action(uint32_t now)
{
    uint8_t ctype = ui.conf_type;
    char sbuf[16];
    ui.conf_type = CONF_NONE;
    ui.page = PAGE_HOME;
    ui.sub[PAGE_HOME] = 0u;

    if (ctype == CONF_SAVE) {
        if (ui.conf_dest_slot && set.bank != 9u && !unsaved_random) {
            save_to_current_slot();
            text_copy(ui.save_banner, "PATCH SAVED", sizeof(ui.save_banner));
            show_hud_badge(now, "PATCH SAVED", set.patch.name, COL_GREEN, 1250u);
        } else {
            save_to_bankx_slot(ui.conf_bx_slot);
            slot3_text(set.prog, sbuf);
            text_copy(ui.save_banner, "SAVED TO BANK X", sizeof(ui.save_banner));
            show_hud_badge(now, "SAVED TO BANK X", sbuf, COL_GREEN, 1350u);
        }
        ui.save_flash_until = now + 1350u;
    } else if (ctype == CONF_RESET) {
        revert_to_factory(ui.conf_reset_init);
        if (ui.conf_reset_init) {
            text_copy(ui.save_banner, "STARFISH INIT", sizeof(ui.save_banner));
            show_hud_badge(now, "STARFISH INIT", "CLEAN TEMPLATE", COL_AMBER, 1300u);
        } else {
            text_copy(ui.save_banner, set.bank == 9u ? "SLOT CLEARED" : "FACTORY RESET", sizeof(ui.save_banner));
            show_hud_badge(now, ui.save_banner, set.patch.name, COL_CYAN, 1300u);
        }
        ui.save_flash_until = now + 1300u;
    } else if (ctype == CONF_RAND_LEAVE) {
        save_to_bankx_slot(ui.conf_bx_slot);
        slot3_text(ui.conf_bx_slot, sbuf);
        text_copy(ui.save_banner, "SAVED ", sizeof(ui.save_banner));
        text_cat(ui.save_banner, sbuf);
        ui.save_flash_until = now + 950u;
        execute_pending_nav(now);
    } else if (ctype == CONF_CLEAR_LOOP) {
        uint8_t tgt = ui.conf_clear_target % 3u;
        if (tgt == 0u || tgt == 2u) {
            uint32_t f = plat_lock();
            synth_loop_cnt = 0u;
            rec_armed = 0u;
            count_in_active = 0u;
            plat_unlock(f);
            stop_loop_playback();
        }
        if (tgt == 1u || tgt == 2u) {
            apply_drum_preset(4u);
            mark_drum_mix_dirty(now);
            ui.page = (tgt == 1u) ? PAGE_SEQ : PAGE_HOME;
        }
        if (tgt == 0u) show_hud_badge(now, "SYNTH CLEARED", "SYNTH LOOP EMPTY", COL_AMBER, 1050u);
        else if (tgt == 1u) show_hud_badge(now, "DRUMS CLEARED", "ALL PATTERNS EMPTY", COL_AMBER, 1050u);
        else show_hud_badge(now, "ALL CLEARED", "SYNTH & DRUMS EMPTY", COL_AMBER, 1050u);
    }
    ui.redraw = 1u;
}

static void fmt_hz_0_127(uint8_t v, char *out)
{
    float norm = (float)v * (1.0f / 127.0f);
    uint32_t hz = (uint32_t)clamp((int32_t)(40.0f * fm_exp2f(norm * 8.287712f) + 0.5f), 40, 12500);
    int_text((int32_t)hz, out);
    text_cat(out, "Hz");
}

static void fmt_sec_0_127(uint8_t v, uint32_t max_ms, char *out)
{
    uint32_t ms = 4u + ((uint32_t)v * (max_ms - 4u)) / 127u;
    if (ms < 1000u) {
        int_text((int32_t)ms, out);
        text_cat(out, "ms");
    } else {
        uint32_t d = ms / 100u;
        int_text((int32_t)(d / 10u), out);
        text_cat(out, ".");
        char dec[4] = {(char)('0' + d % 10u), 's', 0, 0};
        text_cat(out, dec);
    }
}

static void fmt_dec1(uint32_t tenths, const char *suf, char *out)
{
    char dec[6];
    int_text((int32_t)(tenths / 10u), out);
    dec[0] = '.';
    dec[1] = (char)('0' + tenths % 10u);
    dec[2] = 0;
    text_cat(out, dec);
    if (suf) text_cat(out, suf);
}

static void knob_label_val(uint8_t k, char lbl[16], char val[16], int32_t *pct)
{
    const sf_preset_t *p = &set.patch;
    uint8_t sub = ui.sub[ui.page];
    lbl[0] = 0; val[0] = 0; *pct = 0;

    if (ui.page == PAGE_FX) {
        if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "DELAY MIX", 16u); int_text(p->fx_del_mix, val); text_cat(val, "%"); *pct = p->fx_del_mix; }
            else if (k == 1u) { text_copy(lbl, "DELAY FB", 16u); int_text(p->fx_del_fb, val); text_cat(val, "%"); *pct = ((int32_t)p->fx_del_fb * 100) / 95; }
            else if (k == 2u) { text_copy(lbl, "PHASER RATE", 16u); fmt_dec1(p->fx_phaser_rate, "Hz", val); *pct = ((int32_t)p->fx_phaser_rate * 100) / 60; }
            else { uint8_t st = sf_sat_mode(p); text_copy(lbl, "SATURATION", 16u); text_copy(val, SAT_NAMES[st], 16u); *pct = (int32_t)st * 33; }
        } else {
            if (k == 0u) { text_copy(lbl, "CHORUS", 16u); int_text(p->fx_chorus, val); text_cat(val, "%"); *pct = p->fx_chorus; }
            else if (k == 1u) { text_copy(lbl, "PHASER", 16u); int_text(p->fx_phaser, val); text_cat(val, "%"); *pct = p->fx_phaser; }
            else if (k == 2u) {
                text_copy(lbl, "DELAY", 16u);
                if (p->fx_del_time > 100u) {
                    uint8_t si = (uint8_t)((p->fx_del_time - 101u) % 6u);
                    text_copy(val, DLY_SYNC_NAMES[si], 16u);
                    *pct = 75 + (int32_t)si * 5;
                } else {
                    fmt_dec1((uint32_t)p->fx_del_time / 10u, "s", val);
                    *pct = ((int32_t)p->fx_del_time * 75) / 100;
                }
            }
            else { text_copy(lbl, "REVERB", 16u); int_text(p->fx_reverb, val); text_cat(val, "%"); *pct = p->fx_reverb; }
        }
    } else if (ui.page == PAGE_ENV) {
        if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "FLT ATTACK", 16u); fmt_sec_0_127(p->f_atk, 2000u, val); *pct = ((int32_t)p->f_atk * 100) / 127; }
            else if (k == 1u) { text_copy(lbl, "FLT DECAY", 16u); fmt_sec_0_127(p->f_dec, 2500u, val); *pct = ((int32_t)p->f_dec * 100) / 127; }
            else if (k == 2u) { text_copy(lbl, "FLT SUSTAIN", 16u); int_text(((int32_t)p->f_sus * 100) / 127, val); text_cat(val, "%"); *pct = ((int32_t)p->f_sus * 100) / 127; }
            else { text_copy(lbl, "FLT ENV AMT", 16u); int_text(p->f1_env_s, val); text_cat(val, "%"); *pct = (p->f1_env_s + 100) / 2; }
        } else {
            if (k == 0u) { text_copy(lbl, "ATTACK", 16u); fmt_sec_0_127(p->a_atk, 2000u, val); *pct = ((int32_t)p->a_atk * 100) / 127; }
            else if (k == 1u) { text_copy(lbl, "DECAY", 16u); fmt_sec_0_127(p->a_dec, 3000u, val); *pct = ((int32_t)p->a_dec * 100) / 127; }
            else if (k == 2u) { text_copy(lbl, "SUSTAIN", 16u); int_text(((int32_t)p->a_sus * 100) / 127, val); text_cat(val, "%"); *pct = ((int32_t)p->a_sus * 100) / 127; }
            else { text_copy(lbl, "RELEASE", 16u); fmt_sec_0_127(p->a_rel, 3000u, val); *pct = ((int32_t)p->a_rel * 100) / 127; }
        }
    } else if (ui.page == PAGE_SEL) {
        if (sub == 2u) {
            if (k == 0u) { text_copy(lbl, "FLT BALANCE", 16u); int_text(p->f_bal, val); text_cat(val, "%"); *pct = p->f_bal; }
            else if (k == 1u) { text_copy(lbl, "FLT ENV AMT", 16u); int_text(p->f1_env_s, val); text_cat(val, "%"); *pct = (p->f1_env_s + 100) / 2; }
            else if (k == 2u) { text_copy(lbl, "FLT RELEASE", 16u); fmt_sec_0_127(p->f_rel, 2500u, val); *pct = ((int32_t)p->f_rel * 100) / 127; }
            else { text_copy(lbl, "KEYFOLLOW", 16u); int_text(p->f_keyfollow, val); text_cat(val, "%"); *pct = p->f_keyfollow; }
        } else if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "CUTOFF 1", 16u); fmt_hz_0_127(p->f1_cut, val); *pct = ((int32_t)p->f1_cut * 100) / 127; }
            else if (k == 1u) { text_copy(lbl, "RESO 1", 16u); fmt_dec1(5u + ((uint32_t)p->f1_res * 175u) / 127u, "", val); *pct = ((int32_t)p->f1_res * 100) / 127; }
            else if (k == 2u) { text_copy(lbl, "CUTOFF 2", 16u); fmt_hz_0_127(p->f2_cut, val); *pct = ((int32_t)p->f2_cut * 100) / 127; }
            else { text_copy(lbl, "RESO 2", 16u); fmt_dec1(5u + ((uint32_t)p->f2_res * 175u) / 127u, "", val); *pct = ((int32_t)p->f2_res * 100) / 127; }
        } else {
            uint8_t f1t = sf_f1_type(p), f2t = sf_f2_type(p), rt = sf_f_route(p);
            if (k == 0u) { text_copy(lbl, "FILTER MODE", 16u); text_copy(val, FLT_SHORT[f1t], 16u); text_cat(val, " "); text_cat(val, ROUTE_NAMES[rt % 3u]); *pct = (int32_t)f1t * 33; }
            else if (k == 1u) { text_copy(lbl, "ROUTING", 16u); text_copy(val, ROUTE_NAMES[rt % 3u], 16u); *pct = (int32_t)(rt % 3u) * 50; }
            else if (k == 2u) { text_copy(lbl, "FILTER 1", 16u); text_copy(val, FLT_NAMES[f1t], 16u); *pct = (int32_t)f1t * 33; }
            else { text_copy(lbl, "FILTER 2", 16u); text_copy(val, FLT_NAMES[f2t], 16u); *pct = (int32_t)f2t * 33; }
        }
    } else if (ui.page == PAGE_LFO) {
        if (sub == 2u) {
            if (k == 0u) { text_copy(lbl, "SWARM", 16u); int_text(p->swarm_depth, val); text_cat(val, "%"); *pct = p->swarm_depth; }
            else if (k == 1u) { text_copy(lbl, "SWARM RATE", 16u); fmt_dec1((uint32_t)p->swarm_rate / 2u, "Hz", val); *pct = ((int32_t)p->swarm_rate * 100) / 80; }
            else if (k == 2u) { text_copy(lbl, "GLIDE", 16u); int_text(p->glide, val); *pct = ((int32_t)p->glide * 100) / 85; }
            else { text_copy(lbl, "KEYFOLLOW", 16u); int_text(p->f_keyfollow, val); text_cat(val, "%"); *pct = p->f_keyfollow; }
        } else if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "RATE", 16u); fmt_dec1(1u + ((uint32_t)p->lfo2_rate * 199u) / 127u, "Hz", val); *pct = ((int32_t)p->lfo2_rate * 100) / 127; }
            else if (k == 1u) { text_copy(lbl, "DEPTH", 16u); int_text(p->lfo2_depth, val); text_cat(val, "%"); *pct = p->lfo2_depth; }
            else if (k == 2u) { uint8_t d = sf_lfo2_dest(p); text_copy(lbl, "DESTINATION", 16u); text_copy(val, LFO_NAMES[d % SF_NLFO_DEST], 16u); *pct = ((int32_t)(d % SF_NLFO_DEST) * 100) / 10; }
            else { uint8_t s = sf_lfo2_shape(p); text_copy(lbl, "SHAPE", 16u); text_copy(val, LFO_SHAPES[s], 16u); *pct = (int32_t)s * 33; }
        } else {
            if (k == 0u) { text_copy(lbl, "RATE", 16u); fmt_dec1(1u + ((uint32_t)p->lfo1_rate * 199u) / 127u, "Hz", val); *pct = ((int32_t)p->lfo1_rate * 100) / 127; }
            else if (k == 1u) { text_copy(lbl, "DEPTH", 16u); int_text(p->lfo1_depth, val); text_cat(val, "%"); *pct = p->lfo1_depth; }
            else if (k == 2u) { uint8_t d = sf_lfo1_dest(p); text_copy(lbl, "DESTINATION", 16u); text_copy(val, LFO_NAMES[d % SF_NLFO_DEST], 16u); *pct = ((int32_t)(d % SF_NLFO_DEST) * 100) / 10; }
            else { uint8_t s = sf_lfo1_shape(p); text_copy(lbl, "SHAPE", 16u); text_copy(val, LFO_SHAPES[s], 16u); *pct = (int32_t)s * 33; }
        }
    } else if (ui.page == PAGE_EDIT) {
        if (sub == 2u) {
            if (k == 0u) { uint8_t sy = sf_osc_sync(p); text_copy(lbl, "SYNC", 16u); text_copy(val, sy ? "ON" : "OFF", 16u); *pct = sy ? 100 : 0; }
            else if (k == 1u) { text_copy(lbl, "FM AMT", 16u); int_text(p->fm_amt, val); text_cat(val, "%"); *pct = p->fm_amt; }
            else if (k == 2u) { text_copy(lbl, "UNI PAN", 16u); int_text(p->uni_pan, val); text_cat(val, "%"); *pct = p->uni_pan; }
            else { text_copy(lbl, "DRIVE", 16u); int_text(p->fx_drive, val); text_cat(val, "%"); *pct = p->fx_drive; }
        } else if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "SUB LEVEL", 16u); int_text(p->sub_vol, val); text_cat(val, "%"); *pct = p->sub_vol; }
            else if (k == 1u) { uint8_t sw = sf_sub_wave(p) % 3u; text_copy(lbl, "SUB WAVE", 16u); text_copy(val, SUB_NAMES[sw], 16u); *pct = (int32_t)sw * 50; }
            else if (k == 2u) { text_copy(lbl, "NOISE", 16u); int_text(p->noise_vol, val); text_cat(val, "%"); *pct = p->noise_vol; }
            else { text_copy(lbl, "RING MOD", 16u); int_text(p->ring_vol, val); text_cat(val, "%"); *pct = p->ring_vol; }
        } else {
            uint8_t om = sf_osc_mode(p);
            if (k == 0u) { text_copy(lbl, "OSC BAL", 16u); int_text(p->osc_bal, val); text_cat(val, "%"); *pct = p->osc_bal; }
            else if (k == 1u) { text_copy(lbl, "OSC2 PITCH", 16u); if (p->osc2_semi >= 0) text_copy(val, "+", 16u); int_text(p->osc2_semi, val + (p->osc2_semi >= 0 ? 1 : 0)); text_cat(val, "st"); *pct = ((int32_t)p->osc2_semi + 24) * 100 / 48; }
            else if (k == 2u) {
                if (om == SF_OSC_PULSE || om == SF_OSC_WT) {
                    text_copy(lbl, om == SF_OSC_WT ? "WT POS" : "PULSE W", 16u);
                    int_text(((int32_t)p->pw * 100) / 127, val); text_cat(val, "%");
                    *pct = ((int32_t)p->pw * 100) / 127;
                } else {
                    text_copy(lbl, "SPREAD", 16u); int_text(p->spread, val); text_cat(val, "%"); *pct = p->spread;
                }
            } else {
                uint8_t u = clamp((int32_t)sf_unison(p), 1, 5);
                text_copy(lbl, "UNISON", 16u); int_text(u, val); text_cat(val, "V"); *pct = ((int32_t)u - 1) * 25;
            }
        }
    } else if (ui.page == PAGE_ARP) {
        if (sub == 1u) {
            if (k == 0u) { text_copy(lbl, "OCTAVES", 16u); int_text(set.arp_octs, val); text_cat(val, " OCT"); *pct = ((int32_t)set.arp_octs - 1) * 50; }
            else if (k == 1u) { text_copy(lbl, "GATE", 16u); int_text(set.arp_gate, val); text_cat(val, "%"); *pct = ((int32_t)set.arp_gate - 20) * 100 / 75; }
            else if (k == 2u) { text_copy(lbl, "SWING", 16u); int_text(set.swing, val); text_cat(val, "%"); *pct = ((int32_t)set.swing - 50) * 4; }
            else { text_copy(lbl, "TEMPO", 16u); int_text(set.bpm, val); text_cat(val, " BPM"); *pct = ((int32_t)set.bpm - 40) / 2; }
        } else {
            if (k == 0u) { text_copy(lbl, "ARP", 16u); text_copy(val, arp_active ? "ON" : "OFF", 16u); *pct = arp_active ? 100 : 0; }
            else if (k == 1u) { text_copy(lbl, "LATCH", 16u); text_copy(val, set.arp_latch ? "ON" : "OFF", 16u); *pct = set.arp_latch ? 100 : 0; }
            else if (k == 2u) { text_copy(lbl, "PATTERN", 16u); text_copy(val, ARP_MODE_NAMES[set.arp_mode % SF_NARP_MODE], 16u); *pct = ((int32_t)(set.arp_mode % SF_NARP_MODE) * 100) / (int32_t)(SF_NARP_MODE - 1u); }
            else { text_copy(lbl, "RATE", 16u); text_copy(val, ARP_RATE_NAMES[set.arp_rate % SF_NARP_RATE], 16u); *pct = ((int32_t)(set.arp_rate % SF_NARP_RATE) * 100) / (int32_t)(SF_NARP_RATE - 1u); }
        }
    } else if (ui.page == PAGE_SEQ) {
        uint8_t tr = ui.sel_drum % SF_DRUM_VOICES;
        uint32_t dlen = eff_drum_loop_len(set.seq_length);
        if (sub == 1u) {
            uint8_t ep = set.euclid_pulses[tr], er = set.euclid_rot[tr];
            char lbuf[8];
            if (k == 0u) { text_copy(lbl, "EUC HITS", 16u); int_text(ep, val); text_cat(val, "/"); int_text((int32_t)dlen, lbuf); text_cat(val, lbuf); *pct = ((int32_t)ep * 100) / (int32_t)dlen; }
            else if (k == 1u) { text_copy(lbl, "ROTATE", 16u); text_copy(val, "+", 16u); int_text(er, val + 1); *pct = dlen > 1u ? ((int32_t)er * 100) / ((int32_t)dlen - 1) : 0; }
            else if (k == 2u) { text_copy(lbl, "SWING", 16u); int_text(set.swing, val); text_cat(val, "%"); *pct = ((int32_t)set.swing - 50) * 4; }
            else { text_copy(lbl, "LENGTH", 16u); int_text((int32_t)dlen, val); text_cat(val, " ST"); *pct = ((int32_t)dlen - 1) * 100 / 63; }
        } else if (sub == 2u) {
            if (k == 0u) { text_copy(lbl, "DRM LVL", 16u); int_text(set.drum_vol, val); text_cat(val, "%"); *pct = set.drum_vol; }
            else if (k == 1u) { text_copy(lbl, "MASTER BPM", 16u); int_text(set.bpm, val); text_cat(val, " BPM"); *pct = ((int32_t)set.bpm - 40) / 2; }
            else if (k == 2u) { uint8_t bar = ui.sel_step / 16u + 1u; uint8_t maxb = (uint8_t)((dlen + 15u) / 16u); text_copy(lbl, "VIEW BAR", 16u); text_copy(val, "BAR ", 16u); int_text(bar, val + 4); *pct = maxb > 1u ? ((int32_t)bar - 1) * 100 / ((int32_t)maxb - 1) : 0; }
            else { text_copy(lbl, "DRM LOOP", 16u); text_copy(val, DRUM_LOOP_NAMES[set.drum_loop_bars % 5u], 16u); *pct = (int32_t)(set.drum_loop_bars % 5u) * 25; }
        } else {
            if (k == 0u) { text_copy(lbl, "TUNE", 16u); int8_t tn = set.drum_tune[tr]; if (tn >= 0) text_copy(val, "+", 16u); int_text(tn, val + (tn >= 0 ? 1 : 0)); text_cat(val, "st"); *pct = ((int32_t)tn + 12) * 100 / 24; }
            else if (k == 1u) { text_copy(lbl, "DECAY", 16u); int_text(set.drum_decay[tr], val); text_cat(val, "%"); *pct = ((int32_t)set.drum_decay[tr] - 25) * 100 / 175; }
            else if (k == 2u) { text_copy(lbl, "TONE", 16u); int_text(set.drum_tone[tr], val); text_cat(val, "%"); *pct = set.drum_tone[tr]; }
            else { text_copy(lbl, "LEVEL", 16u); int_text(set.drum_level[tr], val); text_cat(val, "%"); *pct = set.drum_level[tr]; }
        }
    } else if (ui.page == PAGE_GLO) {
        if (sub == 4u) {
            if (k == 0u) { text_copy(lbl, "SYNTH CH", 16u); if (set.midi_synth_ch == 0u) text_copy(val, "OMNI", 16u); else int_text(set.midi_synth_ch, val); *pct = ((int32_t)set.midi_synth_ch * 100) / 16; }
            else if (k == 1u) { text_copy(lbl, "DRUM CH", 16u); if (set.midi_drum_ch == 0u) text_copy(val, "OFF", 16u); else int_text(set.midi_drum_ch, val); *pct = ((int32_t)set.midi_drum_ch * 100) / 16; }
            else if (k == 2u) { text_copy(lbl, "BT MODE", 16u); text_copy(val, BT_MODE_NAMES[set.bt_mode % 4u], 16u); *pct = ((int32_t)(set.bt_mode % 4u) * 100) / 3; }
            else { text_copy(lbl, "AUDIO OUT", 16u); text_copy(val, OUT_MODE_NAMES[set.audio_out_mode % 3u], 16u); *pct = ((int32_t)(set.audio_out_mode % 3u) * 100) / 2; }
        } else if (sub == 3u) {
            if (k == 0u) { text_copy(lbl, "STYLE", 16u); text_copy(val, RAND_NAMES[set.rand_style % SF_NRAND_STYLE], 16u); *pct = ((int32_t)set.rand_style * 100) / 7; }
            else if (k == 1u) { text_copy(lbl, "RANDOMIZE", 16u); if (rand_count) { text_copy(val, "GEN #", 16u); int_text((int32_t)rand_count, val + 5); } else text_copy(val, "TURN!", 16u); *pct = (int32_t)(rand_count & 15u) * 6; }
            else if (k == 2u) { text_copy(lbl, "CHAOS", 16u); int_text(set.rand_chaos, val); text_cat(val, "%"); *pct = ((int32_t)set.rand_chaos - 20) * 100 / 80; }
            else { text_copy(lbl, "BANK X", 16u); slot3_text(target_bankx_slot, val); if (bankx_is_used(target_bankx_slot)) text_cat(val, "*"); *pct = ((int32_t)target_bankx_slot * 100) / 127; }
        } else if (sub == 2u) {
            if (k == 0u) { text_copy(lbl, "THEME", 16u); text_copy(val, PALETTE_NAMES[set.palette % 5u], 16u); *pct = ((int32_t)(set.palette % 5u) * 100) / 4; }
            else if (k == 1u) { text_copy(lbl, "STARFISH", 16u); text_copy(val, MASCOT_COLOR_NAMES[set.mascot_idx % 6u], 16u); *pct = ((int32_t)(set.mascot_idx % 6u) * 100) / 5; }
            else if (k == 2u) { text_copy(lbl, "GLIDE", 16u); int_text(p->glide, val); *pct = ((int32_t)p->glide * 100) / 85; }
            else { text_copy(lbl, "OCTAVE", 16u); if (set.octave >= 0) { val[0] = '+'; int_text(set.octave, val + 1); } else int_text(set.octave, val); *pct = (set.octave + 2) * 25; }
        } else if (sub == 1u) {
            uint8_t bars = (uint8_t)((set.seq_length + 15u) / 16u);
            if (k == 0u) { text_copy(lbl, "REC BARS", 16u); int_text(bars, val); text_cat(val, " BAR"); *pct = bars > 1u ? ((int32_t)bars - 1) * 100 / 15 : 0; }
            else if (k == 1u) { text_copy(lbl, "REC SYNC", 16u); text_copy(val, set.rec_quant ? "QUANT" : "FREE", 16u); *pct = set.rec_quant ? 100 : 0; }
            else if (k == 2u) { uint8_t pv = sf_polyphony(p); text_copy(lbl, "POLY", 16u); int_text(pv, val); text_cat(val, "V"); *pct = ((int32_t)pv - 1) * 100 / 7; }
            else { text_copy(lbl, "SWING", 16u); int_text(set.swing, val); text_cat(val, "%"); *pct = ((int32_t)set.swing - 50) * 4; }
        } else {
            if (k == 0u) { text_copy(lbl, "ROOT", 16u); text_copy(val, NOTE12[set.root % 12u], 16u); *pct = ((int32_t)set.root * 100) / 11; }
            else if (k == 1u) { text_copy(lbl, "SCALE", 16u); text_copy(val, SCALE_NAMES[set.scale % SF_NSCALE], 16u); *pct = ((int32_t)set.scale * 100) / 11; }
            else if (k == 2u) { text_copy(lbl, "TEMPO", 16u); int_text(set.bpm, val); text_cat(val, " BPM"); *pct = ((int32_t)set.bpm - 40) / 2; }
            else { text_copy(lbl, "CHORD", 16u); text_copy(val, CHORD_NAMES[set.chord_mode % SF_NCHORD], 16u); *pct = ((int32_t)set.chord_mode * 25); }
        }
    } else if (ui.page == PAGE_HOME && sub == 1u) {
        if (k == 0u) { text_copy(lbl, "SYNTH LVL", 16u); int_text(set.synth_vol, val); text_cat(val, "%"); *pct = set.synth_vol; }
        else if (k == 1u) { text_copy(lbl, "KICK DUCK", 16u); int_text(set.duck_amt, val); text_cat(val, "%"); *pct = set.duck_amt; }
        else if (k == 2u) { text_copy(lbl, "DRM LVL", 16u); int_text(set.drum_vol, val); text_cat(val, "%"); *pct = set.drum_vol; }
        else { text_copy(lbl, "DUCK SRC", 16u); text_copy(val, DRUM_NAMES[set.duck_src % SF_DRUM_VOICES], 16u); *pct = ((int32_t)(set.duck_src % SF_DRUM_VOICES) * 100) / 7; }
    } else {
        if (k == 0u) { text_copy(lbl, "CUTOFF 1", 16u); fmt_hz_0_127(p->f1_cut, val); *pct = ((int32_t)p->f1_cut * 100) / 127; }
        else if (k == 1u) { text_copy(lbl, "RESO 1", 16u); fmt_dec1(5u + ((uint32_t)p->f1_res * 175u) / 127u, "", val); *pct = ((int32_t)p->f1_res * 100) / 127; }
        else if (k == 2u) { text_copy(lbl, "CUTOFF 2", 16u); fmt_hz_0_127(p->f2_cut, val); *pct = ((int32_t)p->f2_cut * 100) / 127; }
        else { text_copy(lbl, "FLT BALANCE", 16u); int_text(p->f_bal, val); text_cat(val, "%"); *pct = p->f_bal; }
    }
}

static void handle_knob(uint32_t now, uint8_t k, int32_t d)
{
    sf_preset_t *p = &set.patch;
    uint8_t sub = ui.sub[ui.page];
    char lbl[16], val[16];
    int32_t pct = 0;

    if (k == 2u) ui.k3_turns = (uint8_t)(ui.k3_turns + d);

    if (ui.conf_type == CONF_SAVE || ui.conf_type == CONF_RAND_LEAVE) {
        ui.conf_bx_slot = (uint8_t)(((int32_t)ui.conf_bx_slot + d + (int32_t)SF_BANK_X_SLOTS * 4) % (int32_t)SF_BANK_X_SLOTS);
        if (ui.conf_type == CONF_SAVE) ui.conf_dest_slot = 0u;
        target_bankx_slot = ui.conf_bx_slot;
        ui.redraw = 1u;
        return;
    }
    if (ui.conf_type == CONF_CLEAR_LOOP) {
        ui.conf_clear_target = (uint8_t)(((int32_t)ui.conf_clear_target + d + 6) % 3);
        ui.redraw = 1u;
        return;
    }

    if (ui.page == PAGE_FX) {
        if (sub == 1u) {
            if (k == 0u) p->fx_del_mix = (uint8_t)clamp((int32_t)p->fx_del_mix + d * 4, 0, 100);
            else if (k == 1u) p->fx_del_fb = (uint8_t)clamp((int32_t)p->fx_del_fb + d * 4, 0, 95);
            else if (k == 2u) p->fx_phaser_rate = (uint8_t)clamp((int32_t)p->fx_phaser_rate + d * 2, 1, 60);
            else { uint8_t s = (uint8_t)((sf_sat_mode(p) + (d > 0 ? 1u : 3u)) & 3u); p->f2_type_route = (p->f2_type_route & 0x3Fu) | (s << 6); }
        } else {
            if (k == 0u) p->fx_chorus = (uint8_t)clamp((int32_t)p->fx_chorus + d * 4, 0, 100);
            else if (k == 1u) p->fx_phaser = (uint8_t)clamp((int32_t)p->fx_phaser + d * 4, 0, 100);
            else if (k == 2u) {
                int32_t cur = (int32_t)p->fx_del_time;
                p->fx_del_time = (uint8_t)((cur > 100 || (cur >= 98 && d > 0))
                    ? clamp(cur + (d > 0 ? 1 : -1), 5, 106)
                    : clamp(cur + d * 3, 5, 106));
            }
            else p->fx_reverb = (uint8_t)clamp((int32_t)p->fx_reverb + d * 4, 0, 100);
        }
    } else if (ui.page == PAGE_ENV) {
        ui.last_env_seg = (uint8_t)(k + 1u);
        ui.last_env_until = now + 1400u;
        if (sub == 1u) {
            if (k == 0u) p->f_atk = (uint8_t)clamp((int32_t)p->f_atk + d * 4, 0, 127);
            else if (k == 1u) p->f_dec = (uint8_t)clamp((int32_t)p->f_dec + d * 4, 0, 127);
            else if (k == 2u) p->f_sus = (uint8_t)clamp((int32_t)p->f_sus + d * 4, 0, 127);
            else p->f1_env_s = (int8_t)clamp((int32_t)p->f1_env_s + d * 5, -100, 100);
        } else {
            if (k == 0u) p->a_atk = (uint8_t)clamp((int32_t)p->a_atk + d * 4, 0, 127);
            else if (k == 1u) p->a_dec = (uint8_t)clamp((int32_t)p->a_dec + d * 4, 0, 127);
            else if (k == 2u) p->a_sus = (uint8_t)clamp((int32_t)p->a_sus + d * 4, 0, 127);
            else p->a_rel = (uint8_t)clamp((int32_t)p->a_rel + d * 4, 0, 127);
        }
    } else if (ui.page == PAGE_SEL) {
        if (sub == 2u) {
            if (k == 0u) p->f_bal = (uint8_t)clamp((int32_t)p->f_bal + d * 4, 0, 100);
            else if (k == 1u) p->f1_env_s = (int8_t)clamp((int32_t)p->f1_env_s + d * 5, -100, 100);
            else if (k == 2u) p->f_rel = (uint8_t)clamp((int32_t)p->f_rel + d * 4, 0, 127);
            else p->f_keyfollow = (uint8_t)clamp((int32_t)p->f_keyfollow + d * 4, 0, 100);
        } else if (sub == 1u) {
            if (k == 0u) p->f1_cut = (uint8_t)clamp((int32_t)p->f1_cut + d * 3, 0, 127);
            else if (k == 1u) p->f1_res = (uint8_t)clamp((int32_t)p->f1_res + d * 4, 0, 127);
            else if (k == 2u) p->f2_cut = (uint8_t)clamp((int32_t)p->f2_cut + d * 3, 0, 127);
            else p->f2_res = (uint8_t)clamp((int32_t)p->f2_res + d * 4, 0, 127);
        } else {
            if (k == 0u || k == 2u) { uint8_t ft = (uint8_t)((sf_f1_type(p) + (d > 0 ? 1u : 3u)) & 3u); p->f2_type_route = (p->f2_type_route & ~3u) | ft; }
            else if (k == 1u) { uint8_t rt = (uint8_t)(((int32_t)sf_f_route(p) + (d > 0 ? 1 : 2)) % 3); p->f2_type_route = (p->f2_type_route & ~0x30u) | (rt << 4); }
            else { uint8_t ft = (uint8_t)((sf_f2_type(p) + (d > 0 ? 1u : 3u)) & 3u); p->f2_type_route = (p->f2_type_route & ~0x0Cu) | (ft << 2); }
        }
    } else if (ui.page == PAGE_LFO) {
        if (sub == 2u) {
            if (k == 0u) p->swarm_depth = (uint8_t)clamp((int32_t)p->swarm_depth + d * 4, 0, 100);
            else if (k == 1u) p->swarm_rate = (uint8_t)clamp((int32_t)p->swarm_rate + d * 2, 1, 80);
            else if (k == 2u) p->glide = (uint8_t)clamp((int32_t)p->glide + d * 3, 0, 85);
            else p->f_keyfollow = (uint8_t)clamp((int32_t)p->f_keyfollow + d * 4, 0, 100);
        } else if (sub == 1u) {
            if (k == 0u) p->lfo2_rate = (uint8_t)clamp((int32_t)p->lfo2_rate + d * 3, 0, 127);
            else if (k == 1u) p->lfo2_depth = (uint8_t)clamp((int32_t)p->lfo2_depth + d * 4, 0, 100);
            else if (k == 2u) { uint8_t dd = (uint8_t)(((int32_t)sf_lfo2_dest(p) + d + (int32_t)SF_NLFO_DEST) % (int32_t)SF_NLFO_DEST); p->lfo2_meta = (p->lfo2_meta & ~0x0Fu) | dd; }
            else { uint8_t sh = (uint8_t)((sf_lfo2_shape(p) + (d > 0 ? 1u : 3u)) & 3u); p->lfo2_meta = (p->lfo2_meta & ~0x30u) | (sh << 4); }
        } else {
            if (k == 0u) p->lfo1_rate = (uint8_t)clamp((int32_t)p->lfo1_rate + d * 3, 0, 127);
            else if (k == 1u) p->lfo1_depth = (uint8_t)clamp((int32_t)p->lfo1_depth + d * 4, 0, 100);
            else if (k == 2u) { uint8_t dd = (uint8_t)(((int32_t)sf_lfo1_dest(p) + d + (int32_t)SF_NLFO_DEST) % (int32_t)SF_NLFO_DEST); p->lfo1_meta = (p->lfo1_meta & ~0x0Fu) | dd; }
            else { uint8_t sh = (uint8_t)((sf_lfo1_shape(p) + (d > 0 ? 1u : 3u)) & 3u); p->lfo1_meta = (p->lfo1_meta & ~0x30u) | (sh << 4); }
        }
    } else if (ui.page == PAGE_EDIT) {
        if (sub == 2u) {
            if (k == 0u) p->osc_meta ^= 0x08u;
            else if (k == 1u) p->fm_amt = (uint8_t)clamp((int32_t)p->fm_amt + d * 4, 0, 100);
            else if (k == 2u) p->uni_pan = (uint8_t)clamp((int32_t)p->uni_pan + d * 4, 0, 100);
            else p->fx_drive = (uint8_t)clamp((int32_t)p->fx_drive + d * 4, 0, 100);
        } else if (sub == 1u) {
            if (k == 0u) p->sub_vol = (uint8_t)clamp((int32_t)p->sub_vol + d * 4, 0, 100);
            else if (k == 1u) { uint8_t sw = (uint8_t)(((int32_t)sf_sub_wave(p) + d + 3) % 3); p->osc_meta = (p->osc_meta & ~0x30u) | (sw << 4); }
            else if (k == 2u) p->noise_vol = (uint8_t)clamp((int32_t)p->noise_vol + d * 4, 0, 100);
            else p->ring_vol = (uint8_t)clamp((int32_t)p->ring_vol + d * 4, 0, 100);
        } else {
            uint8_t om = sf_osc_mode(p);
            if (k == 0u) p->osc_bal = (uint8_t)clamp((int32_t)p->osc_bal + d * 4, 0, 100);
            else if (k == 1u) p->osc2_semi = (int8_t)clamp((int32_t)p->osc2_semi + d, -24, 24);
            else if (k == 2u) {
                if (om == SF_OSC_PULSE || om == SF_OSC_WT) p->pw = (uint8_t)clamp((int32_t)p->pw + d * 4, 6, 121);
                else p->spread = (uint8_t)clamp((int32_t)p->spread + d * 4, 0, 100);
            } else {
                uint8_t u = (uint8_t)clamp((int32_t)sf_unison(p) + (d > 0 ? 1 : -1), 1, 5);
                p->unison_poly = (p->unison_poly & ~7u) | (u - 1u);
            }
        }
    } else if (ui.page == PAGE_ARP) {
        if (sub == 1u) {
            if (k == 0u) set.arp_octs = (uint8_t)clamp((int32_t)set.arp_octs + (d > 0 ? 1 : -1), 1, 3);
            else if (k == 1u) set.arp_gate = (uint8_t)clamp((int32_t)set.arp_gate + d * 5, 20, 95);
            else if (k == 2u) set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75);
            else set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240);
        } else {
            if (k == 0u) { arp_active = arp_active ? 0u : 1u; if (!arp_active) set.arp_latch = 0u; }
            else if (k == 1u) set.arp_latch = set.arp_latch ? 0u : 1u;
            else if (k == 2u) set.arp_mode = (uint8_t)(((int32_t)set.arp_mode + d + (int32_t)SF_NARP_MODE * 2) % (int32_t)SF_NARP_MODE);
            else set.arp_rate = (uint8_t)(((int32_t)set.arp_rate + d + (int32_t)SF_NARP_RATE * 2) % (int32_t)SF_NARP_RATE);
        }
    } else if (ui.page == PAGE_SEQ) {
        uint8_t tr = ui.sel_drum % SF_DRUM_VOICES;
        uint32_t dlen = eff_drum_loop_len(set.seq_length);
        if (sub == 1u) {
            if (k == 0u) apply_euclidean(tr, (uint32_t)clamp((int32_t)set.euclid_pulses[tr] + (d > 0 ? 1 : -1), 0, (int32_t)dlen), set.euclid_rot[tr]);
            else if (k == 1u) apply_euclidean(tr, set.euclid_pulses[tr], (uint32_t)(((int32_t)set.euclid_rot[tr] + d + (int32_t)dlen) % (int32_t)dlen));
            else if (k == 2u) set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75);
            else {
                uint32_t old_dlen = dlen;
                if (set.drum_loop_bars >= 4u) {
                    set.seq_length = (uint16_t)clamp((int32_t)set.seq_length + d, 1, (int32_t)SF_SEQ_STEPS);
                } else {
                    set.drum_loop_bars = (uint8_t)clamp((int32_t)set.drum_loop_bars + (d > 0 ? 1 : -1), 0, 3);
                }
                uint32_t new_dlen = eff_drum_loop_len(set.seq_length);
                expand_drum_bars(old_dlen, new_dlen);
                if (ui.sel_step >= new_dlen) ui.sel_step = (uint8_t)(new_dlen - 1u);
            }
        } else if (sub == 2u) {
            if (k == 0u) set.drum_vol = (uint8_t)clamp((int32_t)set.drum_vol + d * 4, 0, 100);
            else if (k == 1u) set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240);
            else if (k == 2u) { uint8_t bars = (uint8_t)((dlen + 15u) / 16u); uint8_t nb = (uint8_t)(((int32_t)(ui.sel_step / 16u) + (d > 0 ? 1 : -1) + (int32_t)bars) % (int32_t)bars); ui.sel_step = (uint8_t)(nb * 16u); }
            else {
                uint32_t old_dlen = dlen;
                set.drum_loop_bars = (uint8_t)(((int32_t)set.drum_loop_bars + (d > 0 ? 1 : 4)) % 5);
                expand_drum_bars(old_dlen, eff_drum_loop_len(set.seq_length));
            }
        } else {
            if (k == 0u) set.drum_tune[tr] = (int8_t)clamp((int32_t)set.drum_tune[tr] + d, -12, 12);
            else if (k == 1u) set.drum_decay[tr] = (uint8_t)clamp((int32_t)set.drum_decay[tr] + d * 5, 25, 200);
            else if (k == 2u) set.drum_tone[tr] = (uint8_t)clamp((int32_t)set.drum_tone[tr] + d * 4, 0, 100);
            else set.drum_level[tr] = (uint8_t)clamp((int32_t)set.drum_level[tr] + d * 4, 0, 100);
        }
        mark_drum_mix_dirty(now);
    } else if (ui.page == PAGE_GLO) {
        if (sub == 4u) {
            if (k == 0u) set.midi_synth_ch = (uint8_t)(((int32_t)set.midi_synth_ch + d + 34) % 17);
            else if (k == 1u) set.midi_drum_ch = (uint8_t)(((int32_t)set.midi_drum_ch + d + 34) % 17);
            else if (k == 2u) {
                set.bt_mode = (uint8_t)(((int32_t)set.bt_mode + d + 8) % 4);
                set.ble_off = (set.bt_mode == 3u) ? 1u : 0u;
                plat_ble_enable(set.ble_off ? 0 : 1);
            } else {
                set.audio_out_mode = (uint8_t)(((int32_t)set.audio_out_mode + d + 6) % 3);
            }
        } else if (sub == 3u) {
            if (k == 0u) set.rand_style = (uint8_t)(((int32_t)set.rand_style + d + (int32_t)SF_NRAND_STYLE) % (int32_t)SF_NRAND_STYLE);
            else if (k == 1u) randomize_patch();
            else if (k == 2u) set.rand_chaos = (uint8_t)clamp((int32_t)set.rand_chaos + d * 5, 20, 100);
            else target_bankx_slot = (uint8_t)(((int32_t)target_bankx_slot + d + (int32_t)SF_BANK_X_SLOTS) % (int32_t)SF_BANK_X_SLOTS);
        } else if (sub == 2u) {
            if (k == 0u) { set.palette = (uint8_t)(((int32_t)set.palette + (d > 0 ? 1 : 4)) % 5); mark_drum_mix_dirty(now); }
            else if (k == 1u) { set.mascot_idx = (uint8_t)(((int32_t)set.mascot_idx + (d > 0 ? 1 : 5)) % 6); mark_drum_mix_dirty(now); }
            else if (k == 2u) p->glide = (uint8_t)clamp((int32_t)p->glide + d * 3, 0, 85);
            else handle_octave_delta(now, d > 0 ? 1 : -1);
        } else if (sub == 1u) {
            if (k == 0u) {
                uint32_t old_eff = eff_drum_loop_len(set.seq_length);
                uint8_t b = (uint8_t)clamp(((int32_t)set.seq_length + 15) / 16 + (d > 0 ? 1 : -1), 1, 16);
                set.seq_length = (uint16_t)(b * 16u);
                if (set.drum_loop_bars >= 4u) expand_drum_bars(old_eff, eff_drum_loop_len(set.seq_length));
                mark_drum_mix_dirty(now);
            }
            else if (k == 1u) { set.rec_quant = set.rec_quant ? 0u : 1u; mark_drum_mix_dirty(now); }
            else if (k == 2u) { uint8_t pv = (uint8_t)clamp((int32_t)sf_polyphony(p) + (d > 0 ? 1 : -1), 1, 8); p->unison_poly = (p->unison_poly & ~0x38u) | ((pv - 1u) << 3); }
            else { set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75); mark_drum_mix_dirty(now); }
        } else {
            if (k == 0u) set.root = (uint8_t)(((int32_t)set.root + d + 12) % 12);
            else if (k == 1u) set.scale = (uint8_t)(((int32_t)set.scale + d + (int32_t)SF_NSCALE) % (int32_t)SF_NSCALE);
            else if (k == 2u) { set.bpm = (uint16_t)clamp((int32_t)set.bpm + d * 2, 40, 240); mark_drum_mix_dirty(now); }
            else set.chord_mode = (uint8_t)(((int32_t)set.chord_mode + d + (int32_t)SF_NCHORD) % (int32_t)SF_NCHORD);
        }
    } else if (ui.page == PAGE_HOME && sub == 1u) {
        if (k == 0u) set.synth_vol = (uint8_t)clamp((int32_t)set.synth_vol + d * 4, 0, 100);
        else if (k == 1u) set.duck_amt = (uint8_t)clamp((int32_t)set.duck_amt + d * 4, 0, 100);
        else if (k == 2u) set.drum_vol = (uint8_t)clamp((int32_t)set.drum_vol + d * 4, 0, 100);
        else set.duck_src = (uint8_t)(((int32_t)set.duck_src + d + 16) % 8);
    } else {
        if (k == 0u) p->f1_cut = (uint8_t)clamp((int32_t)p->f1_cut + d * 3, 0, 127);
        else if (k == 1u) p->f1_res = (uint8_t)clamp((int32_t)p->f1_res + d * 4, 0, 127);
        else if (k == 2u) p->f2_cut = (uint8_t)clamp((int32_t)p->f2_cut + d * 3, 0, 127);
        else p->f_bal = (uint8_t)clamp((int32_t)p->f_bal + d * 4, 0, 100);
    }

    if ((ui.page == PAGE_HOME && sub == 0u) || ui.page == PAGE_FX || ui.page == PAGE_SEL ||
        ui.page == PAGE_ENV || ui.page == PAGE_LFO || ui.page == PAGE_EDIT ||
        (ui.page == PAGE_GLO && sub == 1u && k == 2u) ||
        (ui.page == PAGE_GLO && sub == 2u && k == 2u)) {
        set_dirty = 1u;
    } else if ((ui.page == PAGE_HOME && sub == 1u) || ui.page == PAGE_ARP || ui.page == PAGE_GLO) {
        mark_drum_mix_dirty(now);
    }
    sync_engine_cfg();
    knob_label_val(k, lbl, val, &pct);
    show_hud_param(now, (uint8_t)(k + 1u), lbl, val, pct, (k < 2u) ? theme_accent() : COL_AMBER);
}

static void cycle_page_btn(uint32_t now, uint8_t target, const char *t, const char *s, uint16_t acc)
{
    ui.mute_held = 0u;
    ui.mute_latched = 0u;
    if (ui.page != target) {
        ui.page = target;
        ui.sub[target] = 0u;
        show_hud_badge(now, t, s, acc, 800u);
    } else if ((uint8_t)(ui.sub[target] + 1u) < PAGE_SUBS[target]) {
        ui.sub[target]++;
        show_hud_badge(now, t, s, acc, 800u);
    } else {
        ui.page = PAGE_HOME;
        ui.sub[PAGE_HOME] = 0u;
        show_hud_badge(now, "PERFORMANCE", "MAIN HOME PAGE", COL_CYAN, 700u);
    }
    ui.redraw = 1u;
}

static void input(uint32_t now)
{
    uint32_t b = plat_buttons();
    uint32_t down = b & ~ui.prev_btn;
    uint32_t up = ~b & ui.prev_btn;
    uint32_t keys;
    uint32_t kdown, kup;
    int32_t d;
    uint32_t k, i;
    static const uint8_t BLK[11] = PLAT_BLACK_KEYS;
    static const uint8_t WHT[16] = PLAT_WHITE_KEYS;

#ifdef APP_TOUCH
    touch_poll();
    keys = touch_keys();
#else
    keys = plat_keys();
#endif
    kdown = keys & ~ui.prev_keys;
    kup = ~keys & ui.prev_keys;

    /* Any button, key, or encoder cancels boot splash cleanly */
    if (now < ui.boot_until && (down || kdown)) {
        ui.boot_until = now;
        sf_dsp_stop_jingle();
        ui.redraw = 1u;
    }

    if (down || kdown) { ui.last_menu_ms = now; ui.last_activity_ms = now; }

    /* 1. Encoders */
    if ((d = plat_enc(EN_SELECT)) != 0) {
        if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); }
        ui.last_menu_ms = now; ui.last_activity_ms = now;
        if (ui.conf_type == CONF_SAVE || ui.conf_type == CONF_RAND_LEAVE) {
            ui.conf_bx_slot = (uint8_t)(((int32_t)ui.conf_bx_slot + d + (int32_t)SF_BANK_X_SLOTS * 4) % (int32_t)SF_BANK_X_SLOTS);
            if (ui.conf_type == CONF_SAVE) ui.conf_dest_slot = 0u;
            target_bankx_slot = ui.conf_bx_slot;
            ui.redraw = 1u;
        } else if (ui.conf_type == CONF_CLEAR_LOOP) {
            ui.conf_clear_target = (uint8_t)(((int32_t)ui.conf_clear_target + d + 6) % 3);
            ui.redraw = 1u;
        } else if (ui.page == PAGE_BLE) {
            btdev_choose(btdev_pick() + d);
            ui.redraw = 1u;
        } else if (ui.page == PAGE_SEQ) {
            ui.sel_drum = (uint8_t)(((int32_t)ui.sel_drum + d + 16) % 8);
            sf_dsp_trigger_drum(ui.sel_drum, 108u);
            show_hud_param(now, 0u, "DRUM VOICE", DRUM_NAMES[ui.sel_drum], ((int32_t)ui.sel_drum * 100) / 7, COL_CYAN);
        } else if (unsaved_random) {
            pending_nav_dir = 1u; pending_nav_delta = (int8_t)d;
            open_confirm(now, CONF_RAND_LEAVE);
        } else {
            uint32_t blen = bank_len(set.bank);
            uint8_t next_p = (uint8_t)(((int32_t)set.prog + d + (int32_t)blen * 4) % (int32_t)blen);
            release_all_keys();
            load_preset(set.bank, next_p);
            show_hud_preset(now);
        }
    }

    if ((d = plat_enc(EN_PRESET)) != 0) {
        if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); }
        ui.last_menu_ms = now; ui.last_activity_ms = now;
        if (ui.conf_type == CONF_SAVE || ui.conf_type == CONF_RAND_LEAVE) {
            ui.conf_bx_slot = (uint8_t)(((int32_t)ui.conf_bx_slot + d + (int32_t)SF_BANK_X_SLOTS * 4) % (int32_t)SF_BANK_X_SLOTS);
            if (ui.conf_type == CONF_SAVE) ui.conf_dest_slot = 0u;
            target_bankx_slot = ui.conf_bx_slot;
            ui.redraw = 1u;
        } else if (ui.conf_type == CONF_CLEAR_LOOP) {
            ui.conf_clear_target = (uint8_t)(((int32_t)ui.conf_clear_target + d + 6) % 3);
            ui.redraw = 1u;
        } else if (ui.page == PAGE_SEQ) {
            set.drum_kit = (uint8_t)(((int32_t)set.drum_kit + d + (int32_t)SF_DRUM_KITS * 2) % (int32_t)SF_DRUM_KITS);
            mark_drum_mix_dirty(now);
            sync_engine_cfg();
            sf_dsp_trigger_drum(ui.sel_drum, 106u);
            show_hud_drumkit(now);
        } else if (unsaved_random) {
            pending_nav_dir = 2u; pending_nav_delta = (int8_t)d;
            open_confirm(now, CONF_RAND_LEAVE);
        } else {
            uint8_t next_b = (uint8_t)(((int32_t)set.bank + d + 20) % 10);
            uint32_t blen = bank_len(next_b);
            uint8_t tp = set.prog < blen ? set.prog : (uint8_t)(blen - 1u);
            release_all_keys();
            load_preset(next_b, tp);
            show_hud_bank(now, next_b);
        }
    }

    if ((d = plat_enc(EN_ALGO)) != 0) {
        if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); }
        ui.last_menu_ms = now; ui.last_activity_ms = now;
        if (ui.page == PAGE_SEQ) {
            set.drum_bus = (uint8_t)(((int32_t)set.drum_bus + d + (int32_t)SF_NBUS * 2) % (int32_t)SF_NBUS);
            mark_drum_mix_dirty(now);
            sync_engine_cfg();
            show_hud_badge(now, BUS_NAMES[set.drum_bus], "DRUM BUS DSP MODE", COL_AMBER, 1050u);
        } else {
            uint8_t cur_o = sf_osc_mode(&set.patch), oi = 0u, next_o;
            for (i = 0; i < SF_NOSC; i++) if (OSC_ORDER[i] == cur_o) { oi = (uint8_t)i; break; }
            oi = (uint8_t)(((int32_t)oi + d + (int32_t)SF_NOSC * 2) % (int32_t)SF_NOSC);
            next_o = OSC_ORDER[oi];
            set.patch.osc_meta = (set.patch.osc_meta & ~0x0Fu) | (next_o & 7u) | ((next_o == SF_OSC_SYNC ? 1u : 0u) << 3);
            set_dirty = 1u;
            sync_engine_cfg();
            show_hud_badge(now, OSC_NAMES[oi], "SYNTH / OSC ENGINE", COL_AMBER, 950u);
        }
    }

    if ((d = plat_enc(EN_K1)) != 0) { if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); } ui.last_menu_ms = now; ui.last_activity_ms = now; handle_knob(now, 0u, d); }
    if ((d = plat_enc(EN_K2)) != 0) { if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); } ui.last_menu_ms = now; ui.last_activity_ms = now; handle_knob(now, 1u, d); }
    if ((d = plat_enc(EN_K3)) != 0) { if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); } ui.last_menu_ms = now; ui.last_activity_ms = now; handle_knob(now, 2u, d); }
    if ((d = plat_enc(EN_K4)) != 0) { if (now < ui.boot_until) { ui.boot_until = now; sf_dsp_stop_jingle(); } ui.last_menu_ms = now; ui.last_activity_ms = now; handle_knob(now, 3u, d); }

    /* 2. Buttons while modal confirmation is active */
    if (ui.conf_type != CONF_NONE) {
        if ((down >> B_OCTUP) & 1u) {
            execute_confirm_action(now);
            ui.prev_btn = b; ui.prev_keys = keys; return;
        }
        if ((down >> B_OCTDN) & 1u) {
            ui.conf_type = CONF_NONE;
            ui.page = PAGE_HOME;
            show_hud_badge(now, "CANCELLED", "NO CHANGES MADE", COL_LABEL, 800u);
            ui.prev_btn = b; ui.prev_keys = keys; return;
        }
        if ((down >> B_SEL) & 1u) {
            if (ui.conf_type == CONF_RAND_LEAVE) {
                ui.conf_type = CONF_NONE;
                ui.page = PAGE_HOME;
                execute_pending_nav(now);
            } else if (ui.conf_type == CONF_RESET) {
                ui.conf_reset_init = ui.conf_reset_init ? 0u : 1u;
                ui.redraw = 1u;
            } else if (ui.conf_type == CONF_CLEAR_LOOP) {
                ui.conf_clear_target = (uint8_t)((ui.conf_clear_target + 1u) % 3u);
                ui.redraw = 1u;
            } else if (ui.conf_type == CONF_SAVE && set.bank != 9u && !unsaved_random) {
                ui.conf_dest_slot = ui.conf_dest_slot ? 0u : 1u;
                ui.redraw = 1u;
            }
            ui.prev_btn = b; ui.prev_keys = keys; return;
        }
        if ((down >> B_SAVE) & 1u && ui.conf_type == CONF_SAVE) {
            execute_confirm_action(now);
            ui.prev_btn = b; ui.prev_keys = keys; return;
        }
        if (down & ~((1u << B_SAVE) | (1u << B_REC))) {
            ui.conf_type = CONF_NONE;
            ui.page = PAGE_HOME;
            ui.redraw = 1u;
        }
    }

    /* 3. Combos: ARP+OCT (Latch), HOME+SEL (A/B Compare), GLO+ARP (Disco), OCT-+OCT+ (Cosmic), GLO+HOME (BLE) */
    if ((b >> B_ARP) & 1u) {
        if ((down >> B_OCTUP) & 1u || (down >> B_OCTDN) & 1u) {
            ui.arp_combo_used = 1u;
            set.arp_latch = set.arp_latch ? 0u : 1u;
            if (set.arp_latch) arp_active = 1u;
            sync_engine_cfg();
            show_hud_badge(now, set.arp_latch ? "LATCH ON" : "LATCH OFF", set.arp_latch ? "ARP NOTES HELD" : "FREEZE RELEASED", COL_AMBER, 1050u);
            down &= ~((1u << B_OCTUP) | (1u << B_OCTDN));
        }
    }
    if ((((b >> B_HOME) & 1u) && ((down >> B_SEL) & 1u)) || (((b >> B_SEL) & 1u) && ((down >> B_HOME) & 1u))) {
        sf_preset_t tmp;
        mem_copy(&tmp, &set.patch, (uint32_t)sizeof(sf_preset_t));
        mem_copy(&set.patch, &patch_baseline, (uint32_t)sizeof(sf_preset_t));
        mem_copy(&patch_baseline, &tmp, (uint32_t)sizeof(sf_preset_t));
        patch_compare_ab = patch_compare_ab ? 0u : 1u;
        set_dirty = patch_compare_ab ? 0u : 1u;
        sync_engine_cfg();
        show_hud_badge(now, patch_compare_ab ? "A/B: ORIGINAL" : "A/B: EDITED", set.patch.name, COL_AMBER, 1000u);
        down &= ~((1u << B_HOME) | (1u << B_SEL));
    }
    if ((((b >> B_GLO) & 1u) && ((down >> B_ARP) & 1u)) || (((b >> B_ARP) & 1u) && ((down >> B_GLO) & 1u))) {
        ui.disco_mode = ui.disco_mode ? 0u : 1u;
        ui.arp_combo_used = 1u;
        show_hud_badge(now, ui.disco_mode ? "DISCO STARFISH!" : "DISCO OFF", ui.disco_mode ? "PARTY PALETTE ON" : "BACK TO NORMAL", COL_PURPLE, 1100u);
        down &= ~((1u << B_GLO) | (1u << B_ARP));
    }
    if ((((b >> B_OCTDN) & 1u) && ((down >> B_OCTUP) & 1u)) || (((b >> B_OCTUP) & 1u) && ((down >> B_OCTDN) & 1u))) {
        static const uint8_t COSMIC[4] = {60u, 64u, 67u, 74u};
        uint32_t ci;
        for (ci = 0; ci < 4u; ci++) sf_dsp_note_on(COSMIC[ci], 108u);
        ui.star_spin_targ += FM_TWO_PI * 2.0f;
        ui.star_happy_until = now + 900u;
        show_hud_badge(now, "COSMIC STAR CHORD!", "C MAJ9 SPARKLE", COL_CYAN, 1100u);
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }

    if ((down >> B_FX) & 1u) cycle_page_btn(now, PAGE_FX, ui.page == PAGE_FX && ui.sub[PAGE_FX] == 0u ? "DLY & SAT (2/2)" : "FX RACK (1/2)", "", COL_CYAN);
    if ((down >> B_ENV) & 1u) cycle_page_btn(now, PAGE_ENV, ui.page == PAGE_ENV && ui.sub[PAGE_ENV] == 0u ? "FILTER ENV (2/2)" : "AMP ENV (1/2)", "", COL_AMBER);
    if ((down >> B_LFO) & 1u) {
        uint8_t nx = (ui.page != PAGE_LFO) ? 0u : (ui.sub[PAGE_LFO] + 1u);
        cycle_page_btn(now, PAGE_LFO, nx == 2u ? "SWARM/GLD (3/3)" : (nx == 1u ? "LFO 2 MATRIX (2/3)" : "LFO 1 MATRIX (1/3)"), "", COL_GREEN);
    }
    if ((down >> B_EDIT) & 1u) {
        uint8_t nx = (ui.page != PAGE_EDIT) ? 0u : (ui.sub[PAGE_EDIT] + 1u);
        cycle_page_btn(now, PAGE_EDIT, nx == 2u ? "SYNC & DRV (3/3)" : (nx == 1u ? "SUB & NOI (2/3)" : "OSC & UNI (1/3)"), "", theme_accent());
    }
    if ((down >> B_GLO) & 1u) {
        if ((b >> B_HOME) & 1u) { ui.page = PAGE_BLE; ui.redraw = 1u; }
        else {
            uint8_t nx = (ui.page != PAGE_GLO) ? 0u : (ui.sub[PAGE_GLO] + 1u);
            cycle_page_btn(now, PAGE_GLO,
                           nx == 4u ? "MIDI & BT I/O (5/5)" : (nx == 3u ? "RANDOM (4/5)" : (nx == 2u ? "COLOR & UI (3/5)" : (nx == 1u ? "REC & SYS (2/5)" : "SCALE/KEY (1/5)"))),
                           "", nx == 4u ? COL_CYAN : (nx == 2u ? theme_accent() : COL_AMBER));
        }
    }
    if ((down >> B_SEL) & 1u) {
        if (ui.page == PAGE_SEQ) {
            ui.sub[PAGE_SEQ] = (uint8_t)((ui.sub[PAGE_SEQ] + 1u) % 3u);
            show_hud_badge(now, ui.sub[PAGE_SEQ] == 0u ? "DRUM VOX (1/3)" : ui.sub[PAGE_SEQ] == 1u ? "EUCLID (2/3)" : "KIT GLOB (3/3)", "", COL_GREEN, 750u);
        } else {
            uint8_t nx = (ui.page != PAGE_SEL) ? 0u : (ui.sub[PAGE_SEL] + 1u);
            cycle_page_btn(now, PAGE_SEL, nx == 2u ? "FLT ENV (3/3)" : (nx == 1u ? "FLT TONE (2/3)" : "FLT MODE (1/3)"), "", COL_CYAN);
        }
    }
    if ((down >> B_HOME) & 1u) {
        ui.mute_held = 0u; ui.mute_latched = 0u;
        if ((b >> B_GLO) & 1u) {
            ui.page = PAGE_BLE; ui.redraw = 1u;
        } else if (ui.page == PAGE_HOME) {
            ui.sub[PAGE_HOME] = ui.sub[PAGE_HOME] ? 0u : 1u;
            show_hud_badge(now, ui.sub[PAGE_HOME] ? "MIX & DUCK (2/2)" : "PERFORMANCE (1/2)", "", ui.sub[PAGE_HOME] ? COL_AMBER : COL_CYAN, 700u);
        } else {
            ui.page = PAGE_HOME; ui.sub[PAGE_HOME] = 0u;
            show_hud_badge(now, "PERFORMANCE (1/2)", "", COL_CYAN, 700u);
        }
    }
    if ((down >> B_SEQ) & 1u) {
        ui.seq_hold_start = now;
        ui.seq_did_long = 0u;
        if (ui.page != PAGE_SEQ) {
            ui.seq_entered_on_down = 1u;
            ui.page = PAGE_SEQ; ui.sub[PAGE_SEQ] = 0u;
            show_hud_badge(now, "DRUM VOX (1/3)", "HOLD SEQ TO EXIT", COL_GREEN, 850u);
        } else {
            ui.seq_entered_on_down = 0u;
        }
    }
    if (((b >> B_SEQ) & 1u) && ui.seq_hold_start && !ui.seq_did_long) {
        if ((now - ui.seq_hold_start) >= 500u) {
            ui.seq_did_long = 1u;
            ui.seq_hold_start = 0u;
            ui.mute_held = 0u; ui.mute_latched = 0u;
            ui.page = PAGE_HOME; ui.sub[PAGE_HOME] = 0u;
            show_hud_badge(now, "SYNTH MODE", "EXITED DRUMS", COL_CYAN, 800u);
        }
    }
    if ((up >> B_SEQ) & 1u) {
        if (!ui.seq_did_long && !ui.seq_entered_on_down && ui.page == PAGE_SEQ) {
            if ((uint8_t)(ui.sub[PAGE_SEQ] + 1u) < PAGE_SUBS[PAGE_SEQ]) {
                ui.sub[PAGE_SEQ]++;
                show_hud_badge(now, ui.sub[PAGE_SEQ] == 1u ? "EUCLID (2/3)" : "KIT GLOB (3/3)", "", COL_GREEN, 800u);
            } else {
                ui.mute_held = 0u; ui.mute_latched = 0u;
                ui.page = PAGE_HOME; ui.sub[PAGE_HOME] = 0u;
                show_hud_badge(now, "SYNTH MODE", "", COL_CYAN, 750u);
            }
        }
        ui.seq_hold_start = 0u;
        ui.seq_entered_on_down = 0u;
    }
    if ((down >> B_PLAY) & 1u) {
        uint8_t was_playing;
        uint32_t f = plat_lock();
        was_playing = seq_playing;
        count_in_active = 0u;
        count_in_beat = 0u;
        if (seq_playing) {
            seq_playing = 0u;
        } else {
            seq_step = 0u; tick_acc_us = 0u; loop_cycle_us = 0u; seq_playing = 1u;
        }
        plat_unlock(f);
        if (was_playing) stop_loop_playback();
        ui.redraw = 1u;
    }
    if ((down >> B_OCTDN) & 1u) handle_octave_delta(now, -1);
    if ((down >> B_OCTUP) & 1u) handle_octave_delta(now, 1);

    /* Holdable buttons: ARP (short=toggle, 420ms=page), SAVE (short=confirm modal, 1500ms=factory reset), REC (short=arm, 1500ms=clear loop) */
    if ((down >> B_ARP) & 1u) {
        ui.arp_hold_start = now;
        ui.arp_did_long = 0u;
        ui.arp_combo_used = 0u;
    }
    if (((b >> B_ARP) & 1u) && ui.arp_hold_start && !ui.arp_did_long && !ui.arp_combo_used && (now - ui.arp_hold_start) >= 420u) {
        ui.arp_did_long = 1u;
        if (ui.page != PAGE_ARP) { ui.page = PAGE_ARP; ui.sub[PAGE_ARP] = 0u; }
        else ui.sub[PAGE_ARP] = ui.sub[PAGE_ARP] ? 0u : 1u;
        show_hud_badge(now, ui.sub[PAGE_ARP] ? "ARP GROOVE (2/2)" : "ARP SETUP (1/2)", "HOLD ARP: FLIP PAGE", COL_GREEN, 850u);
    }
    if ((up >> B_ARP) & 1u) {
        if (!ui.arp_combo_used && !ui.arp_did_long) {
            arp_active = arp_active ? 0u : 1u;
            if (!arp_active) set.arp_latch = 0u;
            sync_engine_cfg();
            show_hud_badge(now, arp_active ? "ARP ON" : "ARP OFF", arp_active ? "HOLD ARP:PG 2/2 | +OCT:LCH" : "ARPEGGIATOR STOPPED", arp_active ? COL_GREEN : COL_LABEL, 850u);
        }
        ui.arp_hold_start = 0u;
    }

    if ((down >> B_REC) & 1u) {
        ui.rec_hold_start = now;
        ui.rec_did_long = 0u;
    }
    if (((b >> B_REC) & 1u) && ui.rec_hold_start && !ui.rec_did_long) {
        if ((now - ui.rec_hold_start) >= 1500u) {
            ui.rec_did_long = 1u;
            ui.rec_hold_start = 0u;
            rec_armed = 0u;
            count_in_active = 0u;
            open_confirm(now, CONF_CLEAR_LOOP);
        } else {
            ui.redraw = 1u;
        }
    }
    if ((up >> B_REC) & 1u) {
        if (!ui.rec_did_long) {
            if (rec_armed) {
                rec_armed = 0u;
                count_in_active = 0u;
                count_in_beat = 0u;
                show_hud_badge(now, "REC OFF", "RECORDING STOPPED", COL_LABEL, 900u);
            } else {
                uint8_t has_drums = 0u, d;
                uint32_t s;
                for (d = 0u; d < SF_DRUM_VOICES && !has_drums; d++) {
                    for (s = 0u; s < SF_SEQ_STEPS; s++) {
                        if (set.seq_pat[d][s]) { has_drums = 1u; break; }
                    }
                }
                uint8_t has_content = (synth_loop_cnt > 0u || has_drums) ? 1u : 0u;
                rec_armed = 1u;
                if (!has_content && !seq_playing) {
                    uint32_t f = plat_lock();
                    count_in_beat = 1u;
                    count_in_acc_us = 0u;
                    count_in_active = 1u;
                    plat_unlock(f);
                    sf_dsp_click(1u);
                    show_hud_badge(now, "COUNT-IN 1/4", "START ON BAR 1", COL_AMBER, 1000u);
                } else {
                    if (!seq_playing) {
                        uint32_t f = plat_lock();
                        seq_step = 0u; tick_acc_us = 0u; loop_cycle_us = 0u; seq_playing = 1u;
                        plat_unlock(f);
                    }
                    show_hud_badge(now, ui.page == PAGE_SEQ ? "DRUM OVERDUB" : "SYNTH OVERDUB",
                                   "PLAY NOTES TO LAYER", COL_RED, 950u);
                }
            }
        }
        ui.rec_hold_start = 0u;
    }

    if ((down >> B_SAVE) & 1u) {
        if (ui.page == PAGE_BLE) {
            int ev = btdev_save();
            show_hud_badge(now, btdev_word(ev), btdev_name(), COL_CYAN, 1100u);
            ui.save_hold_start = 0u;
        } else {
            ui.save_hold_start = now;
            ui.save_did_long = 0u;
        }
    }
    if (((b >> B_SAVE) & 1u) && ui.save_hold_start && !ui.save_did_long) {
        if ((now - ui.save_hold_start) >= 1500u) {
            ui.save_did_long = 1u;
            ui.save_hold_start = 0u;
            open_confirm(now, CONF_RESET);
        } else {
            ui.redraw = 1u;
        }
    }
    if ((up >> B_SAVE) & 1u) {
        if (!ui.save_did_long && ui.page != PAGE_BLE) {
            open_confirm(now, CONF_SAVE);
        }
        ui.save_hold_start = 0u;
    }

    /* 4. Keybed presses & aftertouch */
    for (k = 0; k < NKEYS; k++) {
        uint8_t v = 100u;
        if ((kdown >> k) & 1u) {
#ifdef APP_TOUCH
            v = touch_sensed(k) ? touch_vel(k) : touch_vel_contact(k);
            if (!v) v = 100u;
#endif
            ui.last_vel = (int32_t)v;
            if (ui.page == PAGE_SEQ) {
                if (k == BLK[10]) {
                    if (ui.mute_latched) {
                        ui.mute_latched = 0u; ui.mute_held = 0u; ui.mute_used = 0u;
                        show_hud_badge(now, "MUTE OFF", "DRUM VOICE MODE", COL_LABEL, 650u);
                    } else {
                        ui.mute_held = 1u; ui.mute_used = 0u;
                        show_hud_badge(now, "MUTE ARMED", "TAP BLK 1-8 TO MUTE", COL_RED, 1100u);
                    }
                } else {
                    int bi = -1, wi = -1;
                    for (i = 0; i < 8u; i++) if (BLK[i] == k) { bi = (int)i; break; }
                    for (i = 0; i < 16u; i++) if (WHT[i] == k) { wi = (int)i; break; }
                    if (bi >= 0) {
                        if (ui.mute_held || ui.mute_latched) {
                            ui.mute_used = 1u;
                            set.drum_mute ^= (1u << (uint32_t)bi);
                            mark_drum_mix_dirty(now); sync_engine_cfg();
                            show_hud_badge(now, DRUM_NAMES[bi], ((set.drum_mute >> bi) & 1u) ? "MUTED" : "UNMUTED", ((set.drum_mute >> bi) & 1u) ? COL_RED : COL_GREEN, 900u);
                            ui.drum_clr_start = now; ui.drum_clr_track = (int8_t)bi; ui.drum_clr_done = 0u;
                        } else {
                            ui.sel_drum = (uint8_t)bi;
                            sf_dsp_trigger_drum((uint8_t)bi, v);
                            if (rec_armed && seq_playing) {
                                uint32_t dlen = eff_drum_loop_len(set.seq_length);
                                set.seq_pat[bi][seq_step % dlen] = (v > 115u) ? 2u : 1u;
                                mark_drum_mix_dirty(now); sync_engine_cfg();
                            }
                            ui.redraw = 1u;
                        }
                    } else if (wi >= 0) {
                        uint32_t dlen = eff_drum_loop_len(set.seq_length);
                        uint32_t play_dstep = (uint32_t)seq_step % dlen;
                        uint8_t abar = (seq_playing && !rec_armed) ? (uint8_t)(play_dstep / 16u) : (uint8_t)(ui.sel_step / 16u);
                        if ((uint32_t)abar * 16u >= dlen) abar = (uint8_t)((dlen - 1u) / 16u);
                        uint8_t abs_s = (uint8_t)(abar * 16u + (uint8_t)wi);
                        if ((uint32_t)abs_s >= dlen && abs_s < SF_SEQ_STEPS) {
                            uint8_t nb = (uint8_t)(abs_s / 16u);
                            if (set.drum_loop_bars < 4u && nb > set.drum_loop_bars) set.drum_loop_bars = nb;
                            if (abs_s >= set.seq_length) set.seq_length = (uint16_t)(abs_s + 1u);
                        }
                        set.seq_pat[ui.sel_drum][abs_s] = set.seq_pat[ui.sel_drum][abs_s] ? 0u : 1u;
                        ui.sel_step = abs_s;
                        mark_drum_mix_dirty(now); sync_engine_cfg();
                        if (set.seq_pat[ui.sel_drum][abs_s] && !seq_playing) sf_dsp_trigger_drum(ui.sel_drum, 102u);
                        ui.redraw = 1u;
                    }
                }
            } else {
                uint8_t q = quantize_note((int32_t)(53u + k) + (int32_t)set.octave * 12);
                arp_held[k] = 1u;
                if (set.arp_latch) arp_latch_buf[k] = 1u;
                arp_order_add((uint16_t)(0x0100u | k));
                if (!arp_active) {
                    int8_t ivs[4];
                    uint32_t cnt = chord_intervals(ivs);
                    if (rec_armed && !count_in_active) {
                        uint32_t base_step_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
                        uint32_t len = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
                        uint32_t f = plat_lock();
                        if (!seq_playing) {
                            seq_step = 0u;
                            tick_acc_us = 0u;
                            loop_cycle_us = 0u;
                            seq_playing = 1u;
                        }
                        {
                            uint32_t sw = set.swing < 50u ? 50u : (set.swing > 75u ? 75u : set.swing);
                            uint32_t step_us = ((seq_step & 1u) == 0u) ? (base_step_us * sw) / 50u : (base_step_us * (100u - sw)) / 50u;
                            uint32_t q_step = (tick_acc_us * 2u >= step_us) ? ((seq_step + 1u) % len) : seq_step;
                            uint32_t total_us = base_step_us * len;
                            uint32_t off_us = loop_cycle_us < total_us ? loop_cycle_us : (total_us > 1000u ? total_us - 1000u : 0u);
                            for (i = 0; i < cnt && synth_loop_cnt < SYNTH_LOOP_MAX; i++) {
                                uint8_t nn = (uint8_t)clamp((int32_t)q + ivs[i], 12, 119);
                                sf_loop_ev_t *ev = &synth_loop[synth_loop_cnt++];
                                ev->step = (uint16_t)q_step;
                                ev->note = nn;
                                ev->vel = v;
                                ev->len_steps = 2u;
                                ev->off_us = off_us;
                                ev->dur_us = base_step_us * 2u;
                                ev->holdoff_ms = 220u;
                                ev->rec_start_ms = rec_tick_ms;
                                ev->quant = set.rec_quant ? 1u : 0u;
                                ev->active_src = 1u;
                                ev->active_key = (uint8_t)k;
                            }
                        }
                        plat_unlock(f);
                    }
                    ui.held_chord_cnt[k] = (uint8_t)cnt;
                    for (i = 0; i < cnt; i++) {
                        uint8_t nn = (uint8_t)clamp((int32_t)q + ivs[i], 12, 119);
                        ui.held_notes[k][i] = nn;
                        sf_dsp_note_on(nn, v);
                        send_midi_note_on(nn, v);
                    }
                }
            }
        }
        if ((kup >> k) & 1u) {
            if (ui.page == PAGE_SEQ) {
                int bi = -1;
                for (i = 0; i < 8u; i++) if (BLK[i] == k) { bi = (int)i; break; }
                if (bi >= 0 && ui.drum_clr_track == bi) { ui.drum_clr_start = 0u; ui.drum_clr_track = -1; }
                if (k == BLK[10]) {
                    ui.drum_clr_start = 0u; ui.drum_clr_track = -1;
                    if (ui.mute_used) { ui.mute_held = 0u; ui.mute_latched = 0u; ui.mute_used = 0u; }
                    else if (ui.mute_held) { ui.mute_latched = 1u; show_hud_badge(now, "MUTE LATCHED", "", COL_RED, 1300u); }
                }
            } else {
                uint32_t base_step_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
                uint32_t len = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
                uint32_t max_dur = base_step_us * len;
                uint32_t f = plat_lock();
                for (i = 0; i < synth_loop_cnt; i++) {
                    if (synth_loop[i].active_src == 1u && synth_loop[i].active_key == (uint8_t)k) {
                        uint32_t held_ms = rec_tick_ms - synth_loop[i].rec_start_ms;
                        uint32_t dur_us = held_ms * 1000u;
                        uint32_t min_d = base_step_us / 2u;
                        if (dur_us < min_d) dur_us = min_d;
                        if (dur_us > max_dur) dur_us = max_dur;
                        synth_loop[i].dur_us = dur_us;
                        synth_loop[i].len_steps = (uint16_t)clamp((int32_t)((dur_us + base_step_us / 2u) / base_step_us), 1, (int32_t)len);
                        synth_loop[i].active_src = 0u;
                        synth_loop[i].active_key = 0xFFu;
                    }
                }
                plat_unlock(f);
                arp_held[k] = 0u;
                if (!set.arp_latch) arp_order_remove((uint16_t)(0x0100u | k));
                for (i = 0; i < ui.held_chord_cnt[k]; i++) {
                    uint8_t nn = ui.held_notes[k][i];
                    sf_dsp_note_off(nn);
                    send_midi_note_off(nn);
                }
                ui.held_chord_cnt[k] = 0u;
            }
        }
#ifdef APP_TOUCH
        if (((keys >> k) & 1u) && touch_sensed(k) && ui.page != PAGE_SEQ && !arp_active) {
            uint8_t prs = touch_pressure(k);
            if (prs > 0u) {
                ui.last_prs = (int32_t)prs;
                for (i = 0; i < ui.held_chord_cnt[k]; i++) sf_dsp_aftertouch(ui.held_notes[k][i], prs);
            }
        }
#endif
    }

    if (ui.page == PAGE_SEQ && ui.drum_clr_track >= 0 && ui.drum_clr_start && !ui.drum_clr_done) {
        if ((now - ui.drum_clr_start) >= 1500u) {
            uint8_t tr = (uint8_t)ui.drum_clr_track;
            ui.drum_clr_done = 1u; ui.drum_clr_start = 0u; ui.drum_clr_track = -1;
            for (i = 0; i < SF_SEQ_STEPS; i++) set.seq_pat[tr][i] = 0u;
            set.euclid_pulses[tr] = 0u; set.euclid_rot[tr] = 0u;
            mark_drum_mix_dirty(now); sync_engine_cfg();
            show_hud_badge(now, DRUM_NAMES[tr], "TRACK CLEARED!", COL_AMBER, 1150u);
        } else {
            ui.redraw = 1u;
        }
    }

    /* 12-second menu auto-return on non-HOME, non-SEQ pages */
    if (ui.page != PAGE_HOME && ui.page != PAGE_SEQ && ui.conf_type == CONF_NONE && !rec_armed) {
        if ((now - ui.last_menu_ms) >= 12000u) {
            ui.page = PAGE_HOME; ui.sub[PAGE_HOME] = 0u; ui.redraw = 1u;
        }
    }

    ui.prev_btn = b;
    ui.prev_keys = keys;
}
/* ---------------- Smooth 2D Vector Primitives & Starfish Mascot Renderers ---------------- */

static uint16_t blend565(uint16_t a, uint16_t b, uint32_t alpha255)
{
    uint32_t ia = 255u - alpha255;
    uint32_t ar = a >> 11, ag = (a >> 5) & 63u, ab = a & 31u;
    uint32_t br = b >> 11, bg = (b >> 5) & 63u, bb = b & 31u;
    uint32_t rr = (ar * ia + br * alpha255) / 255u;
    uint32_t rg = (ag * ia + bg * alpha255) / 255u;
    uint32_t rb = (ab * ia + bb * alpha255) / 255u;
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t dy;
    if (r <= 0) { cv_pset(cx, cy, c); return; }
    for (dy = -r; dy <= r; dy++) {
        int32_t y = cy + dy;
        int32_t sy = y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1) continue;
        int32_t dx = (int32_t)fm_sqrtf((float)(r * r - dy * dy) + 0.25f);
        cv_rect(cx - dx, y, dx * 2 + 1, 1, c);
    }
}

static void fill_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t dy;
    float iry2;
    if (rx <= 0 || ry <= 0) return;
    iry2 = 1.0f / (float)(ry * ry);
    for (dy = -ry; dy <= ry; dy++) {
        int32_t y = cy + dy;
        int32_t sy = y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1) continue;
        float frac = 1.0f - (float)(dy * dy) * iry2;
        int32_t dx = frac > 0.0f ? (int32_t)((float)rx * fm_sqrtf(frac) + 0.3f) : 0;
        cv_rect(cx - dx, y, dx * 2 + 1, 1, c);
    }
}

static void stroke_circle(int32_t cx, int32_t cy, int32_t r, int32_t thick, uint16_t c)
{
    int32_t dy, ro = r + thick / 2, ri = r - thick / 2;
    if (ri < 0) ri = 0;
    for (dy = -ro; dy <= ro; dy++) {
        int32_t y = cy + dy;
        int32_t sy = y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1) continue;
        int32_t xo = (int32_t)fm_sqrtf((float)(ro * ro - dy * dy) + 0.25f);
        if (dy < -ri || dy > ri) {
            cv_rect(cx - xo, y, xo * 2 + 1, 1, c);
        } else {
            int32_t xi = (int32_t)fm_sqrtf((float)(ri * ri - dy * dy));
            cv_rect(cx - xo, y, xo - xi + 1, 1, c);
            cv_rect(cx + xi, y, xo - xi + 1, 1, c);
        }
    }
}

static void fill_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t dy;
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 1) { cv_rect(x, y, w, h, c); return; }
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (dy = 0; dy < r; dy++) {
        int32_t d = r - 1 - dy;
        int32_t dx = (int32_t)fm_sqrtf((float)(r * r - d * d) + 0.25f);
        int32_t x0 = x + r - dx;
        int32_t span = w - 2 * (r - dx);
        cv_rect(x0, y + dy, span, 1, c);
        cv_rect(x0, y + h - 1 - dy, span, 1, c);
    }
}

static void draw_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x, y, w, t, c);
    cv_rect(x, y + h - t, w, t, c);
    cv_rect(x, y, t, h, c);
    cv_rect(x + w - t, y, t, h, c);
}

static void draw_thick_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint16_t c)
{
    if (r <= 0) { cv_line(x0, y0, x1, y1, c); return; }
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 600;
    while (guard--) {
        int32_t e2 = 2 * err;
        fill_circle(x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill_tri(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t c)
{
    int32_t t, y;
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y0 > y2) { t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; }
    if (y1 > y2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 == y2) return;
    for (y = y0; y <= y2; y++) {
        int32_t sy = y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1) continue;
        int32_t xa = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0);
        int32_t xb;
        if (y < y1) xb = (y1 == y0) ? x0 : x0 + ((x1 - x0) * (y - y0)) / (y1 - y0);
        else xb = (y2 == y1) ? x1 : x1 + ((x2 - x1) * (y - y1)) / (y2 - y1);
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        cv_rect(xa, y, xb - xa + 1, 1, c);
    }
}

enum {
    STAR_OPT_GLASSES = 1u << 0,
    STAR_OPT_HELMET  = 1u << 1,
    STAR_OPT_CAPTAIN = 1u << 2,
    STAR_OPT_SKATE   = 1u << 3,
    STAR_OPT_CHEF    = 1u << 4,
    STAR_OPT_DRUMMER = 1u << 5
};

static void draw_organic_star(float cx, float cy, float radius, float angle_rad,
                              float sx, float sy, uint8_t pal_idx,
                              uint32_t opts, uint8_t happy, uint8_t blinking,
                              uint8_t double_hit, int8_t stick_side, uint8_t drum_hit)
{
    /* Exact 10-cubic-Bezier silhouette from Fm1BasePanel.tsx drawMiniStarfish (60,60 center, R=50) */
    static const int8_t STAR_BEZ[10][6] = {
        {  7, -50,  10, -30,  15, -25},
        { 21, -19,  43, -24,  47, -17},
        { 51, -10,  33,   2,  31,   9},
        { 29,  17,  44,  34,  38,  40},
        { 31,  47,  15,  34,   7,  33},
        { -1,  32, -11,  47, -19,  44},
        {-27,  40, -20,  20, -23,  12},
        {-26,   5, -47,  -1, -46, -10},
        {-44, -18, -22, -17, -16, -23},
        {-10, -29,  -7, -50,   0, -50}
    };
    enum { NSEG = 10, STEPS = 5, NPTS = NSEG * STEPS };
    int16_t px[NPTS], py[NPTS];
    int32_t i, s, k, miny = 999, maxy = -999, y;
    float sc = radius * (1.0f / 50.0f);
    float ca = fm_cosf(angle_rad), sa = fm_sinf(angle_rad);
    const sf_star_pal_t *pal = &STAR_PALS[pal_idx % 6u];
    uint16_t outline_c = blend565(pal->body, C_WHITE, 115u);
    float p0x = 0.0f, p0y = -50.0f;

    k = 0;
    for (i = 0; i < NSEG; i++) {
        float c1x = (float)STAR_BEZ[i][0], c1y = (float)STAR_BEZ[i][1];
        float c2x = (float)STAR_BEZ[i][2], c2y = (float)STAR_BEZ[i][3];
        float p3x = (float)STAR_BEZ[i][4], p3y = (float)STAR_BEZ[i][5];
        for (s = 0; s < STEPS; s++) {
            float t = (float)s * (1.0f / (float)STEPS);
            float u = 1.0f - t;
            float u2 = u * u, t2 = t * t;
            float b0 = u2 * u;
            float b1 = 3.0f * u2 * t;
            float b2 = 3.0f * u * t2;
            float b3 = t2 * t;
            float bx = (b0 * p0x + b1 * c1x + b2 * c2x + b3 * p3x) * sc * sx;
            float by = (b0 * p0y + b1 * c1y + b2 * c2y + b3 * p3y) * sc * sy;
            float rx = bx * ca - by * sa;
            float ry = bx * sa + by * ca;
            px[k] = (int16_t)(cx + rx + 0.5f);
            py[k] = (int16_t)(cy + ry + 0.5f);
            if (py[k] < miny) miny = py[k];
            if (py[k] > maxy) maxy = py[k];
            k++;
        }
        p0x = p3x; p0y = p3y;
    }

    /* Clip scanline range strictly to the active framebuffer strip */
    int32_t vy0 = miny < (cv_y0 - cv_oy) ? (cv_y0 - cv_oy) : miny;
    int32_t vy1 = maxy >= (cv_y1 - cv_oy) ? (cv_y1 - cv_oy - 1) : maxy;
    for (y = vy0; y <= vy1; y++) {
        int16_t nodes[16];
        int32_t n = 0, j = NPTS - 1, c;
        for (i = 0; i < NPTS; i++) {
            int32_t yi = py[i], yj = py[j];
            if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
                if (n < 16)
                    nodes[n++] = (int16_t)(px[i] + ((y - yi) * (px[j] - px[i])) / (yj - yi));
            }
            j = i;
        }
        for (i = 0; i < n - 1; i++) {
            for (c = i + 1; c < n; c++) {
                if (nodes[c] < nodes[i]) { int16_t tmp = nodes[i]; nodes[i] = nodes[c]; nodes[c] = tmp; }
            }
        }
        for (i = 0; i + 1 < n; i += 2) {
            int32_t x0 = nodes[i], x1 = nodes[i + 1];
            if (x1 >= x0) cv_rect(x0, y, x1 - x0 + 1, 1, pal->body);
        }
    }

    /* Soft highlight rim along organic Bezier perimeter */
    for (i = 0; i < NPTS; i++) {
        int32_t ni = (i + 1) % NPTS;
        cv_line(px[i], py[i], px[ni], py[ni], outline_c);
    }

    #define SX_PT(u, v) ((int32_t)(cx + ((u) * radius * sx) * ca - ((v) * radius * sy) * sa + 0.5f))
    #define SY_PT(u, v) ((int32_t)(cy + ((u) * radius * sx) * sa + ((v) * radius * sy) * ca + 0.5f))

    /* Belly highlight (60,63,r=19 in 50-unit norm) & rosy cheeks (+-14/50, +7/50) */
    fill_circle(SX_PT(0.0f, 0.06f), SY_PT(0.0f, 0.06f), (int32_t)(radius * 0.38f), blend565(pal->body, pal->belly, 115u));
    fill_ellipse(SX_PT(-0.28f, 0.14f), SY_PT(-0.28f, 0.14f), (int32_t)(radius * 0.09f) + 1, (int32_t)(radius * 0.056f) + 1, pal->cheek);
    fill_ellipse(SX_PT(0.28f, 0.14f), SY_PT(0.28f, 0.14f), (int32_t)(radius * 0.09f) + 1, (int32_t)(radius * 0.056f) + 1, pal->cheek);

    /* Eyes or Sunglasses (matches 51.5,57.5 & 68.5,57.5 in 50-unit norm -> +-0.17, -0.05) */
    if (opts & STAR_OPT_GLASSES) {
        int32_t lx = SX_PT(-0.18f, -0.05f), ly = SY_PT(-0.18f, -0.05f);
        int32_t rx = SX_PT(0.18f, -0.05f), ry = SY_PT(0.18f, -0.05f);
        int32_t gw = (int32_t)(radius * 0.24f), gh = (int32_t)(radius * 0.16f);
        fill_round_rect(lx - gw / 2, ly - gh / 2, gw, gh, 2, RGB(11, 17, 28));
        fill_round_rect(rx - gw / 2, ry - gh / 2, gw, gh, 2, RGB(11, 17, 28));
        draw_rect_outline(lx - gw / 2, ly - gh / 2, gw, gh, 1, C_WHITE);
        draw_rect_outline(rx - gw / 2, ry - gh / 2, gw, gh, 1, C_WHITE);
        cv_line(lx + gw / 2, ly, rx - gw / 2, ry, C_WHITE);
        cv_line(lx - gw / 4, ly + 1, lx + 1, ly - gh / 3, C_WHITE);
        cv_line(rx - gw / 4, ry + 1, rx + 1, ry - gh / 3, C_WHITE);
    } else if (happy) {
        int32_t ex1 = SX_PT(-0.17f, -0.05f), ey1 = SY_PT(-0.17f, -0.05f);
        int32_t ex2 = SX_PT(0.17f, -0.05f), ey2 = SY_PT(0.17f, -0.05f);
        int32_t d = (int32_t)(radius * 0.09f) + 1;
        cv_line(ex1 - d, ey1 + 1, ex1, ey1 - d / 2 - 1, RGB(15, 19, 28));
        cv_line(ex1, ey1 - d / 2 - 1, ex1 + d, ey1 + 1, RGB(15, 19, 28));
        cv_line(ex2 - d, ey2 + 1, ex2, ey2 - d / 2 - 1, RGB(15, 19, 28));
        cv_line(ex2, ey2 - d / 2 - 1, ex2 + d, ey2 + 1, RGB(15, 19, 28));
    } else if (blinking) {
        int32_t ex1 = SX_PT(-0.17f, -0.04f), ey1 = SY_PT(-0.17f, -0.04f);
        int32_t ex2 = SX_PT(0.17f, -0.04f), ey2 = SY_PT(0.17f, -0.04f);
        int32_t d = (int32_t)(radius * 0.09f) + 1;
        cv_line(ex1 - d, ey1, ex1 + d, ey1, RGB(15, 19, 28));
        cv_line(ex2 - d, ey2, ex2 + d, ey2, RGB(15, 19, 28));
    } else {
        int32_t er = radius >= 18.0f ? (int32_t)(radius * 0.088f) : 1;
        int32_t ex1 = SX_PT(-0.17f, -0.05f), ey1 = SY_PT(-0.17f, -0.05f);
        int32_t ex2 = SX_PT(0.17f, -0.05f), ey2 = SY_PT(0.17f, -0.05f);
        fill_circle(ex1, ey1, er, RGB(15, 19, 28));
        fill_circle(ex2, ey2, er, RGB(15, 19, 28));
        cv_pset(ex1 + 1, ey1 - 1, C_WHITE);
        cv_pset(ex2 + 1, ey2 - 1, C_WHITE);
    }

    /* Cute smile (54,66 -> 60,72/75 -> 66,66 in 50-unit norm) */
    {
        int32_t mx = SX_PT(0.0f, 0.12f), my = SY_PT(0.0f, 0.12f);
        int32_t mw = (int32_t)(radius * 0.12f) + 1;
        if (happy || radius >= 18.0f) {
            fill_tri(mx - mw, my, mx + mw, my, mx, my + mw / 2 + 1, RGB(15, 19, 28));
        } else {
            cv_line(mx - mw, my, mx, my + 1, RGB(15, 19, 28));
            cv_line(mx, my + 1, mx + mw, my, RGB(15, 19, 28));
        }
    }

    /* Scene Headgear & Drummer Boy Props */
    if (opts & STAR_OPT_HELMET) {
        int32_t hr = (int32_t)(radius * 0.65f);
        stroke_circle((int32_t)cx, (int32_t)cy, hr, 2, RGB(103, 232, 249));
        cv_line((int32_t)cx - hr / 2, (int32_t)cy - hr / 2, (int32_t)cx - hr / 4, (int32_t)cy - hr * 2 / 3, C_WHITE);
    }
    if (opts & STAR_OPT_CAPTAIN) {
        int32_t hx = SX_PT(0.0f, -0.58f), hy = SY_PT(0.0f, -0.58f);
        fill_round_rect(hx - 11, hy - 5, 22, 8, 3, C_WHITE);
        cv_rect(hx - 11, hy + 1, 22, 3, RGB(15, 23, 42));
        fill_circle(hx, hy - 1, 2, COL_AMBER);
    }
    if (opts & STAR_OPT_SKATE) {
        int32_t hx = SX_PT(0.0f, -0.52f), hy = SY_PT(0.0f, -0.52f);
        fill_ellipse(hx, hy, 10, 6, RGB(245, 158, 11));
        fill_round_rect(hx - 13, hy + 1, 14, 4, 2, RGB(14, 165, 233));
    }
    if (opts & STAR_OPT_CHEF) {
        int32_t hx = SX_PT(0.0f, -0.62f), hy = SY_PT(0.0f, -0.62f);
        fill_circle(hx - 6, hy - 4, 6, C_WHITE);
        fill_circle(hx, hy - 7, 7, C_WHITE);
        fill_circle(hx + 6, hy - 4, 6, C_WHITE);
        cv_rect(hx - 9, hy - 2, 18, 6, C_WHITE);
        cv_rect(hx - 9, hy + 2, 18, 2, RGB(203, 213, 225));
    }
    if (opts & STAR_OPT_DRUMMER) {
        int32_t hx = SX_PT(0.0f, -0.60f), hy = SY_PT(0.0f, -0.60f);
        fill_ellipse(hx, hy, 5, 3, RGB(14, 165, 233));
        fill_round_rect(hx, hy, 7, 2, 1, COL_AMBER);
        {
            uint8_t l_dn = drum_hit && (double_hit || stick_side == 1);
            uint8_t r_dn = drum_hit && (double_hit || stick_side == -1);
            int32_t lx0 = (int32_t)cx - 9, ly0 = (int32_t)cy;
            int32_t lx1 = (int32_t)cx - (l_dn ? 3 : 8), ly1 = (int32_t)cy + (l_dn ? 11 : 6);
            int32_t rx0 = (int32_t)cx + 9, ry0 = (int32_t)cy;
            int32_t rx1 = (int32_t)cx + (r_dn ? 3 : 8), ry1 = (int32_t)cy + (r_dn ? 11 : 6);
            cv_line(lx0, ly0, lx1, ly1, RGB(253, 230, 138));
            fill_circle(lx1, ly1, 1, C_WHITE);
            cv_line(rx0, ry0, rx1, ry1, RGB(253, 230, 138));
            fill_circle(rx1, ry1, 1, C_WHITE);
        }
    }
    #undef SX_PT
    #undef SY_PT
}

static void draw_boot_splash(uint32_t now)
{
    uint32_t el = now - ui.boot_start;
    float t = (float)el * 0.001f;
    uint32_t prog_w = (el * 200u) / BOOT_SPLASH_MS;
    uint8_t act = ui.boot_act % 6u;
    uint8_t jidx = ui.boot_jingle % 6u;
    char foot2[36];
    int32_t i;

    if (prog_w > 200u) prog_w = 200u;

    /* 1. Animated 6-Scene Backdrop (y: 0..185) */
    if (act == 0u) {
        /* BEACH CHILLIN' */
        cv_rect(0, 0, 240, 48, RGB(14, 23, 42));
        cv_rect(0, 48, 240, 48, RGB(30, 41, 59));
        fill_circle(185, 78, 22, RGB(251, 146, 60));
        cv_rect(0, 96, 240, 42, RGB(2, 132, 199));
        cv_rect(24, 108, 48, 2, RGB(125, 211, 252));
        cv_rect(140, 118, 62, 2, RGB(125, 211, 252));
        fill_ellipse(120, 178, 150, 44, RGB(253, 230, 138));
        draw_thick_line(64, 152, 54, 174, 1, RGB(146, 64, 14));
        draw_thick_line(154, 152, 164, 174, 1, RGB(146, 64, 14));
        fill_round_rect(60, 128, 92, 14, 4, RGB(244, 63, 94));
        cv_rect(72, 128, 10, 14, C_WHITE);
        cv_rect(98, 128, 10, 14, C_WHITE);
        cv_rect(124, 128, 10, 14, C_WHITE);
        float bob = fm_sinf(t * 3.2f) * 2.5f;
        draw_organic_star(102.0f, 107.0f + bob, 27.0f, -0.32f, 1.0f, 1.0f, 0u, STAR_OPT_GLASSES, 1u, 0u, 0u, 1, 0u);
        fill_round_rect(144, 110, 17, 22, 3, COL_CYAN);
        cv_rect(145, 112, 15, 5, COL_AMBER);
        draw_thick_line(149, 112, 144, 98, 1, RGB(253, 224, 71));
        draw_thick_line(144, 98, 126, (int32_t)(98.0f + bob * 0.5f), 1, RGB(253, 224, 71));
        fill_tri(154, 102, 168, 106, 159, 94, RGB(236, 72, 153));
    } else if (act == 1u) {
        /* MOON EXPLORER */
        cv_rect(0, 0, 240, 186, RGB(5, 8, 20));
        for (i = 0; i < 18; i++) {
            int32_t sx = ((i * 53 + 17) % 220) + 10;
            int32_t sy = ((i * 37 + 11) % 112) + 8;
            cv_rect(sx, sy, (i % 3 == 0) ? 2 : 1, (i % 3 == 0) ? 2 : 1, RGB(226, 232, 240));
        }
        fill_circle(44, 42, 16, RGB(14, 165, 233));
        fill_circle(40, 39, 6, COL_GREEN);
        fill_circle(50, 47, 4, COL_GREEN);
        fill_ellipse(120, 198, 160, 62, RGB(71, 85, 105));
        fill_ellipse(66, 158, 18, 6, RGB(51, 65, 85));
        fill_ellipse(174, 164, 14, 5, RGB(51, 65, 85));
        int32_t fw = (int32_t)(fm_sinf(t * 5.2f) * 3.0f);
        draw_thick_line(156, 152, 156, 72, 1, RGB(226, 232, 240));
        cv_rect(157, 74 + fw / 2, 46, 22, COL_CYAN);
        cv_text(163, 78 + fw / 2, &FONT_S, "FM-1", COL_BG);
        draw_organic_star(94.0f, 114.0f + fm_sinf(t * 2.4f) * 5.0f, 29.0f, fm_sinf(t * 1.8f) * 0.14f, 1.0f, 1.0f, 1u, STAR_OPT_HELMET, 1u, 0u, 0u, 1, 0u);
    } else if (act == 2u) {
        /* SUB CAPTAIN */
        cv_rect(0, 0, 240, 93, RGB(4, 28, 50));
        cv_rect(0, 93, 240, 93, RGB(8, 47, 73));
        for (i = 0; i < 8; i++) {
            int32_t bx = 24 + i * 26;
            int32_t by = (176 - (int32_t)(t * (float)(32 + (i % 3) * 14)) - i * 29) % 175;
            if (by < 0) by += 175;
            stroke_circle(bx, by, 3 + (i % 3), 1, RGB(103, 232, 249));
        }
        fill_round_rect(36, 48, 168, 108, 48, RGB(245, 158, 11));
        fill_circle(120, 104, 42, RGB(15, 23, 42));
        stroke_circle(120, 104, 42, 6, RGB(253, 230, 138));
        draw_organic_star(120.0f, 105.0f + fm_sinf(t * 3.4f) * 2.5f, 26.0f, fm_sinf(t * 2.2f) * 0.12f, 1.0f, 1.0f, 2u, STAR_OPT_CAPTAIN, 1u, 0u, 0u, 1, 0u);
    } else if (act == 3u) {
        /* CAMPFIRE JAM */
        cv_rect(0, 0, 240, 146, RGB(7, 11, 25));
        cv_rect(0, 146, 240, 40, RGB(30, 41, 59));
        for (i = 0; i < 14; i++) cv_rect(((i * 47 + 19) % 210) + 14, ((i * 23 + 9) % 80) + 8, 2, 2, RGB(226, 232, 240));
        int32_t fl = (int32_t)(fm_sinf(t * 14.0f) * 4.0f);
        cv_rect(142, 146, 38, 7, RGB(120, 53, 15));
        cv_rect(146, 142, 30, 6, RGB(120, 53, 15));
        fill_tri(148, 144, 174, 144, 161, 102 + fl, COL_ORANGE);
        fill_tri(153, 144, 169, 144, 161, 118 - fl, COL_AMBER);
        draw_organic_star(82.0f, 116.0f + fm_sinf(t * 3.2f) * 2.0f, 27.0f, fm_sinf(t * 6.0f) * 0.08f, 1.0f, 1.0f, 3u, 0u, 1u, 0u, 0u, 1, 0u);
        fill_ellipse(92, 126, 14, 9, RGB(217, 119, 6));
        fill_circle(90, 126, 4, RGB(69, 26, 3));
        cv_rect(102, 123, 20, 4, RGB(146, 64, 14));
        {
            int32_t nr = ((int32_t)(t * 24.0f)) % 40;
            cv_text(114, 72 - nr / 2, &FONT_B, "#", COL_CYAN);
            cv_text(134, 82 - ((nr + 18) % 40) / 2, &FONT_B, "*", COL_AMBER);
        }
    } else if (act == 4u) {
        /* ROCKET SKATER */
        int32_t y;
        cv_rect(0, 0, 240, 186, RGB(9, 7, 26));
        for (y = 132; y <= 180; y += 12) cv_rect(0, y, 240, 1, RGB(22, 78, 99));
        for (i = -40; i <= 280; i += 34) cv_line(120 + ((i - 120) * 3) / 10, 132, i, 182, RGB(22, 78, 99));
        float jp = fm_fabsf(fm_sinf(t * 2.4f));
        float sy_pos = 104.0f - jp * 32.0f;
        fill_round_rect(86, (int32_t)(sy_pos + 31.0f), 68, 9, 4, COL_CYAN);
        cv_rect(104, (int32_t)(sy_pos + 32.0f), 32, 7, RGB(244, 63, 94));
        draw_organic_star(120.0f, sy_pos, 27.0f, fm_sinf(t * 4.8f) * 0.25f, 1.0f, 1.0f, 4u, STAR_OPT_SKATE, 1u, 0u, 0u, 1, 0u);
    } else {
        /* STARFISH CHEF */
        cv_rect(0, 0, 240, 148, RGB(17, 24, 39));
        cv_rect(0, 148, 240, 38, RGB(51, 65, 85));
        draw_organic_star(86.0f, 114.0f + fm_sinf(t * 3.8f) * 2.5f, 27.0f, fm_sinf(t * 2.8f) * 0.08f, 1.0f, 1.0f, 5u, STAR_OPT_CHEF, 1u, 0u, 0u, 1, 0u);
        int32_t pt = (int32_t)(fm_sinf(t * 5.6f) * 4.0f);
        cv_rect(112, 129 + pt, 28, 5, RGB(148, 163, 184));
        fill_round_rect(136, 125 + pt, 54, 14, 5, RGB(30, 41, 59));
        draw_rect_outline(136, 125 + pt, 54, 14, 1, RGB(203, 213, 225));
        int32_t h1 = (int32_t)(fm_fabsf(fm_sinf(t * 4.8f)) * 42.0f);
        int32_t h2 = (int32_t)(fm_fabsf(fm_cosf(t * 4.2f)) * 34.0f);
        cv_text(148, 108 - h1, &FONT_B, "#", COL_AMBER);
        cv_text(170, 106 - h2, &FONT_B, "*", COL_GREEN);
    }

    /* 2. Boot Footer Plate (y: 186..240) */
    cv_rect(0, 186, 240, 54, RGB(9, 13, 22));
    cv_rect(0, 186, 240, 1, COL_CYAN_DIM);

    text_copy(foot2, "STARFISH | ", sizeof(foot2));
    text_cat(foot2, MASCOT_TITLES[act]);
    {
        int32_t tw = text_w(&FONT_S, foot2);
        cv_text((240 - tw) / 2, 191, &FONT_S, foot2, COL_CYAN);
    }
    text_copy(foot2, JINGLE_NAMES[jidx], sizeof(foot2));
    text_cat(foot2, " | ");
    text_cat(foot2, ui.boot_preset_name[0] ? ui.boot_preset_name : set.patch.name);
    {
        int32_t tw = text_w(&FONT_XS, foot2);
        if (tw > 224) tw = 224;
        cv_text((240 - tw) / 2, 211, &FONT_XS, foot2, COL_LIGHT);
    }
    cv_rect(20, 230, 200, 4, COL_BAR_BG);
    if (prog_w > 0u) cv_rect(20, 230, (int32_t)prog_w, 4, theme_accent());
}
/* ---------------- Top-Right Dancing Starfish, Simulator Screens & Frame Loop ---------------- */

static void star_anim_tick(uint32_t now, float rms)
{
    uint32_t ns = vis.note_trig_seq;
    uint32_t ds = vis.drum_trig_seq;
    if (ns != ui.star_note_seq) {
        ui.star_note_seq = ns;
        ui.last_activity_ms = now;
        ui.star_bounce = 1.0f;
        ui.star_step_dir = (int8_t)(-ui.star_step_dir);
        ui.star_swivel_targ = (float)ui.star_step_dir * 0.24f;
        ui.star_dance_xtarg = (float)ui.star_step_dir * 2.5f;
        ui.star_happy_until = now + 220u;
        ui.star_pulse_cnt++;
        if ((ui.star_pulse_cnt % 8u) == 0u) ui.star_spin_targ += FM_TWO_PI;
    }
    if (ds != ui.star_drum_seq) {
        ui.star_drum_seq = ds;
        ui.last_activity_ms = now;
        ui.star_last_drum = vis.last_drum_idx;
        ui.star_stick_side = (int8_t)(-ui.star_stick_side);
        ui.star_drum_until = now + 180u;
        ui.star_bounce = ui.star_bounce > 0.85f ? ui.star_bounce : 0.85f;
        ui.star_step_dir = (int8_t)(-ui.star_step_dir);
        ui.star_swivel_targ = (float)ui.star_step_dir * 0.28f;
        ui.star_dance_xtarg = (float)ui.star_step_dir * 2.5f;
        ui.star_happy_until = now + 190u;
        ui.star_pulse_cnt++;
        if (vis.last_drum_idx == 4u || (ui.star_pulse_cnt % 8u) == 0u)
            ui.star_spin_targ += FM_TWO_PI;
    }

    ui.star_bounce *= 0.86f;
    if (ui.star_bounce < 0.005f) ui.star_bounce = 0.0f;
    ui.star_swivel_targ *= 0.88f;
    ui.star_dance_xtarg *= 0.88f;
    ui.star_swivel += (ui.star_swivel_targ - ui.star_swivel) * 0.26f;
    ui.star_dance_x += (ui.star_dance_xtarg - ui.star_dance_x) * 0.26f;
    ui.star_spin_cur += (ui.star_spin_targ - ui.star_spin_cur) * 0.26f;
    if (fm_fabsf(ui.star_spin_targ - ui.star_spin_cur) < 0.02f) {
        while (ui.star_spin_targ >= FM_TWO_PI) ui.star_spin_targ -= FM_TWO_PI;
        ui.star_spin_cur = ui.star_spin_targ;
    }
    if (now > ui.star_next_blink)
        ui.star_next_blink = now + 3200u + (xorshift32() % 1400u);
    (void)rms;
}

static void draw_mini_starfish(uint32_t now, float rms)
{
    float t = (float)now * 0.001f;
    uint8_t is_seq = (ui.page == PAGE_SEQ) ? 1u : 0u;
    uint8_t sleepy = (!seq_playing && !rec_armed && !count_in_active && (now - ui.last_activity_ms) >= 45000u) ? 1u : 0u;
    float groove = ui.star_bounce;
    if (rms * 0.85f > groove) groove = rms * 0.85f;
    if (seq_playing && 0.30f > groove) groove = 0.30f;

    float idle_swivel = sleepy ? fm_sinf(t * 0.9f) * 0.04f : fm_sinf(t * 2.3f) * 0.09f;
    float idle_bob = sleepy ? fm_sinf(t * 1.1f) * 0.6f : fm_sinf(t * 3.1f) * 1.1f;
    float seq_hop = seq_playing ? -fm_fabsf(fm_sinf(t * 8.2f)) * 1.6f : 0.0f;
    float step_y = -groove * 3.0f + seq_hop;
    float rot = ui.star_spin_cur + ui.star_swivel + idle_swivel + (seq_playing ? fm_sinf(t * 4.1f) * 0.12f : 0.0f);
    float sx = 1.0f + ui.star_bounce * 0.09f + rms * 0.06f;
    float sy = 1.0f - ui.star_bounce * 0.06f - rms * 0.04f;
    uint8_t blink = (sleepy || (ui.star_next_blink > now && (ui.star_next_blink - now) < 130u)) ? 1u : 0u;
    uint8_t happy = (!sleepy && now < ui.star_happy_until) ? 1u : 0u;
    uint8_t drum_hit = (is_seq && now < ui.star_drum_until) ? 1u : 0u;
    uint8_t dbl = (ui.star_last_drum == 1u || ui.star_last_drum == 4u) ? 1u : 0u;
    uint8_t star_pal = ui.disco_mode ? (uint8_t)((set.mascot_idx + (now / 180u)) % 6u) : set.mascot_idx;

    if (is_seq) {
        fill_ellipse(221, 28, 11, 4, drum_hit ? COL_AMBER : COL_SLATE_D);
        cv_rect(210, 28, 22, 4, RGB(15, 23, 42));
        fill_ellipse(221, 28, 9, 3, drum_hit ? RGB(254, 243, 199) : COL_CYAN);
        cv_line(210, 28, 210, 32, drum_hit ? COL_AMBER : COL_MUTED);
        cv_line(232, 28, 232, 32, drum_hit ? COL_AMBER : COL_MUTED);
        cv_line(210, 32, 232, 32, drum_hit ? COL_AMBER : COL_MUTED);
    }

    draw_organic_star(221.0f + ui.star_dance_x,
                      (is_seq ? 14.0f : 17.0f) + idle_bob + step_y,
                      is_seq ? 11.5f : 12.8f,
                      rot, sx, sy, star_pal,
                      is_seq ? STAR_OPT_DRUMMER : 0u,
                      happy, blink, dbl, ui.star_stick_side, drum_hit);
    if (sleepy) {
        int32_t zy = (int32_t)((now / 450u) % 6u);
        cv_text(204, 4 + (6 - zy), &FONT_T, "Z", COL_CYAN);
    }
}

static void draw_centered(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    int32_t tw = text_w(f, s);
    int32_t x = (240 - tw) / 2;
    if (x < 4) x = 4;
    cv_text(x, y, f, s, c);
}

static void draw_upper_fit(int32_t x, int32_t y, int32_t max_w, const char *s, uint16_t c, uint8_t center)
{
    char up[36];
    const felucca_font_t *f = &FONT_M;
    int32_t tw, dy = 0;
    text_upper(up, s, sizeof(up));
    tw = text_w(f, up);
    if (tw > max_w) { f = &FONT_B; tw = text_w(f, up); dy = 4; }
    if (center) {
        int32_t cx = (240 - tw) / 2;
        if (cx < 4) cx = 4;
        cv_text(cx, y + dy, f, up, c);
    } else {
        cv_text(x, y + dy, f, up, c);
    }
}

static void draw_confirm_screen(uint32_t now, float rms)
{
    char sbuf[32], slotc[8];
    const char *banner = "MEMORY WRITE";
    const char *head = "SAVE PATCH?";
    uint16_t bcol = COL_GREEN;

    if (ui.conf_type == CONF_RAND_LEAVE) {
        banner = "UNSAVED RANDOM"; head = "SAVE BEFORE LEAVE?"; bcol = COL_ORANGE;
    } else if (ui.conf_type == CONF_CLEAR_LOOP) {
        static const char *const CL_HEAD[3] = {"CLEAR SYNTH LOOP?", "CLEAR DRUM PATS?", "CLEAR SYNTH+DRUMS?"};
        banner = "LOOP MEMORY"; head = CL_HEAD[ui.conf_clear_target % 3u]; bcol = COL_AMBER;
    } else if (ui.conf_type == CONF_RESET) {
        banner = "SLOT RESET";
        head = ui.conf_reset_init ? "LOAD INIT PATCH?" : "FACTORY RESET?";
        bcol = COL_AMBER;
    }

    cv_rect(0, 0, 240, 240, RGB(9, 13, 22));
    cv_rect(0, 0, 240, 34, COL_TOPBAR);
    cv_text(10, 10, &FONT_S, banner, bcol);
    draw_mini_starfish(now, rms);

    draw_centered(52, &FONT_B, head, COL_TEXT_HI);
    slot3_text(ui.conf_bx_slot, slotc);

    if (ui.conf_type == CONF_CLEAR_LOOP) {
        uint32_t d_hits = 0u, d, s;
        char dnb[8];
        for (d = 0u; d < SF_DRUM_VOICES; d++)
            for (s = 0u; s < SF_SEQ_STEPS; s++)
                if (set.seq_pat[d][s]) d_hits++;
        int_text((int32_t)synth_loop_cnt, sbuf);
        text_cat(sbuf, " SYNTH | ");
        int_text((int32_t)d_hits, dnb);
        text_cat(sbuf, dnb);
        text_cat(sbuf, " DRUMS");
        draw_centered(84, &FONT_S, sbuf, COL_CYAN);
        cv_rect(16, 116, 208, 24, COL_CELL);
        draw_centered(122, &FONT_XS, "SEL / KNOBS: PICK TARGET", COL_AMBER);
    } else if (ui.conf_type == CONF_RAND_LEAVE) {
        text_copy(sbuf, set.patch.name, sizeof(sbuf));
        text_cat(sbuf, " -> ");
        text_cat(sbuf, slotc);
        draw_centered(82, &FONT_S, sbuf, COL_CYAN);
        draw_centered(100, &FONT_XS, bankx_is_used(ui.conf_bx_slot) ? "OVERWRITES USED SLOT" : "EMPTY BANK X SLOT",
                      bankx_is_used(ui.conf_bx_slot) ? COL_ORANGE : COL_GREEN);
        cv_rect(16, 120, 208, 26, RGB(44, 22, 10));
        draw_rect_outline(16, 120, 208, 26, 1, COL_ORANGE);
        draw_centered(126, &FONT_XS, "SEL : DISCARD & LEAVE", RGB(253, 186, 116));
    } else if (ui.conf_type == CONF_SAVE) {
        const sf_preset_t *ex = bankx_slot_ptr(ui.conf_bx_slot);
        if (!ui.conf_dest_slot) {
            text_copy(sbuf, "BANK X | ", sizeof(sbuf));
            text_cat(sbuf, slotc);
            text_cat(sbuf, bankx_is_used(ui.conf_bx_slot) ? " (" : " (EMPTY)");
            if (bankx_is_used(ui.conf_bx_slot)) { text_cat(sbuf, ex->name); text_cat(sbuf, ")"); }
        } else {
            text_copy(sbuf, "SLOT | ", sizeof(sbuf));
            text_cat(sbuf, set.patch.name);
        }
        draw_centered(84, &FONT_XS, sbuf, COL_CYAN);
        cv_rect(16, 116, 208, 24, COL_CELL);
        draw_centered(122, &FONT_XS, (!ui.conf_dest_slot && set.bank != 9u && !unsaved_random)
                      ? "SEL: CURRENT SLOT | KNOBS: PICK X"
                      : "KNOBS: PICK BANK X SLOT", COL_AMBER);
    } else {
        draw_centered(84, &FONT_S, set.patch.name, COL_CYAN);
        cv_rect(24, 116, 192, 22, COL_CELL);
        draw_centered(121, &FONT_XS, ui.conf_reset_init ? "SEL: STARFISH INIT" : (set.bank == 9u ? "SEL: CLEAR USER SLOT" : "SEL: FACTORY PRESET"), COL_LABEL);
    }

    /* OCT+ YES Button */
    cv_rect(16, 158, 208, 30, COL_GRN_DIM);
    draw_rect_outline(16, 158, 208, 30, 1, COL_GREEN);
    if (ui.conf_type == CONF_RAND_LEAVE) {
        text_copy(sbuf, "OCT+ : SAVE ", sizeof(sbuf));
        text_cat(sbuf, slotc);
        text_cat(sbuf, " & GO");
        draw_centered(166, &FONT_S, sbuf, COL_GREEN);
    } else {
        draw_centered(166, &FONT_S, ui.conf_type == CONF_SAVE ? "OCT+/SAVE : CONFIRM" : "OCT+ : YES CONFIRM", COL_GREEN);
    }

    /* OCT- CANCEL Button */
    cv_rect(16, 196, 208, 28, COL_RED_DIM);
    draw_rect_outline(16, 196, 208, 28, 1, COL_RED);
    draw_centered(203, &FONT_S, ui.conf_type == CONF_RAND_LEAVE ? "OCT- : CANCEL STAY" : "OCT- : CANCEL", COL_RED_TXT);
}

static void draw_hud_overlay(uint32_t now, float rms)
{
    uint32_t i;
    cv_rect(0, 0, 240, 240, COL_BG_HUD);

    if (ui.hud_kind == HUD_BANK) {
        draw_centered(20, &FONT_S, "PRESET BANK", COL_LABEL);
        draw_centered(72, &FONT_M, ui.hud_code, COL_CYAN);
        draw_centered(126, &FONT_S, ui.hud_sub, COL_TEXT_HI);
        for (i = 0; i < 10u; i++) {
            int32_t bx = 12 + (int32_t)i * 21;
            uint8_t is_sel = (i == set.bank) ? 1u : 0u;
            char bc[2] = {BANK_CHAR[i], 0};
            cv_rect(bx + 1, 178, 19, 30, is_sel ? COL_CYAN : COL_CELL);
            cv_text(bx + 6, 185, &FONT_S, bc, is_sel ? COL_BG : COL_LABEL);
        }
    } else if (ui.hud_kind == HUD_PRESET) {
        draw_centered(16, &FONT_M, ui.hud_code, COL_AMBER);
        if (ui.hud_prev[0]) draw_centered(58, &FONT_S, ui.hud_prev, COL_DIM);
        cv_rect(8, 84, 224, 46, COL_CYAN_DIM);
        draw_upper_fit(12, 95, 216, ui.hud_title, COL_TEXT_HI, 1u);
        if (ui.hud_next[0]) draw_centered(138, &FONT_S, ui.hud_next, COL_DIM);
        draw_centered(174, &FONT_XS, ui.hud_sub, COL_CYAN);
        {
            char fbuf[32], cnts[10];
            if (set.bank == 9u) {
                text_copy(fbuf, "BANK X | ", sizeof(fbuf));
                int_text((int32_t)bankx_used_count(), cnts);
                text_cat(fbuf, cnts);
                text_cat(fbuf, "/128 USER SAVED");
            } else {
                text_copy(fbuf, "BANK ", sizeof(fbuf));
                char bc[2] = {BANK_CHAR[set.bank % 10u], 0};
                text_cat(fbuf, bc);
                text_cat(fbuf, " | ");
                int_text((int32_t)bank_len(set.bank), cnts);
                text_cat(fbuf, cnts);
                text_cat(fbuf, " SOUNDS");
            }
            draw_centered(206, &FONT_XS, fbuf, COL_MUTED);
        }
    } else if (ui.hud_kind == HUD_DRUMKIT) {
        draw_centered(16, &FONT_M, ui.hud_code, COL_GREEN);
        draw_centered(58, &FONT_S, ui.hud_prev, COL_DIM);
        cv_rect(8, 84, 224, 46, COL_GRN_DIM);
        draw_upper_fit(12, 95, 216, ui.hud_title, COL_TEXT_HI, 1u);
        draw_centered(138, &FONT_S, ui.hud_next, COL_DIM);
        draw_centered(174, &FONT_XS, ui.hud_sub, COL_CYAN);
        draw_centered(206, &FONT_XS, "DRUM MACHINE | 8 VOICES", COL_MUTED);
    }
    draw_mini_starfish(now, rms);
}

static void draw_top_bar(uint32_t now, float rms)
{
    char code[8], bpm_s[8];
    const char *title = (ui.page == PAGE_SEQ) ? KIT_NAMES[set.drum_kit % SF_DRUM_KITS] : set.patch.name;
    const char *pill = 0;
    uint16_t pill_bg = COL_GREEN, pill_fg = COL_BG;

    cv_rect(0, 0, 240, 34, COL_TOPBAR);
    if (ui.page == PAGE_SEQ) {
        code[0] = 'K';
        int_text((int32_t)set.drum_kit + 1, code + 1);
    } else if (set.bank == 9u) {
        slot3_text(set.prog, code);
    } else {
        uint32_t pv = (uint32_t)set.prog + 1u;
        code[0] = BANK_CHAR[set.bank % 10u];
        code[1] = (char)('0' + (pv / 100u) % 10u);
        code[2] = (char)('0' + (pv / 10u) % 10u);
        code[3] = (char)('0' + pv % 10u);
        code[4] = 0;
    }

    cv_text(7, 10, &FONT_S, code, (ui.page == PAGE_SEQ) ? COL_GREEN : theme_accent());
    cv_text(44, 10, &FONT_S, title, COL_TEXT_HI);

    if ((ui.mute_held || ui.mute_latched) && ui.page == PAGE_SEQ) {
        pill = "MUTE"; pill_bg = COL_RED; pill_fg = C_WHITE;
    } else if (count_in_active) {
        pill = "CNT"; pill_bg = COL_AMBER; pill_fg = COL_BG;
    } else if (rec_armed) {
        pill = "REC"; pill_bg = COL_RED; pill_fg = C_WHITE;
    } else if (patch_compare_ab) {
        pill = "A/B"; pill_bg = COL_AMBER; pill_fg = COL_BG;
    } else if (arp_active && ui.page != PAGE_SEQ) {
        pill = synth_loop_cnt ? "A+LP" : (set.arp_latch ? "HLD" : "ARP");
        pill_bg = set.arp_latch ? COL_AMBER : COL_GREEN;
    } else if (synth_loop_cnt > 0u) {
        pill = "LOOP"; pill_bg = COL_GREEN;
    }

    if (pill) {
        cv_rect(125, 7, 30, 20, pill_bg);
        int32_t tw = text_w(&FONT_XS, pill);
        cv_text(140 - tw / 2, 11, &FONT_XS, pill, pill_fg);
    }

    int_text((int32_t)set.bpm, bpm_s);
    text_cat(bpm_s, " BPM");
    {
        int32_t bw = text_w(&FONT_T, bpm_s);
        cv_text(203 - bw, 12, &FONT_T, bpm_s, seq_playing ? COL_GREEN : COL_LABEL);
    }
    draw_mini_starfish(now, rms);
}

static void draw_page_strip(uint32_t now)
{
    uint8_t p = ui.page;
    uint8_t sub = ui.sub[p];
    uint8_t tot = PAGE_SUBS[p];
    uint16_t acc = theme_accent();
    char title[28];

    if (ui.save_hold_start && (plat_buttons() >> B_SAVE) & 1u) {
        uint32_t w = ((now - ui.save_hold_start) * 212u) / 1500u;
        if (w > 212u) w = 212u;
        cv_rect(8, 38, 224, 42, RGB(32, 26, 12));
        cv_text(14, 43, &FONT_S, "HOLD SAVE FOR RESET...", COL_AMBER);
        cv_rect(14, 66, 212, 7, COL_BAR_BG);
        if (w) cv_rect(14, 66, (int32_t)w, 7, COL_AMBER);
    } else if (ui.rec_hold_start && (plat_buttons() >> B_REC) & 1u) {
        uint32_t w = ((now - ui.rec_hold_start) * 212u) / 1500u;
        if (w > 212u) w = 212u;
        cv_rect(8, 38, 224, 42, COL_RED_DIM);
        cv_text(14, 43, &FONT_S, "HOLD REC TO CLR LOOP...", COL_RED_TXT);
        cv_rect(14, 66, 212, 7, COL_BAR_BG);
        if (w) cv_rect(14, 66, (int32_t)w, 7, COL_RED);
    } else if (count_in_active) {
        uint32_t cb = count_in_beat < 1u ? 1u : (count_in_beat > 4u ? 4u : count_in_beat);
        char cmsg[24];
        text_copy(cmsg, "COUNT-IN  ", sizeof(cmsg));
        cmsg[10] = (char)('0' + cb);
        cmsg[11] = '/';
        cmsg[12] = '4';
        cmsg[13] = 0;
        cv_rect(8, 38, 224, 42, COL_AMB_DIM);
        cv_rect(8, 38, 4, 42, COL_AMBER);
        cv_text(18, 44, &FONT_S, cmsg, COL_AMBER);
        cv_rect(18, 66, 204, 6, COL_BAR_BG);
        cv_rect(18, 66, (int32_t)((cb * 204u) / 4u), 6, COL_AMBER);
    } else if (ui.hud_kind == HUD_BADGE && now < ui.hud_until) {
        cv_rect(8, 38, 224, 42, RGB(17, 24, 38));
        cv_rect(8, 38, 4, 42, ui.hud_accent);
        draw_upper_fit(18, 47, 210, ui.hud_title, ui.hud_accent, 0u);
    } else {
        text_copy(title, sub ? "MIX & DUCK (2/2)" : "PERFORMANCE (1/2)", sizeof(title));
        if (p == PAGE_HOME && sub) acc = COL_AMBER;

        if (p == PAGE_FX) {
            text_copy(title, sub ? "DLY & SAT (2/2)" : "FX RACK (1/2)", sizeof(title));
        } else if (p == PAGE_SEL) {
            text_copy(title, sub == 2u ? "FLT ENV (3/3)" : (sub == 1u ? "FLT TONE (2/3)" : "FLT MODE (1/3)"), sizeof(title));
            if (sub == 2u) acc = COL_AMBER;
        } else if (p == PAGE_ENV) {
            text_copy(title, sub ? "FILTER ENV (2/2)" : "AMP ENV (1/2)", sizeof(title));
            if (sub) acc = COL_AMBER;
        } else if (p == PAGE_LFO) {
            text_copy(title, sub == 2u ? "SWARM/GLD (3/3)" : (sub == 1u ? "LFO 2 MATRIX (2/3)" : "LFO 1 MATRIX (1/3)"), sizeof(title));
            acc = (sub == 1u) ? COL_PURPLE : COL_GREEN;
        } else if (p == PAGE_EDIT) {
            text_copy(title, sub == 2u ? "SYNC & DRV (3/3)" : (sub == 1u ? "SUB & NOI (2/3)" : "OSC & UNI (1/3)"), sizeof(title));
        } else if (p == PAGE_ARP) {
            text_copy(title, sub ? "ARP GROOVE (2/2)" : "ARP SETUP (1/2)", sizeof(title));
            acc = set.arp_latch ? COL_AMBER : COL_GREEN;
        } else if (p == PAGE_GLO) {
            text_copy(title,
                      sub == 4u ? "MIDI & BT I/O (5/5)" : (sub == 3u ? "RANDOM (4/5)" : (sub == 2u ? "COLOR & UI (3/5)" : (sub == 1u ? "REC & SYS (2/5)" : "SCALE/KEY (1/5)"))),
                      sizeof(title));
            if (sub == 4u) acc = COL_CYAN;
            else if (sub == 3u) acc = COL_AMBER;
        } else if (p == PAGE_SEQ) {
            text_copy(title, sub == 2u ? "KIT GLOB (3/3)" : (sub == 1u ? "EUCLID (2/3)" : "DRUM VOX (1/3)"), sizeof(title));
            acc = (ui.mute_held || ui.mute_latched) ? COL_RED : COL_GREEN;
        } else if (p == PAGE_BLE) {
            btdev_title(title);
        }

        draw_upper_fit(10, 47, tot > 1u ? 190 : 220, title, COL_TEXT_HI, 0u);
        if (tot > 1u) {
            uint8_t dp;
            for (dp = 0; dp < tot; dp++) {
                int32_t lx = 228 - (int32_t)(tot - 1u - dp) * 10;
                fill_circle(lx, 59, (dp == sub) ? 3 : 2, (dp == sub) ? acc : COL_SLATE_D);
            }
        }
    }
    cv_rect(0, 84, 240, 1, COL_HAIRLINE);
}

static void draw_env_curve_view(uint32_t now)
{
    const sf_preset_t *pr = &set.patch;
    uint8_t is_f = (ui.sub[PAGE_ENV] == 1u) ? 1u : 0u;
    float wa_a = fm_sqrtf((float)(pr->a_atk + 1u) * (1.0f / 128.0f)) * 52.0f + 8.0f;
    float wd_a = fm_sqrtf((float)(pr->a_dec + 2u) * (1.0f / 128.0f)) * 66.0f + 14.0f;
    float wr_a = fm_sqrtf((float)(pr->a_rel + 2u) * (1.0f / 128.0f)) * 66.0f + 14.0f;
    float sum_a = wa_a + wd_a + wr_a;
    float sc_a = (sum_a + 38.0f > 220.0f) ? (182.0f / sum_a) : 1.0f;
    int32_t axA = 10 + (int32_t)(wa_a * sc_a);
    int32_t axD = axA + (int32_t)(wd_a * sc_a);
    int32_t axR = 230 - (int32_t)(wr_a * sc_a);
    int32_t ayS = 156 - (int32_t)((float)(156 - 98) * fm_clampf((float)pr->a_sus * (1.0f / 127.0f), 0.05f, 1.0f));

    float wa_f = fm_sqrtf((float)(pr->f_atk + 1u) * (1.0f / 128.0f)) * 52.0f + 8.0f;
    float wd_f = fm_sqrtf((float)(pr->f_dec + 2u) * (1.0f / 128.0f)) * 66.0f + 14.0f;
    float wr_f = fm_sqrtf((float)(pr->f_rel + 2u) * (1.0f / 128.0f)) * 66.0f + 14.0f;
    float sum_f = wa_f + wd_f + wr_f;
    float sc_f = (sum_f + 38.0f > 220.0f) ? (182.0f / sum_f) : 1.0f;
    int32_t fxA = 10 + (int32_t)(wa_f * sc_f);
    int32_t fxD = fxA + (int32_t)(wd_f * sc_f);
    int32_t fxR = 230 - (int32_t)(wr_f * sc_f);
    int32_t fyS = 156 - (int32_t)((float)(156 - 98) * fm_clampf((float)pr->f_sus * (1.0f / 127.0f), 0.05f, 1.0f));

    char rbuf[16];
    cv_rect(10, 87, 108, 13, is_f ? COL_AMB_DIM : COL_CYAN_DIM);
    cv_text(13, 88, &FONT_XS, is_f ? "FILTER ADSR" : "AMP ADSR", is_f ? COL_AMBER : COL_CYAN);
    if (is_f) { text_copy(rbuf, "ENV ", sizeof(rbuf)); int_text(pr->f1_env_s, rbuf + 4); text_cat(rbuf, "%"); }
    else { text_copy(rbuf, "REL ", sizeof(rbuf)); fmt_sec_0_127(pr->a_rel, 3000u, rbuf + 4); }
    cv_text(230 - text_w(&FONT_XS, rbuf), 88, &FONT_XS, rbuf, COL_MUTED);

    cv_rect(10, 127, 220, 1, COL_HAIRLINE);
    cv_rect(10, 156, 220, 1, COL_HAIRLINE);

    /* Inactive curve faint */
    if (is_f) {
        cv_line(10, 156, axA, 98, COL_CYAN_DIM); cv_line(axA, 98, axD, ayS, COL_CYAN_DIM);
        cv_line(axD, ayS, axR, ayS, COL_CYAN_DIM); cv_line(axR, ayS, 230, 156, COL_CYAN_DIM);
        draw_thick_line(10, 156, fxA, 102, 1, COL_AMBER);
        draw_thick_line(fxA, 102, fxD, fyS, 1, COL_AMBER);
        draw_thick_line(fxD, fyS, fxR, fyS, 1, COL_AMBER);
        draw_thick_line(fxR, fyS, 230, 156, 1, COL_AMBER);
    } else {
        cv_line(10, 156, fxA, 102, COL_AMB_DIM); cv_line(fxA, 102, fxD, fyS, COL_AMB_DIM);
        cv_line(fxD, fyS, fxR, fyS, COL_AMB_DIM); cv_line(fxR, fyS, 230, 156, COL_AMB_DIM);
        draw_thick_line(10, 156, axA, 98, 1, COL_CYAN);
        draw_thick_line(axA, 98, axD, ayS, 1, COL_CYAN);
        draw_thick_line(axD, ayS, axR, ayS, 1, COL_CYAN);
        draw_thick_line(axR, ayS, 230, 156, 1, COL_CYAN);
    }

    uint16_t nc = is_f ? COL_AMBER : COL_CYAN;
    int32_t px[4] = {is_f ? fxA : axA, is_f ? fxD : axD, is_f ? (fxD + fxR) / 2 : (axD + axR) / 2, is_f ? fxR : axR};
    int32_t py[4] = {is_f ? 102 : 98, is_f ? fyS : ayS, is_f ? fyS : ayS, is_f ? fyS : ayS};
    uint8_t hot = (now < ui.last_env_until) ? ui.last_env_seg : 0u;
    int32_t i;
    for (i = 0; i < 4; i++) {
        if (hot == (uint8_t)(i + 1)) {
            stroke_circle(px[i], py[i], 6, 1, C_WHITE);
            fill_circle(px[i], py[i], 3, C_WHITE);
        } else {
            fill_circle(px[i], py[i], 3, nc);
        }
    }
}

static void draw_center_stage(uint32_t now, float rms)
{
    uint8_t p = ui.page;
    uint8_t sub = ui.sub[p];
    const sf_preset_t *pr = &set.patch;
    int32_t i;

    if (now < ui.save_flash_until) {
        cv_rect(12, 92, 216, 62, COL_GRN_DIM);
        draw_rect_outline(12, 92, 216, 62, 2, COL_GREEN);
        draw_centered(102, &FONT_B, ui.save_banner, COL_GREEN);
        draw_centered(128, &FONT_S, pr->name, COL_LIGHT);
        return;
    }

    if (p == PAGE_ENV) {
        draw_env_curve_view(now);
    } else if (p == PAGE_ARP) {
        static const uint8_t PAT_H[6][8] = {
            {6, 11, 16, 21, 26, 30, 34, 38},
            {38, 32, 26, 21, 16, 12, 8, 5},
            {8, 17, 26, 36, 30, 21, 14, 8},
            {23, 10, 34, 15, 30, 7, 27, 19},
            {12, 18, 14, 20, 26, 21, 28, 34},
            {16, 28, 10, 34, 22, 14, 30, 20}
        };
        const uint8_t *hs = PAT_H[set.arp_mode % SF_NARP_MODE];
        uint32_t beat = (now / (15000u / (set.bpm < 40u ? 40u : set.bpm))) & 7u;
        int32_t gw = 10 + ((int32_t)(set.arp_gate - 20u) * 8) / 75;
        char info[32], octs[4];
        cv_rect(10, 88, 66, 18, COL_GRN_DIM);
        cv_text(14, 91, &FONT_XS, set.arp_latch ? "LATCH" : (arp_active ? "ARP ON" : "ARP OFF"),
                set.arp_latch ? COL_AMBER : (arp_active ? COL_GREEN : COL_MUTED));
        text_copy(info, ARP_MODE_NAMES[set.arp_mode % SF_NARP_MODE], sizeof(info));
        text_cat(info, "|");
        text_cat(info, ARP_RATE_NAMES[set.arp_rate % SF_NARP_RATE]);
        text_cat(info, "|");
        int_text(set.arp_octs, octs);
        text_cat(info, octs);
        text_cat(info, "O");
        cv_text(82, 91, &FONT_XS, info, COL_LIGHT);
        for (i = 0; i < 8; i++) {
            int32_t bx = 14 + i * 27;
            int32_t bh = hs[i] + (int32_t)set.arp_octs * 2;
            uint8_t is_b = (arp_active && (uint32_t)i == beat) ? 1u : 0u;
            cv_rect(bx, 154 - bh, gw, bh, is_b ? COL_AMBER : (arp_active ? COL_CYAN : COL_BAR_BG));
        }
    } else if (p == PAGE_LFO) {
        float t = (float)now * 0.001f;
        uint8_t sh = (sub == 1u) ? sf_lfo2_shape(pr) : (sub == 2u ? 0u : sf_lfo1_shape(pr));
        uint8_t dp = (sub == 1u) ? pr->lfo2_depth : (sub == 2u ? pr->swarm_depth : pr->lfo1_depth);
        uint8_t rt = (sub == 1u) ? pr->lfo2_rate : (sub == 2u ? pr->swarm_rate : pr->lfo1_rate);
        float amp = 24.0f * (0.20f + 0.80f * ((float)dp * 0.01f));
        float freq = 1.0f + ((float)rt * 0.03f);
        uint16_t col = (sub == 1u) ? COL_PURPLE : (sub == 2u ? COL_CYAN : COL_GREEN);
        int32_t prev_y = 124;
        cv_rect(10, 124, 220, 1, COL_HAIRLINE);
        for (i = 10; i < 230; i += 2) {
            float nx = (float)(i - 10) * (1.0f / 220.0f);
            float cyc = nx * 2.0f * freq + t * (0.5f + freq * 0.35f);
            float cyc01 = cyc - fm_floorf(cyc);
            float wv;
            if (sh == 3u) wv = cyc01 < 0.5f ? 1.0f : -1.0f;
            else if (sh == 2u) wv = 2.0f * cyc01 - 1.0f;
            else if (sh == 1u) wv = cyc01 < 0.25f ? cyc01 * 4.0f : (cyc01 < 0.75f ? 2.0f - cyc01 * 4.0f : cyc01 * 4.0f - 4.0f);
            else wv = fm_sin_turns(cyc);
            int32_t y = 124 - (int32_t)(wv * amp);
            if (i > 10) cv_line(i - 2, prev_y, i, y, col);
            prev_y = y;
        }
    } else if (p == PAGE_FX) {
        static const char *const L0[4] = {"CHO", "PHS", "DLY", "REV"};
        static const char *const L1[4] = {"DMX", "DFB", "PHZ", "SAT"};
        static const uint16_t C[4] = {COL_CYAN, COL_GREEN, COL_AMBER, COL_ORANGE};
        int32_t vals[4];
        if (sub == 1u) {
            vals[0] = pr->fx_del_mix; vals[1] = (pr->fx_del_fb * 100) / 95;
            vals[2] = (pr->fx_phaser_rate * 100) / 60; vals[3] = (3 - (int32_t)sf_sat_mode(pr)) * 33;
        } else {
            vals[0] = pr->fx_chorus; vals[1] = pr->fx_phaser;
            vals[2] = pr->fx_del_time > 100u ? (75 + ((int32_t)(pr->fx_del_time - 101u) % 6) * 5) : (((int32_t)pr->fx_del_time * 75) / 100);
            vals[3] = pr->fx_reverb;
        }
        for (i = 0; i < 4; i++) {
            int32_t bx = 18 + i * 54;
            int32_t fh = (44 * clamp(vals[i], 0, 100)) / 100;
            const char *lb = sub == 1u ? L1[i] : L0[i];
            cv_rect(bx, 94, 40, 44, COL_CELL);
            if (fh > 0) cv_rect(bx, 94 + (44 - fh), 40, fh, C[i]);
            cv_text(bx + 20 - text_w(&FONT_XS, lb) / 2, 144, &FONT_XS, lb, COL_LIGHT);
        }
    } else if (p == PAGE_SEL) {
        char p1[16], p2[16];
        uint8_t f1t = sf_f1_type(pr), f2t = sf_f2_type(pr), rt = sf_f_route(pr);
        int32_t cx1 = 12 + ((int32_t)pr->f1_cut * 216) / 127;
        int32_t cx2 = 12 + ((int32_t)pr->f2_cut * 216) / 127;
        text_copy(p1, "F1:", sizeof(p1)); text_cat(p1, FLT_NAMES[f1t]);
        text_copy(p2, "F2:", sizeof(p2)); text_cat(p2, FLT_NAMES[f2t]);
        cv_rect(10, 88, 72, 17, theme_dim());
        cv_text(13, 90, &FONT_XS, p1, theme_accent());
        cv_text(84, 90, &FONT_XS, rt == SF_ROUTE_PAR ? "||" : (rt == SF_ROUTE_SPL ? "//" : "->"), COL_AMBER);
        cv_rect(98, 88, 72, 17, COL_AMB_DIM);
        cv_text(101, 90, &FONT_XS, p2, COL_AMBER);
        cv_text(232 - text_w(&FONT_XS, ROUTE_NAMES[rt]), 90, &FONT_XS, ROUTE_NAMES[rt], COL_LABEL);
        cv_rect(12, 156, 216, 1, COL_HAIRLINE);
        /* Dual filter magnitude sketch */
        int32_t prev1 = 125, prev2 = 125;
        for (i = 0; i <= 54; i++) {
            int32_t x = 12 + i * 4;
            int32_t d1 = x - cx1, d2 = x - cx2;
            float r1 = (f1t == SF_FLT_HP) ? (d1 >= 0 ? 0.72f : 0.14f) : (f1t == SF_FLT_BP ? (1.0f / (1.0f + (float)(d1 * d1) * 0.002f)) * 0.75f : (d1 <= 0 ? 0.72f : fm_maxf(0.08f, 0.72f - (float)d1 * 0.01f)));
            float r2 = (f2t == SF_FLT_HP) ? (d2 >= 0 ? 0.72f : 0.14f) : (f2t == SF_FLT_BP ? (1.0f / (1.0f + (float)(d2 * d2) * 0.002f)) * 0.75f : (d2 <= 0 ? 0.72f : fm_maxf(0.08f, 0.72f - (float)d2 * 0.01f)));
            int32_t y1 = 155 - (int32_t)(r1 * 40.0f);
            int32_t y2 = 155 - (int32_t)(r2 * 40.0f);
            if (i > 0) { cv_line(x - 4, prev2, x, y2, COL_AMB_DIM); cv_line(x - 4, prev1, x, y1, theme_accent()); }
            prev1 = y1; prev2 = y2;
        }
    } else if (p == PAGE_EDIT) {
        if (sub == 2u) {
            char line[32], nb[8];
            cv_rect(12, 92, 96, 24, RGB(34, 22, 12));
            cv_text(18, 97, &FONT_XS, sf_osc_sync(pr) ? "SYNC: ON" : "SYNC: OFF", sf_osc_sync(pr) ? COL_GREEN : COL_MUTED);
            text_copy(line, "FM MOD: ", sizeof(line)); int_text(pr->fm_amt, nb); text_cat(line, nb); text_cat(line, "%");
            cv_text(120, 97, &FONT_XS, line, COL_AMBER);
            text_copy(line, "UNI PAN: ", sizeof(line)); int_text(pr->uni_pan, nb); text_cat(line, nb);
            text_cat(line, "% | DRV: "); int_text(pr->fx_drive, nb); text_cat(line, nb); text_cat(line, "%");
            cv_text(14, 134, &FONT_XS, line, COL_LIGHT);
        } else if (sub == 1u) {
            /* Sub + Noise + Ring Mod Interactive Waveform */
            uint8_t sw = sf_sub_wave(pr) % 3u;
            float sub_a = (float)pr->sub_vol * 0.01f;
            float noi_a = (float)pr->noise_vol * 0.01f;
            float rng_a = (float)pr->ring_vol * 0.01f;
            int32_t py = 128;
            char hdr[28], nb[8];
            text_copy(hdr, "SUB:", sizeof(hdr)); text_cat(hdr, SUB_NAMES[sw]);
            text_cat(hdr, " | RNG:"); int_text(pr->ring_vol, nb); text_cat(hdr, nb); text_cat(hdr, "%");
            cv_text(12, 88, &FONT_XS, hdr, theme_accent());
            cv_rect(12, 128, 216, 1, COL_HAIRLINE);
            for (i = 0; i <= 72; i++) {
                float ph = ((float)i / 72.0f) * 2.0f;
                float cyc = ph - fm_floorf(ph);
                float w;
                if (sw == 0u) w = cyc < 0.5f ? 1.0f : -1.0f;
                else if (sw == 1u) w = fm_sin_turns(ph);
                else w = 2.0f * cyc - 1.0f;
                w *= sub_a;
                if (rng_a > 0.01f) w = w * (1.0f - 0.5f * rng_a) + fm_sin_turns(ph * 2.5f) * fm_sin_turns(ph * 1.0f) * rng_a;
                if (noi_a > 0.02f) {
                    float nz = ((float)(((i * 73 + (int32_t)(now / 40u)) & 31) - 16) / 16.0f);
                    w += nz * noi_a * 0.55f;
                }
                w = fm_clampf(w, -1.0f, 1.0f);
                int32_t x = 12 + i * 3;
                int32_t y = 128 - (int32_t)(w * 23.0f);
                if (i > 0) cv_line(x - 3, py, x, y, noi_a > 0.45f ? COL_AMBER : theme_accent());
                py = y;
            }
        } else {
            /* OSC & UNISON: Live Oscillator Waveform + Symmetric Unison Detune Spread */
            uint8_t om = sf_osc_mode(pr);
            uint8_t uni = (uint8_t)clamp((int32_t)sf_unison(pr), 1, 5);
            float bal = (float)pr->osc_bal * 0.01f;
            float pw_f = fm_clampf((float)pr->pw * (1.0f / 127.0f), 0.08f, 0.92f);
            float spr = (float)pr->spread * 0.01f;
            float det_ratio = 1.0f + ((float)pr->osc2_semi * 0.0416667f);
            float tph = (float)now * 0.0012f;
            int32_t py = 128;
            char oinfo[24], nb[8];
            text_copy(oinfo, OSC_NAMES[om % SF_NOSC], sizeof(oinfo));
            text_cat(oinfo, " ");
            if (pr->osc2_semi >= 0) text_cat(oinfo, "+");
            int_text(pr->osc2_semi, nb); text_cat(oinfo, nb); text_cat(oinfo, "st");
            cv_text(12, 88, &FONT_XS, oinfo, theme_accent());
            cv_rect(12, 128, 136, 1, COL_HAIRLINE);
            for (i = 0; i <= 45; i++) {
                float u = ((float)i / 45.0f) * 2.0f + tph * 0.25f;
                float u1 = u - fm_floorf(u);
                float u2s = u * fm_maxf(0.25f, det_ratio);
                float u2 = u2s - fm_floorf(u2s);
                float w1, w2 = 2.0f * u2 - 1.0f;
                if (om == SF_OSC_PULSE) w1 = (u1 < pw_f) ? 1.0f : -1.0f;
                else if (om == SF_OSC_TRI) w1 = u1 < 0.25f ? u1 * 4.0f : (u1 < 0.75f ? 2.0f - u1 * 4.0f : u1 * 4.0f - 4.0f);
                else if (om == SF_OSC_WT) w1 = fm_sin_turns(u) * (1.0f - pw_f * 0.45f) + fm_sin_turns(u * (2.0f + pw_f * 3.0f)) * pw_f * 0.65f;
                else if (om == SF_OSC_HYPERSAW) w1 = ((2.0f * u1 - 1.0f) * 0.65f) + (2.0f * ((u * (1.0f + spr * 0.08f)) - fm_floorf(u * (1.0f + spr * 0.08f))) - 1.0f) * 0.35f;
                else w1 = 2.0f * u1 - 1.0f;
                float mixw = fm_clampf(w1 * (1.0f - bal) + w2 * bal, -1.0f, 1.0f);
                int32_t x = 12 + i * 3;
                int32_t y = 128 - (int32_t)(mixw * 23.0f);
                if (i > 0) cv_line(x - 3, py, x, y, theme_accent());
                py = y;
            }
            /* Right side: Unison voices fanned symmetrically by detune spread */
            {
                char ustr[12];
                ustr[0] = 'U'; ustr[1] = 'N'; ustr[2] = 'I'; ustr[3] = ' '; ustr[4] = (char)('0' + uni); ustr[5] = 'V'; ustr[6] = 0;
                cv_text(162, 88, &FONT_XS, ustr, COL_LABEL);
                cv_rect(158, 104, 72, 48, COL_CELL);
                for (i = 0; i < (int32_t)uni; i++) {
                    float off = uni > 1u ? (((float)i / (float)(uni - 1u)) - 0.5f) * 2.0f : 0.0f;
                    int32_t vx = 194 + (int32_t)(off * (12.0f + spr * 18.0f));
                    int32_t vh = 38 - (int32_t)(fm_fabsf(off) * 14.0f);
                    cv_rect(vx - 3, 148 - vh, 6, vh, (i == (int32_t)(uni / 2u)) ? theme_accent() : COL_AMBER);
                }
            }
        }
    } else if (p == PAGE_SEQ) {
        uint32_t dlen = eff_drum_loop_len(set.seq_length);
        uint32_t play_dstep = (uint32_t)seq_step % dlen;
        uint8_t abar = (seq_playing && !rec_armed) ? (uint8_t)(play_dstep / 16u) : (ui.sel_step / 16u);
        uint8_t bars = (uint8_t)((dlen + 15u) / 16u);
        char bpill[10], snum[8];
        if (abar >= bars) abar = 0u;
        for (i = 0; i < 8; i++) {
            int32_t tx = 8 + i * 23;
            uint8_t mut = (set.drum_mute >> (uint32_t)i) & 1u;
            uint8_t sel = (i == (int32_t)ui.sel_drum) ? 1u : 0u;
            cv_rect(tx, 86, 21, 15, mut ? (sel ? COL_RED : COL_RED_DIM) : (sel ? COL_CYAN : COL_CELL));
            cv_text(tx + 3, 88, &FONT_XS, DRUM_NAMES[i], mut ? (sel ? C_WHITE : COL_RED_TXT) : (sel ? COL_BG : COL_LIGHT));
        }
        cv_rect(194, 86, 38, 15, COL_BAR_BG);
        bpill[0] = 'B'; bpill[1] = (char)('1' + abar); bpill[2] = '/'; bpill[3] = (char)('0' + bars); bpill[4] = 0;
        cv_text(201, 88, &FONT_XS, bpill, COL_AMBER);
        for (i = 0; i < 16; i++) {
            uint32_t abs_s = (uint32_t)abar * 16u + (uint32_t)i;
            uint8_t inl = abs_s < dlen ? 1u : 0u;
            uint8_t hit = inl ? set.seq_pat[ui.sel_drum][abs_s] : 0u;
            uint8_t is_ph = (seq_playing && play_dstep == abs_s) ? 1u : 0u;
            uint8_t is_cur = (ui.sel_step == abs_s) ? 1u : 0u;
            int32_t sx = 14 + (i & 7) * 27, sy = 106 + (i >> 3) * 27;
            uint16_t bg = !inl ? RGB(11, 15, 24) : (is_ph ? COL_GREEN : (hit > 1u ? COL_AMBER : (hit ? COL_CYAN : COL_CELL)));
            cv_rect(sx, sy, 24, 22, bg);
            if (is_cur) draw_rect_outline(sx - 1, sy - 1, 26, 24, 1, rec_armed ? COL_RED : C_WHITE);
            int_text((int32_t)abs_s + 1, snum);
            cv_text(sx + (24 - text_w(&FONT_XS, snum)) / 2, sy + 5, &FONT_XS, snum,
                    !inl ? COL_BAR_BG : ((is_ph || hit) ? COL_BG : COL_MUTED));
        }
    } else if (p == PAGE_GLO) {
        char l1[36], nb[8];
        if (sub == 4u) {
            int hp = plat_headphones();
            text_copy(l1, "SYNTH CH: ", sizeof(l1));
            if (set.midi_synth_ch == 0u) text_cat(l1, "OMNI");
            else { int_text(set.midi_synth_ch, nb); text_cat(l1, nb); }
            text_cat(l1, " | DRM: ");
            if (set.midi_drum_ch == 0u) text_cat(l1, "OFF");
            else { int_text(set.midi_drum_ch, nb); text_cat(l1, nb); }
            cv_text(12, 92, &FONT_XS, l1, COL_CYAN);
            text_copy(l1, "BT: ", sizeof(l1));
            text_cat(l1, BT_MODE_NAMES[set.bt_mode % 4u]);
            cv_text(12, 112, &FONT_S, l1, set.ble_off ? COL_RED_TXT : COL_GREEN);
            text_copy(l1, "OUT: ", sizeof(l1));
            text_cat(l1, OUT_MODE_NAMES[set.audio_out_mode % 3u]);
            text_cat(l1, hp == 1 ? " (HP IN)" : (hp == 0 ? " (SPKR)" : ""));
            cv_text(12, 136, &FONT_XS, l1, COL_AMBER);
        } else if (sub == 3u) {
            char sc[8]; slot3_text(target_bankx_slot, sc);
            cv_rect(10, 90, 220, 66, RGB(28, 24, 12));
            draw_rect_outline(10, 90, 220, 66, 1, COL_AMBER);
            text_copy(l1, RAND_NAMES[set.rand_style % SF_NRAND_STYLE], sizeof(l1));
            text_cat(l1, " | CHAOS "); int_text(set.rand_chaos, nb); text_cat(l1, nb); text_cat(l1, "%");
            cv_text(18, 96, &FONT_XS, l1, COL_AMBER);
            cv_text(18, 114, &FONT_XS, rand_count ? pr->name : "SAFE ANTI-SCREECH ENGINE", COL_GREEN);
            text_copy(l1, "K2:GEN | K4:", sizeof(l1)); text_cat(l1, sc);
            cv_text(18, 134, &FONT_XS, l1, COL_LABEL);
        } else if (sub == 2u) {
            static const uint16_t SWATCH[5] = {
                COL_GREEN, COL_AMBER, COL_CYAN, RGB(251, 113, 133), RGB(226, 232, 240)
            };
            static const char *const SW_LBL[5] = {"EMR", "AMB", "CYN", "CRM", "MNO"};
            cv_text(12, 88, &FONT_XS, "OLED COLOR SCHEME:", COL_LIGHT);
            for (i = 0; i < 5; i++) {
                int32_t bx = 12 + i * 44;
                uint8_t sel = (i == (int32_t)(set.palette % 5u)) ? 1u : 0u;
                cv_rect(bx, 104, 40, 28, sel ? SWATCH[i] : COL_CELL);
                if (sel) draw_rect_outline(bx - 1, 103, 42, 30, 1, C_WHITE);
                cv_text(bx + 8, 112, &FONT_XS, SW_LBL[i], sel ? COL_BG : SWATCH[i]);
            }
            text_copy(l1, "STAR: ", sizeof(l1)); text_cat(l1, MASCOT_COLOR_NAMES[set.mascot_idx % 6u]);
            text_cat(l1, " | OCT: "); if (set.octave >= 0) text_cat(l1, "+");
            int_text(set.octave, nb); text_cat(l1, nb);
            cv_text(12, 141, &FONT_XS, l1, COL_LABEL);
        } else if (sub == 1u) {
            text_copy(l1, "REC: ", sizeof(l1)); int_text((int32_t)set.seq_length, nb); text_cat(l1, nb);
            text_cat(l1, "ST | "); text_cat(l1, set.rec_quant ? "QUANT" : "FREE");
            cv_text(12, 96, &FONT_S, l1, COL_GREEN);
            text_copy(l1, "POLY: ", sizeof(l1)); int_text((int32_t)sf_polyphony(pr), nb); text_cat(l1, nb);
            text_cat(l1, "V | SWG: "); int_text(set.swing, nb); text_cat(l1, nb); text_cat(l1, "%");
            cv_text(12, 118, &FONT_S, l1, COL_AMBER);
            text_copy(l1, "GLIDE: ", sizeof(l1)); int_text(pr->glide, nb); text_cat(l1, nb);
            text_cat(l1, " | OCT: "); int_text(set.octave, nb); text_cat(l1, nb);
            cv_text(12, 140, &FONT_XS, l1, COL_LIGHT);
        } else {
            text_copy(l1, "KEY: ", sizeof(l1)); text_cat(l1, NOTE12[set.root % 12u]);
            text_cat(l1, " "); text_cat(l1, SCALE_NAMES[set.scale % SF_NSCALE]);
            cv_text(12, 96, &FONT_S, l1, theme_accent());
            text_copy(l1, "CHORD: ", sizeof(l1)); text_cat(l1, CHORD_NAMES[set.chord_mode % SF_NCHORD]);
            text_cat(l1, " | "); int_text(set.bpm, nb); text_cat(l1, nb); text_cat(l1, " BPM");
            cv_text(12, 118, &FONT_S, l1, COL_AMBER);
            cv_text(12, 140, &FONT_XS, "HOLD GLO+HOME: BLE MIDI", COL_LIGHT);
        }
    } else if (p == PAGE_BLE) {
        int cnt = btdev_count();
        char bhdr[32];
        text_copy(bhdr, "BT:", sizeof(bhdr));
        text_cat(bhdr, BT_MODE_NAMES[set.bt_mode % 4u]);
        cv_text(12, 87, &FONT_XS, bhdr, COL_CYAN);
        cv_text(154, 87, &FONT_XS, "SAVE:CONN", COL_AMBER);
        if (cnt == 0) {
            draw_centered(120, &FONT_XS, set.bt_mode == 2u ? "USB/BLE PC DEVICE" : "SCANNING BLE MIDI...", COL_LABEL);
        } else {
            for (i = 0; i < cnt && i < 4; i++) {
                char nm[22]; int rssi = -80;
                int32_t ry = 102 + i * 14, bars, bi;
                btdev_found((int)i, nm, &rssi);
                uint8_t sel = (i == btdev_pick()) ? 1u : 0u;
                cv_rect(12, ry, 216, 13, sel ? theme_dim() : COL_CELL);
                cv_text(16, ry + 1, &FONT_XS, nm, sel ? COL_TEXT_HI : COL_LIGHT);
                bars = rssi > -60 ? 3 : (rssi > -76 ? 2 : 1);
                for (bi = 0; bi < 3; bi++) {
                    cv_rect(206 + bi * 5, ry + 8 - bi * 3, 3, 3 + bi * 3, bi < bars ? COL_GREEN : COL_BAR_BG);
                }
            }
        }
    } else if (p == PAGE_HOME && sub == 1u) {
        /* HOME 2/2: MIXER & KICK SIDECHAIN DUCKING METER */
        char mb[24], nb[8];
        int32_t sw = (130 * (int32_t)set.synth_vol) / 100;
        int32_t dw = (130 * (int32_t)set.drum_vol) / 100;
        int32_t dkw = (130 * (int32_t)vis.duck_pct) / 100;
        cv_text(12, 91, &FONT_XS, "SYNTH", COL_CYAN);
        cv_rect(64, 92, 130, 10, COL_CELL);
        if (sw > 0) cv_rect(64, 92, sw, 10, COL_CYAN);
        int_text(set.synth_vol, nb); text_copy(mb, nb, sizeof(mb)); text_cat(mb, "%");
        cv_text(200, 91, &FONT_XS, mb, COL_TEXT_HI);

        cv_text(12, 110, &FONT_XS, "DRUMS", COL_GREEN);
        cv_rect(64, 111, 130, 10, COL_CELL);
        if (dw > 0) cv_rect(64, 111, dw, 10, COL_GREEN);
        int_text(set.drum_vol, nb); text_copy(mb, nb, sizeof(mb)); text_cat(mb, "%");
        cv_text(200, 110, &FONT_XS, mb, COL_TEXT_HI);

        text_copy(mb, "DUCK(", sizeof(mb));
        text_cat(mb, DRUM_NAMES[set.duck_src % SF_DRUM_VOICES]);
        text_cat(mb, ")");
        cv_text(12, 129, &FONT_XS, mb, COL_AMBER);
        cv_rect(64, 130, 130, 10, COL_CELL);
        if (dkw > 0) cv_rect(64, 130, dkw, 10, COL_AMBER);
        cv_text(200, 129, &FONT_XS, vis.duck_pct > 2u ? "-" : "0%", COL_AMBER);
        if (vis.duck_pct > 2u) {
            int_text((int32_t)vis.duck_pct, nb);
            text_copy(mb, "-", sizeof(mb)); text_cat(mb, nb); text_cat(mb, "%");
            cv_text(200, 129, &FONT_XS, mb, COL_AMBER);
        }
        cv_text(12, 148, &FONT_XS, "PRESS HOME TO RETURN 1/2", COL_MUTED);
    } else {
        /* HOME 1/2: Live 32-band Spectrum Analyzer + Cutoff Passband + Glowing Oscilloscope + Playhead Strip */
        int32_t cx1 = 12 + ((int32_t)pr->f1_cut * 216) / 127;
        int32_t cx2 = 12 + ((int32_t)pr->f2_cut * 216) / 127;
        float tsec = (float)now * 0.003f;
        uint32_t slen = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
        uint32_t bars = (slen + 15u) / 16u;
        uint32_t base_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        uint32_t tot_us = slen * base_us;
        float prog = (seq_playing && tot_us > 0u) ? fm_clampf((float)loop_cycle_us / (float)tot_us, 0.0f, 1.0f) : 0.0f;

        cv_rect(12, 88, cx1 > 20 ? cx1 - 12 : 8, 56, RGB(10, 22, 30));
        for (i = 0; i < 32; i++) {
            float bv = (float)vis.bands[i] * 0.01f;
            float shim = (0.06f + 0.04f * fm_sinf(tsec * 1.4f + (float)i * 0.45f)) * (1.0f - fm_minf(1.0f, rms * 1.6f));
            float mag = bv > shim ? bv : shim;
            int32_t bh = (int32_t)(mag * 50.0f);
            if (bh < 3) bh = 3;
            if (bh > 50) bh = 50;
            int32_t bx = 12 + (i * 216) / 32;
            uint16_t bc = bv > 0.65f ? COL_AMBER : (bx <= cx1 + 4 ? theme_band() : COL_BAR_BG);
            cv_rect(bx, 144 - bh, 5, bh, bc);
        }
        cv_rect(cx1 - 1, 88, 2, 56, theme_accent());
        cv_rect(cx2, 92, 1, 52, COL_AMBER);
        int32_t prev_y = 115, prev_x = 12;
        uint16_t sc_col = rms > 0.02f ? theme_glow() : theme_accent();
        for (i = 0; i < 64; i++) {
            float raw = (float)vis.scope[i * 2] * (1.0f / 120.0f);
            if (rms < 0.01f) raw += fm_sinf(tsec * 1.6f + (float)i * 0.28f) * 0.045f;
            int32_t x = 12 + (i * 216) / 63;
            int32_t y = clamp(115 + (int32_t)(raw * 24.0f), 88, 144);
            if (i > 0) cv_line(prev_x, prev_y, x, y, sc_col);
            prev_x = x; prev_y = y;
        }

        /* Recording / Loop Playhead Progress Meter (y: 148..161) */
        {
            char plbl[12];
            uint32_t cur_bar = (seq_step / 16u) + 1u;
            uint32_t cur_beat = ((seq_step % 16u) / 4u) + 1u;
            uint16_t pcol = rec_armed ? COL_RED : (seq_playing ? theme_accent() : COL_SLATE_D);
            int32_t px = 62 + (int32_t)(prog * 166.0f);
            if (seq_playing || rec_armed) {
                plbl[0] = rec_armed ? 'R' : 'B';
                if (cur_bar >= 10u) {
                    int_text((int32_t)cur_bar, plbl + 1);
                    text_cat(plbl, ".");
                    {
                        char bch[2] = {(char)('0' + cur_beat), 0};
                        text_cat(plbl, bch);
                    }
                } else {
                    plbl[1] = (char)('0' + cur_bar);
                    plbl[2] = '.';
                    plbl[3] = (char)('0' + cur_beat);
                    plbl[4] = 0;
                }
            } else if (synth_loop_cnt > 0u) {
                text_copy(plbl, "LP ", sizeof(plbl));
                int_text((int32_t)synth_loop_cnt, plbl + 3);
            } else {
                int_text((int32_t)bars, plbl);
                text_cat(plbl, "BAR");
            }
            cv_text(10, 149, &FONT_XS, plbl, rec_armed ? COL_RED_TXT : (seq_playing ? COL_TEXT_HI : COL_MUTED));
            cv_rect(62, 150, 166, 9, COL_CELL);
            if (seq_playing && px > 62) cv_rect(62, 150, px - 62, 9, pcol);
            /* Beat & bar dividers across progress bar */
            {
                uint32_t bi;
                for (bi = 1u; bi < bars; bi++) {
                    int32_t tx = 62 + (int32_t)((bi * 166u) / bars);
                    cv_rect(tx, 149, 1, 11, COL_BG);
                }
            }
            if (seq_playing || rec_armed) cv_rect(px - 1, 148, 2, 13, C_WHITE);
        }
    }

    /* Compact live playhead indicator on non-HOME/non-SEQ pages when recording or playing */
    if (p != PAGE_HOME && p != PAGE_SEQ && (seq_playing || rec_armed)) {
        uint32_t slen = set.seq_length < 1u ? 1u : (set.seq_length > SF_SYNTH_MAX_STEPS ? SF_SYNTH_MAX_STEPS : set.seq_length);
        uint32_t base_us = 15000000u / (uint32_t)(set.bpm < 40u ? 40u : set.bpm);
        uint32_t tot_us = slen * base_us;
        float prog = tot_us > 0u ? fm_clampf((float)loop_cycle_us / (float)tot_us, 0.0f, 1.0f) : 0.0f;
        int32_t pw = (int32_t)(prog * 220.0f);
        cv_rect(10, 160, 220, 3, COL_CELL);
        if (pw > 0) cv_rect(10, 160, pw, 3, rec_armed ? COL_RED : theme_accent());
        cv_rect(10 + pw - 1, 159, 2, 5, C_WHITE);
    }
}

static void draw_bottom_dock(uint32_t now)
{
    uint8_t k;
    cv_rect(0, 164, 240, 1, COL_HAIRLINE);

    if (ui.hud_kind == HUD_PARAM && now < ui.hud_until) {
        int32_t lw = text_w(&FONT_S, ui.hud_title);
        int32_t avail = 218 - lw;
        const felucca_font_t *vf = &FONT_L;
        int32_t vy = 167;
        cv_rect(0, 165, 240, 75, COL_DOCK);
        cv_text(10, 174, &FONT_S, ui.hud_title, theme_glow());
        if (text_w(&FONT_L, ui.hud_sub) > avail) {
            if (text_w(&FONT_M, ui.hud_sub) <= avail) { vf = &FONT_M; vy = 171; }
            else { vf = &FONT_B; vy = 175; }
        }
        cv_text(230 - text_w(vf, ui.hud_sub), vy, vf, ui.hud_sub, COL_TEXT_HI);
        cv_rect(10, 207, 220, 19, COL_BAR_BG);
        {
            int32_t fw = (220 * clamp(ui.hud_val_pct, 0, 100)) / 100;
            if (fw > 0) cv_rect(10, 207, fw, 19, ui.hud_accent);
        }
        draw_rect_outline(10, 207, 220, 19, 1, COL_HAIRLINE);
        return;
    }

    /* 2x2 Parameter Grid */
    cv_rect(120, 164, 1, 76, COL_HAIRLINE);
    cv_rect(0, 202, 240, 1, COL_HAIRLINE);
    for (k = 0; k < 4u; k++) {
        char lbl[16], val[16];
        int32_t pct = 0;
        int32_t cx = (k >= 2u) ? 120 : 0;
        int32_t cy = (k & 1u) ? 202 : 164;
        knob_label_val(k, lbl, val, &pct);
        cv_text(cx + 8, cy + 4, &FONT_XS, lbl, COL_LABEL);
        cv_text(cx + 112 - text_w(&FONT_S, val), cy + 16, &FONT_S, val, COL_TEXT_HI);
        cv_rect(cx + 8, cy + 33, 104, 3, COL_BAR_BG);
        {
            int32_t bw = (104 * clamp(pct, 0, 100)) / 100;
            if (bw > 0) cv_rect(cx + 8, cy + 33, bw, 3, (k < 2u) ? theme_accent() : COL_AMBER);
        }
    }
}

static void render_strip(uint32_t now, float rms, int32_t y_offset)
{
    cv_begin(240u, 120u, COL_BG);
    cv_oy = y_offset;
    cv_y0 = 0;
    cv_y1 = 120;

    if (now < ui.boot_until) {
        draw_boot_splash(now);
    } else if (ui.conf_type != CONF_NONE) {
        draw_confirm_screen(now, rms);
    } else if ((ui.hud_kind == HUD_BANK || ui.hud_kind == HUD_PRESET || ui.hud_kind == HUD_DRUMKIT) && now < ui.hud_until) {
        draw_hud_overlay(now, rms);
    } else {
        draw_top_bar(now, rms);
        draw_page_strip(now);
        draw_center_stage(now, rms);
        draw_bottom_dock(now);
    }
    cv_oy = 0;
}

static void draw_screen(uint32_t now)
{
    float rms;
    sf_dsp_get_vis(&vis);
    rms = (float)vis.rms_pct * 0.01f;
    star_anim_tick(now, rms);

    render_strip(now, rms, 0);
    cv_blit(0u, 0u);
    render_strip(now, rms, -120);
    cv_blit(0u, 120u);
    lcd_sync();
}

static void update_leds(uint32_t now)
{
    uint32_t btn = 0u, dim_b = 0u, keys = 0u, dim_k = 0u, mid_k = 0u;
    uint32_t active_page_bit = 0u;
    static const uint8_t WHT[16] = PLAT_WHITE_KEYS;
    static const uint8_t BLK[11] = PLAT_BLACK_KEYS;

    if (now < ui.boot_until) {
        uint32_t beat = (now / 140u) % 10u;
        btn = (1u << beat);
        plat_lights(btn, 0u, 0u, 0u, 1);
        plat_lights_mid(0u, 0u);
        return;
    }

    if (ui.page == PAGE_FX) active_page_bit = (1u << B_FX);
    else if (ui.page == PAGE_SEL) active_page_bit = (1u << B_SEL);
    else if (ui.page == PAGE_ENV) active_page_bit = (1u << B_ENV);
    else if (ui.page == PAGE_LFO) active_page_bit = (1u << B_LFO);
    else if (ui.page == PAGE_EDIT) active_page_bit = (1u << B_EDIT);
    else if (ui.page == PAGE_GLO || ui.page == PAGE_BLE) active_page_bit = (1u << B_GLO);
    else if (ui.page == PAGE_HOME) active_page_bit = (1u << B_HOME);
    else if (ui.page == PAGE_SEQ) active_page_bit = (1u << B_SEQ);

    btn |= active_page_bit;
    if (arp_active) btn |= (1u << B_ARP);
    if (set_dirty) btn |= (1u << B_SAVE);
    if (rec_armed) btn |= (1u << B_REC);
    if (seq_playing) btn |= LED_PLAY_GREEN;
    dim_b = ((1u << NB) - 1u) & ~btn;

    if (ui.page == PAGE_SEQ) {
        uint32_t dlen = eff_drum_loop_len(set.seq_length);
        uint32_t play_dstep = (uint32_t)seq_step % dlen;
        uint32_t abar = (seq_playing && !rec_armed) ? (play_dstep / 16u) : (ui.sel_step / 16u);
        if (abar * 16u >= dlen) abar = (dlen - 1u) / 16u;
        uint32_t i;
        keys |= (1u << BLK[ui.sel_drum]);
        if (ui.mute_held || ui.mute_latched) keys |= (1u << BLK[10]);
        for (i = 0; i < 8u; i++) if ((set.drum_mute >> i) & 1u) mid_k |= (1u << BLK[i]);
        for (i = 0; i < 16u; i++) {
            uint32_t s = abar * 16u + i;
            if (s < dlen && set.seq_pat[ui.sel_drum][s]) {
                if (seq_playing) mid_k |= (1u << WHT[i]);
                else keys |= (1u << WHT[i]);
            }
            if (!seq_playing && ui.sel_step == s) mid_k |= (1u << WHT[i]);
            if (seq_playing && play_dstep == s) keys |= (1u << WHT[i]);
        }
    } else {
        keys = ui.prev_keys;
    }
    plat_lights(btn, keys, dim_b, dim_k, 1);
    plat_lights_mid(0u, mid_k);
}

void app_ui_init(void)
{
    uint32_t now = plat_ms();
    uint8_t rp_idx, jb, jp;
    const sf_preset_t *jpatch;
    mem_zero(&ui, (uint32_t)sizeof(ui));
    ui.boot_act = (uint8_t)((now + xorshift32()) % 6u);
    ui.boot_jingle = (uint8_t)((now + (xorshift32() >> 3)) % 6u);
    rp_idx = (uint8_t)((now ^ (xorshift32() >> 5)) % 6u);
    jb = set.recent_presets[rp_idx][0] % 10u;
    jp = (uint8_t)(set.recent_presets[rp_idx][1] % bank_len(jb));
    jpatch = get_preset_ptr(jb, jp);
    text_copy(ui.boot_preset_name, jpatch->name, sizeof(ui.boot_preset_name));
    set.mascot_idx = ui.boot_act;
    ui.boot_start = now;
    ui.boot_until = now + BOOT_SPLASH_MS;
    ui.star_step_dir = 1;
    ui.star_stick_side = 1;
    ui.star_next_blink = now + 2200u;
    ui.last_menu_ms = now;
    ui.last_activity_ms = now;
    ui.redraw = 1u;
    sf_dsp_start_jingle(ui.boot_jingle, jpatch);
#ifdef APP_TOUCH
    touch_on(1);
#endif
    draw_screen(now);
    update_leds(now);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    btdev_page((ui.page == PAGE_BLE && !set.ble_off && set.bt_mode != 2u) ? 1 : 0);
    {
        int ev;
        while ((ev = btdev_event()) != BTDEV_NONE)
            show_hud_badge(now, btdev_word(ev), btdev_name(), COL_CYAN, 1100u);
    }
    input(now);
    if (drum_store_dirty && !backup_hold && !seq_playing && !rec_armed &&
        ui.conf_type == CONF_NONE && now >= ui.boot_until &&
        (now - drum_edit_ms) >= 1500u && sf_dsp_active_voices() == 0u) {
        set.magic = SET_MAGIC;
        if (plat_store_save(OBJ_SETTINGS, &set, (uint32_t)sizeof(set)) == 0)
            drum_store_dirty = 0u;
    }
    {
        uint32_t draw_int = (sf_dsp_active_voices() >= 4u && now >= ui.boot_until && now >= ui.hud_until && ui.conf_type == CONF_NONE) ? 55u : 33u;
        if (ui.redraw || (now - ui.last_draw_ms) >= draw_int) {
            ui.redraw = 0u;
            ui.last_draw_ms = now;
            draw_screen(now);
        }
    }
    update_leds(now);
}

void app_input_only(void)
{
    input(plat_ms());
}

void app_say(const char *a, const char *b)
{
    (void)a;
    (void)b;
}

int app_backup_playing(void) { return seq_playing ? 1 : 0; }
void app_backup_flush(void) { if (set_dirty) save_settings_to_store(); }
void app_backup_hold(int on) { backup_hold = on ? 1u : 0u; }
void app_backup_restored(void) { load_all_from_store(); ui.redraw = 1u; }

int app_expect(const char *what, int *got)
{
    if (!what || !got) return 0;
    uint32_t i;
    static const struct { const char *k; uint8_t id; } MAP[] = {
        {"bank", 0}, {"prog", 1}, {"octave", 2}, {"dirty", 3}, {"page", 4},
        {"playing", 5}, {"arp", 6}, {"note", 7}, {"vel", 8}, {"prs", 9},
        {"turns5", 10}, {"bpm", 11}, {"sub", 12}, {"drum_vol", 13}, {"drum_lvl0", 14},
        {"rec_quant", 15}, {"loop_cnt", 16}, {"palette", 17}, {"drum_loop_bars", 18},
        {"synth_vol", 19}, {"duck_amt", 20}, {"midi_synth_ch", 21}, {"midi_drum_ch", 22}
    };
    for (i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        const char *a = what, *b = MAP[i].k;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            switch (MAP[i].id) {
            case 0: *got = (int)set.bank; return 1;
            case 1: *got = (int)set.prog; return 1;
            case 2: *got = (int)set.octave; return 1;
            case 3: *got = (int)set_dirty; return 1;
            case 4: *got = (int)ui.page; return 1;
            case 5: *got = (int)seq_playing; return 1;
            case 6: *got = (int)arp_active; return 1;
            case 7: *got = (int)sf_dsp_last_note(); return 1;
            case 8: *got = (int)ui.last_vel; return 1;
            case 9: *got = (int)ui.last_prs; return 1;
            case 10: *got = (int)ui.k3_turns; return 1;
            case 11: *got = (int)set.bpm; return 1;
            case 12: *got = (int)ui.sub[ui.page]; return 1;
            case 13: *got = (int)set.drum_vol; return 1;
            case 14: *got = (int)set.drum_level[0]; return 1;
            case 15: *got = (int)set.rec_quant; return 1;
            case 16: *got = (int)synth_loop_cnt; return 1;
            case 17: *got = (int)set.palette; return 1;
            case 18: *got = (int)set.drum_loop_bars; return 1;
            case 19: *got = (int)set.synth_vol; return 1;
            case 20: *got = (int)set.duck_amt; return 1;
            case 21: *got = (int)set.midi_synth_ch; return 1;
            case 22: *got = (int)set.midi_drum_ch; return 1;
            }
        }
    }
    return 0;
}
