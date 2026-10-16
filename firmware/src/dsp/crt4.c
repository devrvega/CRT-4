/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (crt4.h). One compilation unit, -O2: this file includes the building blocks, the voices,
 * the consoles' engines, their effects and their sample bank, which are only ever part of it. */
#include "crt4.h"
#include "fastmath.h"

#ifdef EN_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------ shared state --- */
en_cfg_t en_cfg;
en_glob_t en_glob;
en_pat_t en_pat[EN_NPAT] EN_POOL;

volatile uint8_t en_playing, en_ext, en_recording, en_step, en_tick, en_pat_now, en_pat_next, en_chain_pos;
volatile uint8_t en_chord_now = 0xFF, en_fx_now, en_gates, en_con = CON_NES;
volatile uint32_t en_steps, en_rec_count;
volatile uint8_t en_trk_on[EN_NTRACK], en_trk_note[EN_NTRACK], en_trk_acc[EN_NTRACK], en_part_on[EN_NPART];
volatile float en_level[EN_NTRACK];
uint32_t (*en_time)(void);
volatile uint32_t en_prof[PR_N];
#ifdef CRT4_BENCH
volatile uint32_t en_prof_worst[PR_N + 1];      /* a development build: the slowest render's parts, then its whole */
#endif
static uint32_t prof_t;
static inline void prof(int k)                  /* the time since the last mark, to part k */
{
    if (en_time) {
        uint32_t t = en_time();
        en_prof[k] += t - prof_t;
        prof_t = t;
    }
}

static float tunefac = 1.0f;
static int chord_now = -1, chord_now_q = CQ_MAJ;
static int key_off(void)                       /* Key as a move of -5 .. +6 semitones */
{
    int k = clampi(en_cfg.par[P_KEY], 0, 11);
    return k <= 6 ? k : k - 12;
}
static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static char *itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static void scopy(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

#include "crt4_bank.h"
static inline const bk_smp_t *bank_smp(int i) { return &BK[(unsigned)i < (unsigned)BK_N ? i : 0]; }
#include "crt4_dsp.h"
#include "crt4_voice.c"
#include "crt4_chip.h"
#include "crt4_cons.c"

/* ------------------------------------------------------------------ tables --- */
const char *const EN_FX_NAME[EN_NFX + 1] = {"", "Repeat 1/16", "Repeat 1/8", "Repeat 1/4", "Stutter", "Reverse",
                                            "Half speed", "Glitch", "Fill", "Octave up", "Octave down",
                                            "Solo drums", "Solo tracks", "Fade", "Pitch dive", "Vibrato", "Echo"};

const en_chord_t EN_CHORD[EN_NCHORD] = {
    {"C", 0, CQ_MAJ},     {"Dm", 2, CQ_MIN},    {"Em", 4, CQ_MIN},    {"F", 5, CQ_MAJ},
    {"G", 7, CQ_MAJ},     {"Am", 9, CQ_MIN},    {"Bb", 10, CQ_MAJ},   {"E", 4, CQ_MAJ},
    {"Cmaj7", 0, CQ_MAJ7}, {"Dm7", 2, CQ_MIN7}, {"Em7", 4, CQ_MIN7},  {"Fmaj7", 5, CQ_MAJ7},
    {"G7", 7, CQ_DOM7},   {"Am7", 9, CQ_MIN7},  {"Fm", 5, CQ_MIN},    {"Gsus4", 7, CQ_SUS4},
};
static const char *const CQ_SUFFIX[7] = {"", "m", "dim", "sus4", "7", "maj7", "m7"};
void en_chord_name(int c, int key, char *b)
{
    const en_chord_t *ch = &EN_CHORD[(unsigned)c % EN_NCHORD];
    const char *r = EN_NOTE_NAME[(ch->root + key) % 12], *s = CQ_SUFFIX[ch->q];
    int i = 0;
    while (*r && i < 7)
        b[i++] = *r++;
    while (*s && i < 7)
        b[i++] = *s++;
    b[i] = 0;
}
int en_chord_tones(int c, int key, int *pc)
{
    static const int8_t T[7][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1}, {0, 5, 7, -1},
                                   {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}};
    const en_chord_t *ch = &EN_CHORD[(unsigned)c % EN_NCHORD];
    int i, n = 0;
    for (i = 0; i < 4; i++)
        if (T[ch->q][i] >= 0)
            pc[n++] = (ch->root + key + T[ch->q][i]) % 12;
    return n;
}

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 40, 240, 120}, {"Swing", 0, 100, 0}, {"Key", 0, 11, 0},
    {"FX 1", 0, 100, 0},     {"FX 2", 0, 100, 0},  {"FX 3", 0, 100, 0}, {"FX 4", 0, 100, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const en_param_t TPARAMS[TP_N] = {
    {"Sound", 0, EN_NSOUND - 1, 0}, {"Level", 0, 100, 80}, {"Pan", 0, 100, 50}, {"Echo", 0, 100, 0},
    {"Glide", 0, 100, 0},           {"Follow", 0, 1, 1},
};
const en_param_t *en_tparam_info(int i) { return (i >= 0 && i < TP_N) ? &TPARAMS[i] : &TPARAMS[0]; }
static const en_param_t GLOBS[G_N] = {{"Tune", -50, 50, 0}, {"MIDI out", 0, 1, 0}, {"Clock", 0, 1, 0}};
const en_param_t *en_glob_info(int i) { return (i >= 0 && i < G_N) ? &GLOBS[i] : &GLOBS[0]; }

void en_param_text(int i, int v, char *b)
{
    switch (i) {
    case P_SWING:
        itoa_s(50 + v / 4, b);
        scopy(b + 2, "%", 2);
        break;
    case P_KEY: scopy(b, EN_NOTE_NAME[clampi(v, 0, 11)], 10); break;
    case P_FX1:
    case P_FX2:
    case P_FX3:
    case P_FX4: en_fx_text(en_con, i - P_FX1, v, b); break;
    default: itoa_s(v, b); break;
    }
}
void en_glob_text(int i, int v, char *b)
{
    switch (i) {
    case G_TUNE: {
        char *e = itoa_s(v, b);
        if (v > 0) {
            int k;
            for (k = (int)(e - b); k >= 0; k--)
                b[k + 1] = b[k];
            b[0] = '+';
        }
        break;
    }
    case G_MIDI: scopy(b, v ? "On" : "Off", 10); break;
    case G_CLOCK: scopy(b, v ? "Internal" : "Auto", 10); break;
    default: itoa_s(v, b); break;
    }
}
void en_tparam_text(int t, int i, int v, char *b)
{
    switch (i) {
    case TP_SOUND: scopy(b, t == 0 ? en_kit_name(en_con, v % EN_NKIT) : en_sound(en_con, v % EN_NSOUND)->name, 10); break;
    case TP_PAN: {
        int mode = en_console(en_con)->pan;
        if (mode == PAN_MONO) {
            scopy(b, "Mono", 10);
        } else if (mode == PAN_LCR) {
            scopy(b, v < 34 ? "Left" : v > 66 ? "Right" : "Both", 10);
        } else if (v == 50) {
            scopy(b, "C", 10);
        } else {
            b[0] = v < 50 ? 'L' : 'R';
            itoa_s(v < 50 ? 50 - v : v - 50, b + 1);
        }
        break;
    }
    case TP_FOLLOW: scopy(b, v ? "On" : "Off", 10); break;
    default: itoa_s(v, b); break;
    }
}

/* ------------------------------------------------------- settings, patterns --- */
void en_pat_clear(en_pat_t *p)
{
    int t, s;
    uint8_t *b = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < sizeof *p; i++)
        b[i] = 0;
    for (s = 0; s < EN_NSTEP; s++) {
        p->dlock[0][s] = p->dlock[1][s] = EN_NOLOCK;
        for (t = 0; t < EN_NSYN; t++) {
            p->syn[t][s].len = 4;
            p->syn[t][s].lock[0] = p->syn[t][s].lock[1] = EN_NOLOCK;
        }
    }
    p->len = EN_NSTEP;
}

void en_cfg_defaults(int con)
{
    uint8_t *b = (uint8_t *)&en_cfg;
    uint32_t i;
    int t, s, k, p;
    const en_console_t *c = en_console(con);
    for (i = 0; i < sizeof en_cfg; i++)
        b[i] = 0;
    en_cfg.con = (uint8_t)con;
    for (i = 0; i < P_NPARAMS; i++)
        en_cfg.par[i] = PARAMS[i].def;
    for (i = 0; i < 4; i++)
        en_cfg.par[P_FX1 + i] = c->fx_def[i];
    for (t = 0; t < EN_NTRACK; t++) {
        for (i = 0; i < TP_N; i++)
            en_cfg.tp[t][i] = (uint8_t)TPARAMS[i].def;
    }
    en_cfg.tp[0][TP_SOUND] = 0;
    en_cfg.tp[1][TP_SOUND] = 0;          /* a bass, a key, a lead */
    en_cfg.tp[2][TP_SOUND] = 4;
    en_cfg.tp[3][TP_SOUND] = 8;
    en_cfg.tp[0][TP_LEVEL] = 85;
    for (t = 0; t < EN_NSYN; t++)
        for (s = 0; s < EN_NSOUND; s++)
            for (k = 0; k < 4; k++)
                en_cfg.sk[t][s][k] = en_sound(con, s)->def[k];
    for (k = 0; k < EN_NKIT; k++)
        for (p = 0; p < EN_NPART; p++)
            en_drum_def(con, k, p, en_cfg.dk[k][p]);
}
void en_glob_defaults(void)
{
    int i;
    for (i = 0; i < G_N; i++)
        en_glob.v[i] = GLOBS[i].def;
}

int en_cfg_valid(const en_cfg_t *c)
{
    int i, t;
    if (c->con >= EN_NCON)
        return 0;
    for (i = 0; i < P_NPARAMS; i++)
        if (c->par[i] < PARAMS[i].lo || c->par[i] > PARAMS[i].hi)
            return 0;
    for (t = 0; t < EN_NTRACK; t++)
        for (i = 0; i < TP_N; i++)
            if (c->tp[t][i] > (t == 0 && i == TP_SOUND ? EN_NKIT - 1 : TPARAMS[i].hi))
                return 0;
    for (i = 0; i < EN_NSYN * EN_NSOUND * 4; i++)
        if ((&c->sk[0][0][0])[i] > 100)
            return 0;
    for (i = 0; i < EN_NKIT * EN_NPART * 4; i++)
        if ((&c->dk[0][0][0])[i] > 100)
            return 0;
    if (c->chain_n > EN_CHAIN_MAX || c->chords_n > EN_CHAIN_MAX || c->pat >= EN_NPAT)
        return 0;
    for (i = 0; i < c->chain_n; i++)
        if (c->chain[i] >= EN_NPAT)
            return 0;
    for (i = 0; i < c->chords_n; i++)
        if (c->chords[i] >= EN_NCHORD)
            return 0;
    return 1;
}

int en_pat_valid(const en_pat_t *p)
{
    int t, s, k;
    if (p->len < 1 || p->len > EN_NSTEP)
        return 0;
    for (s = 0; s < EN_NSTEP; s++) {
        if (p->fx[s] > EN_NFX || (p->dlock[0][s] > 100 && p->dlock[0][s] != EN_NOLOCK) ||
            (p->dlock[1][s] > 100 && p->dlock[1][s] != EN_NOLOCK))
            return 0;
        for (t = 0; t < EN_NSYN; t++) {
            const en_step_t *e = &p->syn[t][s];
            if (e->len < 1 || e->len > 64)
                return 0;
            for (k = 0; k < EN_NNOTE; k++)
                if (e->note[k] > 127)
                    return 0;
            for (k = 0; k < 2; k++)
                if (e->lock[k] > 100 && e->lock[k] != EN_NOLOCK)
                    return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- commands --- */
enum { C_PLAY, C_PATTERN, C_CHAIN, C_NOTE, C_HIT, C_REC, C_FX, C_KNOB, C_PANIC, C_CLOCK, C_CON };
#define QN 128u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_play(int on) { post(C_PLAY, on, 0); }
void en_pattern(int p) { post(C_PATTERN, p, 0); }
void en_chain_restart(void) { post(C_CHAIN, 0, 0); }
void en_note(int trk, int note, int on, int acc) { post(C_NOTE, trk, (note & 0x7F) | (on ? 0x80 : 0) | (acc ? 0x100 : 0)); }
void en_hit(int part, int acc) { post(C_HIT, part, acc); }
void en_rec(int on) { post(C_REC, on, 0); }
void en_fx(int fx) { post(C_FX, fx, 0); }
void en_knob(int trk, int k) { post(C_KNOB, trk, k); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_set_console(int con) { post(C_CON, con, 0); }

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (en_glob.v[G_MIDI])
        en_midi_out(st, d1, d2);
}

/* ================================================================ the sequencer === */
/* 24 ticks a step. A step's events are scheduled at its start (later for an off-beat with swing, again for
 * a ratchet or a stutter) and fire on their tick; the render splits its blocks on ticks. */
#define TPS 24
static float step_samples = 5512.5f, tick_left;
static uint32_t abs_tick;
static int vstep, vtick, started;                 /* the playhead (before any effect) */
static int pat_q = -1, chain_pos, chord_pos, chain_restart;
static int fx_live, fx_cur, fx_s0;
static int skip_step = -1;                        /* a live note already played what this step holds */
static uint16_t skip_drum;
static uint8_t skip_syn;
static uint8_t mg_mute;                           /* the tracks heard (mutes and solo effects) */
static float mg[EN_NTRACK] = {1.0f, 1.0f, 1.0f, 1.0f};

typedef struct {
    uint32_t at;
    uint8_t step, sub, rat, fx;
} ev_t;
#define NEV 24
static ev_t evq[NEV];
static int evn;

/* live recording: a key's note, where it went, when it began (to measure its length) */
static struct {
    uint8_t on, note, step, pat;
    uint32_t t0;
} recn[EN_NSYN][4];
static uint32_t rec_pass[EN_NSYN][EN_NSTEP];      /* the pass a step was last written in (chords in one pass) */
static uint32_t pass;
static int last_rec_step[EN_NSYN] = {-1, -1, -1};

/* MIDI notes out: their offs, counted in ticks */
static struct {
    uint8_t ch, note;
    int16_t left;
} moff[16];

static void tempo_set(void)
{
    step_samples = EN_SR * 60.0f / ((float)clampi(en_cfg.par[P_TEMPO], 40, 300) * 4.0f);
    tick_len = step_samples * (1.0f / (float)TPS);
}
/* the tracker echo's delay: Echo (the FX page's first knob) in sixteenths */
static float echo_samples(void)
{
    static const uint8_t SIX[6] = {1, 2, 3, 4, 6, 8};
    int z = (int)((float)clampi(en_cfg.par[P_FX1], 0, 100) * 0.01f * 5.99f);
    return step_samples * (float)SIX[z];
}

static const en_pat_t *patp(void) { return &en_pat[clampi(en_pat_now, 0, EN_NPAT - 1)]; }
static int pat_len(void) { return clampi(patp()->len, 1, EN_NSTEP); }

int en_transpose(int trk, int note)
{
    int n = note;
    if (trk >= 1 && trk < EN_NTRACK && en_cfg.tp[trk][TP_FOLLOW] && chord_now >= 0) {
        const en_chord_t *c = &EN_CHORD[chord_now];
        int pc = ((note % 12) + 12) % 12, r = c->root <= 6 ? c->root : c->root - 12, d = 0;
        switch (c->q) {                           /* the scale's notes, bent to the chord */
        case CQ_MIN: d = pc == 4 ? -1 : pc == 9 ? -1 : pc == 11 ? -1 : 0; break;
        case CQ_MIN7: d = pc == 4 ? -1 : pc == 11 ? -1 : 0; break;
        case CQ_DIM: d = pc == 4 ? -1 : pc == 7 ? -1 : pc == 9 ? -1 : pc == 11 ? -1 : 0; break;
        case CQ_SUS4: d = pc == 4 ? 1 : 0; break;
        case CQ_DOM7: d = pc == 11 ? -1 : 0; break;
        default: break;
        }
        n += d + r;
    }
    n += key_off();
    if (en_fx_now == FX_OCTUP)
        n += 12;
    else if (en_fx_now == FX_OCTDN)
        n -= 12;
    while (n < 12)
        n += 12;
    while (n > 120)
        n -= 12;
    return n;
}

static void ev_add(uint32_t at, int step, int sub, int rat, int fx)
{
    if (evn >= NEV)
        return;
    evq[evn].at = at;
    evq[evn].step = (uint8_t)step;
    evq[evn].sub = (uint8_t)sub;
    evq[evn].rat = (uint8_t)rat;
    evq[evn].fx = (uint8_t)fx;
    evn++;
}

static void midi_note(int ch, int note, int vel, int ticks)
{
    int i;
    if (!en_glob.v[G_MIDI])
        return;
    for (i = 0; i < 16; i++)
        if (moff[i].left <= 0) {
            moff[i].ch = (uint8_t)ch;
            moff[i].note = (uint8_t)note;
            moff[i].left = (int16_t)(ticks < 1 ? 1 : ticks);
            en_midi_out(0x90u | (uint32_t)ch, (uint32_t)note, (uint32_t)vel);
            return;
        }
}
static void midi_ticks(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (moff[i].left > 0 && --moff[i].left == 0)
            en_midi_out(0x80u | moff[i].ch, moff[i].note, 0);
}
static void midi_all_off(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (moff[i].left > 0) {
            moff[i].left = 0;
            en_midi_out(0x80u | moff[i].ch, moff[i].note, 0);
        }
}

static const uint8_t GM_DRUM[EN_NPART] = {36, 38, 41, 45, 48, 37, 39, 42, 46, 49, 56};

static void hit(int p, float vel, int tune_off, int dec_off)
{
    int kit = en_cfg.tp[0][TP_SOUND] % EN_NKIT;
    drum_fire(kit, p, vel, tune_off, dec_off);
    en_part_on[p]++;
    en_trk_on[0]++;
    en_trk_note[0] = (uint8_t)p;
    en_trk_acc[0] = vel > 0.9f;
    midi_note(9, GM_DRUM[p], (int)(vel * 127.0f), 2);
}

/* the drums of step s (sub: a ratchet's or a stutter's repeat) */
static void fire_drums(int s, int sub, int fx)
{
    const en_pat_t *p = patp();
    uint16_t hits = 0;
    int k, to = 0, dof = 0, fill = fx == FX_FILL;
    if (!(mg_mute & 1u))
        return;
    for (k = 0; k < EN_NPART; k++)
        if (p->hit[k] >> s & 1u)
            hits |= (uint16_t)(1u << k);
    if (fill) {                                   /* a fill: the kick as written, the snare on every step, the toms down */
        int x = vstep % 16;
        hits &= 1u | 1u << 7 | 1u << 9;
        hits |= 1u << 1;
        if (x >= 8 && x < 10)
            hits |= 1u << 4;
        else if (x >= 10 && x < 12)
            hits |= 1u << 3;
        else if (x >= 12 && x < 14)
            hits |= 1u << 2;
    }
    if (sub == 0 && vstep == skip_step)
        hits &= (uint16_t)~skip_drum;
    if (p->dlock[0][s] != EN_NOLOCK)
        to = p->dlock[0][s] - 50;
    if (p->dlock[1][s] != EN_NOLOCK)
        dof = p->dlock[1][s] - 50;
    for (k = 0; k < EN_NPART; k++)
        if (hits >> k & 1u) {
            float vel = (p->acc >> s & 1u) ? 1.0f : 0.72f;
            if (fill && k == 1)
                vel = 0.45f + 0.5f * (float)(vstep % 16) / 15.0f;
            if (sub)
                vel *= 0.8f;
            hit(k, vel, to, dof);
        }
}

/* a track's step s */
static void fire_syn(int t, int s, int sub, int rat, int fx)
{
    const en_pat_t *p = patp();
    const en_step_t *e = &p->syn[t][s];
    int notes[EN_NNOTE], nn = 0, k, acc = (e->fl & EN_F_ACC) != 0, slide = (e->fl & EN_F_SLIDE) != 0;
    int32_t gate;
    int16_t lock[2];
    float vel = acc ? 1.0f : 0.78f, ticks;
    if (!e->note[0] || !(mg_mute >> (t + 1) & 1u))
        return;
    if (sub == 0 && vstep == skip_step && (skip_syn >> t & 1u))
        return;
    for (k = 0; k < EN_NNOTE && e->note[k]; k++)
        notes[nn++] = en_transpose(t + 1, e->note[k]);
    ticks = (float)(e->len * 6);
    if (rat > 1 || fx == FX_STUTTER)
        ticks = fm_minf(ticks, (float)(TPS / (fx == FX_STUTTER ? 2 : rat)) * 0.55f);
    else if (slide)
        ticks += 4.0f;                            /* into the next note: legato, the slide */
    else
        ticks -= 2.0f;                            /* shut before the next */
    if (ticks < 2.0f)
        ticks = 2.0f;
    gate = (int32_t)(ticks * tick_len);
    lock[0] = e->lock[0] == EN_NOLOCK ? -1 : e->lock[0];
    lock[1] = e->lock[1] == EN_NOLOCK ? -1 : e->lock[1];
    if (sub)
        vel *= 0.85f;
    syn_notes_on(t, notes, nn, vel, acc, gate, lock, 0);
    if (sub == 0)
        echo_queue(t, notes, nn, vel, gate, 0);
    en_trk_on[t + 1]++;
    en_trk_note[t + 1] = (uint8_t)notes[0];
    en_trk_acc[t + 1] = (uint8_t)acc;
    for (k = 0; k < nn; k++)
        midi_note(t + 1, notes[k], (int)(vel * 127.0f), (int)ticks);   /* tracks 2-4 on channels 2-4 */
}

static void ev_fire(const ev_t *e)
{
    int t;
    fire_drums(e->step, e->sub, e->fx);
    for (t = 0; t < EN_NSYN; t++)
        fire_syn(t, e->step, e->sub, e->rat, e->fx);
}

/* the end of a pattern: the next one (queued, the chain's next, or the same), the next chord */
static void pattern_advance(void)
{
    if (pat_q >= 0) {
        en_pat_now = (uint8_t)pat_q;
        pat_q = -1;
        chain_pos = 0;
    } else if (chain_restart) {
        chain_pos = 0;
        en_pat_now = en_cfg.chain_n ? en_cfg.chain[0] : en_cfg.pat;
    } else if (en_cfg.chain_n) {
        chain_pos = (chain_pos + 1) % en_cfg.chain_n;
        en_pat_now = en_cfg.chain[chain_pos];
    } else {
        en_pat_now = en_cfg.pat;
    }
    if (en_cfg.chords_n) {
        chord_pos = chain_restart ? 0 : (chord_pos + 1) % en_cfg.chords_n;
        chord_now = en_cfg.chords[chord_pos] % EN_NCHORD;
    } else {
        chord_now = -1;
    }
    chain_restart = 0;
    en_pat_now %= EN_NPAT;
    en_chain_pos = (uint8_t)chain_pos;
}

static int next_pattern(void)
{
    if (pat_q >= 0)
        return pat_q;
    if (chain_restart)
        return en_cfg.chain_n ? en_cfg.chain[0] : en_cfg.pat;
    if (en_cfg.chain_n)
        return en_cfg.chain[(chain_pos + 1) % en_cfg.chain_n];
    return en_cfg.pat;
}

/* a step begins: which step plays (the effects move it), and when its events fire */
static void step_begin(void)
{
    en_pat_t *p = &en_pat[en_pat_now % EN_NPAT];
    int len = pat_len(), fx, ps = vstep, d, rat = 1, sw, t, trig = 1;
    if (en_recording && fx_live && en_playing)
        p->fx[vstep] = (uint8_t)fx_live;           /* an effect held while writing goes on the step */
    fx = fx_live ? fx_live : p->fx[vstep];
    if (fx != fx_cur) {
        fx_cur = fx;
        fx_s0 = vstep;
    }
    en_fx_now = (uint8_t)fx;
    d = ((vstep - fx_s0) % len + len) % len;
    switch (fx) {
    case FX_RPT16:
    case FX_STUTTER: ps = fx_s0; break;
    case FX_RPT8: ps = (fx_s0 + d % 2) % len; break;
    case FX_RPT4: ps = (fx_s0 + d % 4) % len; break;
    case FX_REVERSE: ps = ((fx_s0 - d) % len + len) % len; break;
    case FX_HALF:
        ps = (fx_s0 + d / 2) % len;
        trig = (d & 1) == 0;
        break;
    case FX_GLITCH:
        ps = (int)(rnd() % (uint32_t)len);
        rat = (rnd() & 3u) == 0 ? 2 : 1;
        break;
    default: break;
    }
    en_step = (uint8_t)ps;
    sw = (vstep & 1) ? (clampi(en_cfg.par[P_SWING], 0, 100) * 12 + 50) / 100 : 0;
    {   /* the ratchets: the most any track's step asks for */
        for (t = 0; t < EN_NSYN; t++)
            if (p->syn[t][ps].note[0] && EN_RATCHET(p->syn[t][ps].fl) > (unsigned)rat)
                rat = (int)EN_RATCHET(p->syn[t][ps].fl);
    }
    if (trig) {
        int k;
        ev_add(abs_tick + (uint32_t)sw, ps, 0, rat, fx);
        for (k = 1; k < rat; k++)
            ev_add(abs_tick + (uint32_t)(sw + k * TPS / rat), ps, k, rat, fx);
        if (fx == FX_STUTTER)
            ev_add(abs_tick + (uint32_t)(sw + TPS / 2), ps, 1, 2, fx);
    }
}

static void tick(void)
{
    int i;
    if (vtick == 0) {
        if (vstep == 0 && started)
            pattern_advance();
        started = 1;
        step_begin();
        en_pat_next = (uint8_t)next_pattern();
    }
    for (i = 0; i < evn;) {
        if ((int32_t)(abs_tick - evq[i].at) >= 0) {
            ev_t e = evq[i];
            evq[i] = evq[--evn];
            ev_fire(&e);
        } else {
            i++;
        }
    }
    midi_ticks();
    en_tick = (uint8_t)vtick;
    abs_tick++;
    if (++vtick >= TPS) {
        vtick = 0;
        if (vstep == skip_step) {
            skip_step = -1;
            skip_drum = 0;
            skip_syn = 0;
        }
        en_steps++;
        if (++vstep >= pat_len()) {
            vstep = 0;
            pass++;
        }
    }
    if (vtick % 4 == 0 && !en_ext)
        midi(0xF8, 0, 0);                         /* 24 a quarter: one every 4 ticks */
}

/* ------------------------------------------------------------ live input --- */
static int rec_step(void)                         /* where a note played now goes: this step, or the next */
{
    int len = pat_len();
    if (vtick < TPS / 2)
        return vstep;
    return (vstep + 1) % len;
}
static void mark_skip(int s, int trk_bit, int part_bit)
{
    if (s == vstep)
        return;                                   /* this step: it already fired */
    if (skip_step != s) {
        skip_step = s;
        skip_drum = 0;
        skip_syn = 0;
    }
    if (trk_bit >= 0)
        skip_syn |= (uint8_t)(1u << trk_bit);
    if (part_bit >= 0)
        skip_drum |= (uint16_t)(1u << part_bit);
}

static void live_hit(int part, int acc)
{
    if (part < 0 || part >= EN_NPART)
        return;
    hit(part, acc ? 1.0f : 0.72f, 0, 0);
    if (en_recording && en_playing) {
        en_pat_t *p = &en_pat[en_pat_now % EN_NPAT];
        int s = rec_step();
        p->hit[part] |= (uint16_t)(1u << s);
        if (acc)
            p->acc |= (uint16_t)(1u << s);
        mark_skip(s, -1, part);
        en_rec_count++;
    }
}

static void live_note(int t, int note, int on, int acc)
{
    st_t *T;
    int i, snd_note;
    if (t < 0 || t >= EN_NSYN)
        return;
    T = &trk[t];
    if (on) {
        int legato = T->held_n > 0;
        snd_note = en_transpose(t + 1, note);
        if (T->held_n < 8) {
            T->held_snd[T->held_n] = (uint8_t)snd_note;
            T->held[T->held_n++] = (uint8_t)note;
        }
        if (con_mono() && T->held_n > 1 && (((const drv_t *)snd_of_trk(t)->patch)->flags & DF_AUTOARP)) {
            int ns[EN_NNOTE], k, m = 0;            /* an arpeggio sound: the keys held run as one */
            for (k = 0; k < T->held_n && m < EN_NNOTE; k++)
                ns[m++] = T->held_snd[k];
            for (k = 1; k < m; k++) {
                int x = ns[k], j = k;
                while (j > 0 && ns[j - 1] > x) {
                    ns[j] = ns[j - 1];
                    j--;
                }
                ns[j] = x;
            }
            T->v[0].on = 0;                        /* (from the top, as a new chord) */
            syn_notes_on(t, ns, m, acc ? 1.0f : 0.8f, acc, -1, 0, 0);
        } else {
            syn_notes_on(t, &snd_note, 1, acc ? 1.0f : 0.8f, acc, -1, 0, 0);
        }
        en_trk_on[t + 1]++;
        en_trk_note[t + 1] = (uint8_t)snd_note;
        en_trk_acc[t + 1] = (uint8_t)acc;
        if (en_glob.v[G_MIDI])
            en_midi_out(0x90u | (uint32_t)(t + 1), (uint32_t)snd_note, acc ? 127u : 100u);
        if (en_recording && en_playing) {         /* into the pattern, on the nearest step */
            en_pat_t *p = &en_pat[en_pat_now % EN_NPAT];
            int s = rec_step();
            en_step_t *e = &p->syn[t][s];
            if (rec_pass[t][s] != pass + 1u || !e->note[0]) {   /* the first note on this step this pass */
                for (i = 0; i < EN_NNOTE; i++)
                    e->note[i] = 0;
                e->fl = 0;
                e->lock[0] = e->lock[1] = EN_NOLOCK;
            }
            for (i = 0; i < EN_NNOTE && e->note[i] && e->note[i] != note; i++)
                ;
            if (i < EN_NNOTE && !e->note[i]) {    /* in, in order */
                int j = i;
                while (j > 0 && e->note[j - 1] > note) {
                    e->note[j] = e->note[j - 1];
                    j--;
                }
                e->note[j] = (uint8_t)note;
            }
            e->len = 4;
            if (acc)
                e->fl |= EN_F_ACC;
            if (legato && last_rec_step[t] >= 0 && last_rec_step[t] != s)   /* a key over a key: the slide */
                p->syn[t][last_rec_step[t]].fl |= EN_F_SLIDE;
            rec_pass[t][s] = pass + 1u;
            last_rec_step[t] = s;
            mark_skip(s, t, -1);
            for (i = 0; i < 4; i++)
                if (!recn[t][i].on) {
                    recn[t][i].on = 1;
                    recn[t][i].note = (uint8_t)note;
                    recn[t][i].step = (uint8_t)s;
                    recn[t][i].pat = en_pat_now;
                    recn[t][i].t0 = abs_tick - (uint32_t)vtick + (s == vstep ? 0u : (uint32_t)TPS);
                    break;
                }
            en_rec_count++;
        }
    } else {
        snd_note = -1;
        for (i = 0; i < T->held_n; i++)           /* the note as it began, even if the key or chord moved since */
            if (T->held[i] == note) {
                int k;
                snd_note = T->held_snd[i];
                for (k = i; k + 1 < T->held_n; k++) {
                    T->held[k] = T->held[k + 1];
                    T->held_snd[k] = T->held_snd[k + 1];
                }
                T->held_n--;
                break;
            }
        if (snd_note < 0)
            return;
        syn_note_off(t, snd_note);
        if (en_glob.v[G_MIDI])
            en_midi_out(0x80u | (uint32_t)(t + 1), (uint32_t)snd_note, 0);
        for (i = 0; i < 4; i++)                   /* its length, if it was written */
            if (recn[t][i].on && recn[t][i].note == note) {
                en_step_t *e = &en_pat[recn[t][i].pat % EN_NPAT].syn[t][recn[t][i].step];
                int32_t dt = (int32_t)(abs_tick - recn[t][i].t0);
                int q4 = (int)((dt + 3) / 6);
                recn[t][i].on = 0;
                if (en_recording && e->note[0])
                    e->len = (uint8_t)clampi(q4, 1, 64);
            }
    }
}

static void knob_rec(int t, int k)                /* a knob turned while writing: its value on this step */
{
    if (!en_recording || !en_playing || t < 1 || t > EN_NSYN || k < 0 || k > 1)
        return;
    {
        en_pat_t *p = &en_pat[en_pat_now % EN_NPAT];
        int s = rec_step(), snd = en_cfg.tp[t][TP_SOUND] % EN_NSOUND;
        p->syn[t - 1][s].lock[k] = en_cfg.sk[t - 1][snd][k];
    }
}

/* ------------------------------------------------------------- transport --- */
static int ext_pulses;                            /* MIDI clock: ticks still owed */
static float ext_iv = 920.0f, ext_since;          /* samples between clock pulses */
static uint32_t ext_age;

static void quiet_all(void)
{
    int t;
    drums_quiet();
    for (t = 0; t < EN_NSYN; t++)
        trk_quiet(t);
    midi_all_off();
}

static void play_start(void)
{
    started = 0;
    vstep = vtick = 0;
    evn = 0;
    skip_step = -1;
    fx_cur = 0;
    pat_q = -1;
    chain_pos = 0;
    chain_restart = 0;
    en_pat_now = (uint8_t)(en_cfg.chain_n ? en_cfg.chain[0] : en_cfg.pat) % EN_NPAT;
    chord_pos = 0;
    chord_now = en_cfg.chords_n ? en_cfg.chords[0] % EN_NCHORD : -1;
    en_chain_pos = 0;
    tick_left = 0.0f;
    ext_pulses = 0;
    pass++;
    en_playing = 1;
    en_steps = 0;
    if (!en_ext)
        midi(0xFA, 0, 0);
}

static void play_stop(void)
{
    int t;
    en_playing = 0;
    evn = 0;
    for (t = 0; t < EN_NSYN; t++)
        trk_quiet(t);
    if (!en_ext)
        midi(0xFC, 0, 0);
    midi_all_off();
}

static void console_set(int con);
static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_PLAY:
            if (a && !en_playing)
                play_start();
            else if (!a && en_playing)
                play_stop();
            break;
        case C_PATTERN:
            if (en_playing) {
                pat_q = a % EN_NPAT;
            } else {
                en_pat_now = (uint8_t)(a % EN_NPAT);
                vstep = 0;
            }
            break;
        case C_CHAIN:
            if (en_playing)
                chain_restart = 1;
            else
                en_pat_now = (uint8_t)((en_cfg.chain_n ? en_cfg.chain[0] : en_cfg.pat) % EN_NPAT);
            break;
        case C_NOTE: live_note(a - 1, b & 0x7F, (b & 0x80) != 0, (b & 0x100) != 0); break;
        case C_HIT: live_hit(a, b); break;
        case C_REC: en_recording = (uint8_t)(a != 0); break;
        case C_FX: fx_live = a <= EN_NFX ? a : 0; break;
        case C_KNOB: knob_rec(a, b); break;
        case C_PANIC: quiet_all(); break;
        case C_CON: console_set(a); break;
        case C_CLOCK:
            if (en_glob.v[G_CLOCK])
                break;                            /* Clock: Internal */
            en_ext = 1;
            ext_age = 0;
            if (a == EN_CLK_TICK) {
                if (en_playing) {
                    if (ext_pulses > 0)
                        while (ext_pulses-- > 0)  /* behind: catch up now */
                            tick();
                    ext_pulses = 4;
                    if (ext_since > 50.0f)
                        ext_iv += (ext_since - ext_iv) * 0.3f;
                    ext_since = 0.0f;
                    tick_left = 0.0f;
                }
            } else if (a == EN_CLK_START) {
                play_start();
            } else if (a == EN_CLK_CONTINUE) {
                if (!en_playing)
                    play_start();
            } else if (en_playing) {
                play_stop();
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ render --- */
/* The tracks run at the console's own rate (the SNES's 32 kHz, the N64's 22 kHz or 32: the rest at the
 * output's), mixed there with the console's effects and its output's filters, then brought to 44.1 kHz
 * (a cubic between the console's samples, as its DAC and its analog filter would round them). */
#define BLK 256
#define NBLK 264                                  /* a chunk at the console's rate (never more than the output's) */
static float mixl[NBLK], mixr[NBLK], tl[NBLK], tr[NBLK], sendl[NBLK], sendr[NBLK];
/* the rate converter: a 24-tap windowed sinc in 64 phases, its band ending at 0.43 of the console's rate
 * (what the console's own output filter left); the console's last RS_T samples, then a chunk's, in a row;
 * where between them */
#define RS_T 24
#define RS_P 64
static float rs_k[RS_P][RS_T];
static float rs_x[2 * (RS_T + NBLK)] EN_POOL, rs_f;      /* left and right, by turns */
static int rs_quiet;                              /* the history is silent: nothing to convert */
static void rs_init(void)
{
    int p, k;
    for (p = 0; p < RS_P; p++) {
        float sum = 0.0f;
        for (k = 0; k < RS_T; k++) {
            float t = (float)(k - (RS_T / 2 - 1)) - (float)p / (float)RS_P, x = 2.0f * 0.43f * t, w;
            float sn = fm_fabsf(x) < 1e-6f ? 1.0f : fm_sinf(FM_PI * x) / (FM_PI * x);
            w = 0.42f + 0.5f * fm_cosf(FM_PI * t / (float)(RS_T / 2)) + 0.08f * fm_cosf(2.0f * FM_PI * t / (float)(RS_T / 2));
            if (fm_fabsf(t) >= (float)(RS_T / 2))
                w = 0.0f;
            rs_k[p][k] = sn * w;
            sum += rs_k[p][k];
        }
        for (k = 0; k < RS_T; k++)
            rs_k[p][k] /= sum;
    }
}
static float fade_g = 1.0f;                       /* the Fade effect's level */

static float pan_of(int t)                        /* a track's pan as its console could */
{
    int v = en_cfg.tp[t + 1][TP_PAN];
    switch (en_console(en_con)->pan) {
    case PAN_MONO: return 0.0f;
    case PAN_LCR: return v < 34 ? -1.0f : v > 66 ? 1.0f : 0.0f;
    default: return ((float)v - 50.0f) * 0.02f;
    }
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j, m = n;
    int t, pan_ok = en_console(en_con)->pan != PAN_MONO;
    float step = ratio, sendmax = en_fx_now == FX_ECHO && en_playing ? 0.75f : 0.0f;
    if (ratio < 0.999f) {                         /* how many of the console's samples these n need */
        float f = rs_f;
        m = 0;
        for (j = 0; j < n; j++) {
            f += step;
            while (f >= 1.0f) {
                f -= 1.0f;
                m++;
            }
        }
    }
    for (j = 0; j < m; j++)
        mixl[j] = mixr[j] = sendl[j] = sendr[j] = 0.0f;
    for (t = 0; t < EN_NTRACK; t++) {
        float lv = (float)en_cfg.tp[t][TP_LEVEL] * 0.01f, g, ss;
        g = lv * lv * mg[t];
        ss = fm_maxf((float)en_cfg.tp[t][TP_SEND] * 0.01f, sendmax);
        if (con_echo_chip())
            ss = 0.0f;                            /* (the chip consoles' echo is the tracker's: more notes) */
        if (t == 0) {
            float pk = 0.0f;
            int any;
            prof(PR_OTHER);
            any = drums_run(m, tl, tr, pan_ok);
            prof(PR_DRUMS);
            if (!any) {
                en_level[0] *= 0.97f;
                continue;
            }
            for (j = 0; j < m; j++) {
                tl[j] *= g;
                tr[j] *= g;
            }
            for (j = 0; j < m; j += 4)
                pk = fm_maxf(pk, fm_fabsf(tl[j]) + fm_fabsf(tr[j]));
            en_level[0] = fm_maxf(pk * 0.8f, en_level[0] * 0.97f);
        } else {
            int any;
            for (j = 0; j < m; j++)
                tl[j] = tr[j] = 0.0f;
            prof(PR_OTHER);
            any = trk_run(t - 1, m, tl, tr, g);
            prof(PR_TRK1 + t - 1);
            if (!any)
                continue;
        }
        for (j = 0; j < m; j++) {
            mixl[j] += tl[j];
            mixr[j] += tr[j];
        }
        if (ss > 0.0f)
            for (j = 0; j < m; j++) {
                sendl[j] += tl[j] * ss;
                sendr[j] += tr[j] * ss;
            }
    }
    prof(PR_OTHER);
    con_out(mixl, mixr, sendl, sendr, m);         /* the console's effects and its output */
    prof(PR_SENDS);
    {   /* Fade: the master volume stepped down over two bars while it lasts (16 steps on the chip consoles) */
        float bar = step_samples * 16.0f;
        if (en_fx_now == FX_FADE && en_playing)
            fade_g = fm_maxf(fade_g - (float)n / (2.0f * bar), 0.0f);
        else
            fade_g = fm_minf(fade_g + (float)n / (0.05f * EN_SR), 1.0f);
    }
    {
        /* each console a little up or down, so that they sit near one another (their chips did not) */
        static const float TRIM[EN_NCON] = {0.74f, 1.42f, 0.87f, 1.0f, 1.33f, 1.55f, 1.0f, 0.82f, 1.0f};
        float fg = con_echo_chip() ? vol_q(fade_g, 15.0f) : fade_g, g = 0.5f * gain * fg * TRIM[en_con % EN_NCON];
        if (ratio < 0.999f) {                     /* to 44.1 kHz: the windowed sinc between the console's samples */
            float f = rs_f, pk = 0.0f;
            uint32_t k = 0;
            for (j = 0; j < m; j++) {
                rs_x[2 * (RS_T + j)] = mixl[j];
                rs_x[2 * (RS_T + j) + 1] = mixr[j];
                pk = fm_maxf(pk, fm_fabsf(mixl[j]) + fm_fabsf(mixr[j]));
            }
            if (pk < 1e-7f && rs_quiet) {         /* silence in, silence out (and the time to the screen) */
                for (j = 0; j < 2 * n; j++)
                    out[j] = 0;
                for (j = 0; j < n; j++) {
                    f += step;
                    while (f >= 1.0f)
                        f -= 1.0f;
                }
            } else {
                for (j = 0; j < n; j++) {
                    const float *xs, *c;
                    float ol = 0.0f, orr = 0.0f;
                    int i;
                    f += step;
                    while (f >= 1.0f) {
                        f -= 1.0f;
                        k++;
                    }
                    xs = rs_x + 2 * k;
                    c = rs_k[(int)(f * (float)RS_P) & (RS_P - 1)];
                    for (i = 0; i < RS_T; i++) {
                        ol += xs[2 * i] * c[i];
                        orr += xs[2 * i + 1] * c[i];
                    }
                    out[2 * j] = (int32_t)(sat(ol * g) * 8300000.0f);
                    out[2 * j + 1] = (int32_t)(sat(orr * g) * 8300000.0f);
                }
            }
            rs_f = f;
            for (j = 0; j < 2 * RS_T; j++)        /* the last RS_T, the next chunk's start */
                rs_x[j] = rs_x[2 * m + j];
            rs_quiet = pk < 1e-7f && m >= RS_T;
            if (rs_quiet)                         /* (a whole history's worth of it: the history is silent too) */
                for (j = 0; j < 2 * RS_T; j++)
                    rs_x[j] = 0.0f;
        } else {
            for (j = 0; j < n; j++) {
                out[2 * j] = (int32_t)(sat(mixl[j] * g) * 8300000.0f);
                out[2 * j + 1] = (int32_t)(sat(mixr[j] * g) * 8300000.0f);
            }
        }
    }
    prof(PR_MASTER);
}

static void control(uint32_t n)                   /* once a chunk: the slow things */
{
    int t;
    uint8_t heard = 0;
    for (t = 0; t < EN_NTRACK; t++) {
        int on = !(en_cfg.mute >> t & 1u);
        if (en_fx_now == FX_SOLODRUM && en_playing)
            on = on && t == 0;
        else if (en_fx_now == FX_SOLOSYN && en_playing)
            on = on && t != 0;
        if (on)
            heard |= (uint8_t)(1u << t);
        mg[t] += ((on ? 1.0f : 0.0f) - mg[t]) * fm_minf((float)n * (1.0f / (0.015f * EN_SR)), 1.0f);
    }
    mg_mute = heard;
    {
        uint8_t g = 0;
        for (t = 0; t < EN_NSYN; t++) {
            int i;
            for (i = 0; i < NV; i++)
                if (trk[t].v[i].on && trk[t].v[i].gate && !trk[t].v[i].echo)
                    g |= (uint8_t)(1u << (t + 1));
        }
        en_gates = g;
    }
    {   /* the punch-in effects that reach the voices: a dive (an octave a bar), a vibrato */
        float bar = step_samples * 16.0f;
        if (en_fx_now == FX_DIVE && en_playing)
            fx_dive = fm_minf(fx_dive + 12.0f * (float)n / bar, 36.0f);
        else
            fx_dive = fm_maxf(fx_dive - 96.0f * (float)n / EN_SR, 0.0f);
        fx_vib = en_fx_now == FX_VIBRATO && en_playing ? 1.0f : 0.0f;
        fx_vibph = wrap1(fx_vibph + 6.0f * (float)n / EN_SR);
    }
    tunefac = fm_exp2f((float)clampi(en_glob.v[G_TUNE], -50, 50) * (1.0f / 1200.0f));
    en_chord_now = chord_now < 0 ? 0xFFu : (uint8_t)chord_now;
    chord_now_q = chord_now < 0 ? CQ_MAJ : EN_CHORD[chord_now].q;
    tempo_set();
    con_control();
}

/* the governor: when a block takes most of its time, the tracks get fewer voices for new notes (four, three,
 * two); when it is easy again, they come back. Only where the app gives a clock (the device). */
static uint32_t gov_t0, gov_easy;
static void governor(uint32_t n)
{
    uint32_t spent, budget = (uint32_t)((float)n * (24000000.0f / EN_SR));   /* the device's clock: 24 MHz */
    if (!en_time || !n)
        return;
    spent = en_time() - gov_t0;
    if (spent * 100u > budget * 80u && poly_cap > 2) {
        poly_cap--;
        gov_easy = 0;
    } else if (spent * 100u < budget * 55u) {
        if (++gov_easy > 400u && poly_cap < NV) {            /* two seconds at ease */
            poly_cap++;
            gov_easy = 0;
        }
    } else {
        gov_easy = 0;
    }
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    uint32_t n0 = n;
#ifdef CRT4_BENCH
    uint32_t p0[PR_N], k0;
    for (k0 = 0; k0 < PR_N; k0++)
        p0[k0] = en_prof[k0];
#endif
    if (en_time)
        prof_t = gov_t0 = en_time();
    drain();
    if (en_ext && ++ext_age > 150u)               /* the clock stopped coming: our own again */
        en_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        prof(PR_OTHER);
        if (en_playing) {
            if (!en_ext) {
                if (tick_left <= 0.0f) {
                    tick();
                    tick_left += tick_len;
                }
                if ((float)k > tick_left)
                    k = (uint32_t)tick_left + 1u;
            } else if (ext_pulses > 0) {
                if (tick_left <= 0.0f) {
                    tick();
                    ext_pulses--;
                    tick_left += ext_iv * 0.25f;
                }
                if ((float)k > tick_left)
                    k = (uint32_t)tick_left + 1u;
            }
        }
        if (k > n)
            k = n;
        prof(PR_SEQ);
        if (en_playing)
            tick_left -= (float)k;
        ext_since += (float)k;
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
    governor(n0);
#ifdef CRT4_BENCH
    if (en_time) {
        uint32_t all = en_time() - gov_t0;
        if (all > en_prof_worst[PR_N]) {
            for (k0 = 0; k0 < PR_N; k0++)
                en_prof_worst[k0] = en_prof[k0] - p0[k0];
            en_prof_worst[PR_N] = all;
        }
    }
#endif
}

/* a new console: everything stops at once, its engine and effects from the start */
static void console_set(int con)
{
    int t, i;
    if (en_playing)
        play_stop();
    en_con = (uint8_t)((unsigned)con % EN_NCON);
    eng = con_engine(en_con);
    ratio = eng->rate / EN_SR;
    drums_quiet();
    for (t = 0; t < EN_NSYN; t++)
        trk_reset(t);
    for (i = 0; i < 2 * RS_T; i++)
        rs_x[i] = 0.0f;
    rs_f = 0.0f;
    rs_quiet = 1;
    fx_dive = 0.0f;
    con_reset();
}

void en_init(void)
{
    int i, t;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    q_r = q_w = 0;                     /* (what was queued goes: the sequencer's state with it) */
    fx_live = fx_cur = 0;
    en_fx_now = 0;
    fade_g = 1.0f;
    fx_vib = 0.0f;
    en_recording = 0;
    rs_init();
    cons_init();
    for (t = 0; t < EN_NTRACK; t++)
        mg[t] = 1.0f;
    mg_mute = 0x0F;
    console_set(en_cfg.con < EN_NCON ? en_cfg.con : CON_NES);
    tempo_set();
    en_playing = 0;
    en_recording = 0;
    en_chord_now = 0xFF;
}

#include "crt4_demo.c"

#ifdef EN_HOST
/* (the tests: what is still sounding, and why) */
#include <stdio.h>
void en_debug_voices(void)
{
    int t, i;
    for (t = 0; t < EN_NSYN; t++)
        for (i = 0; i < NV; i++) {
            const sv_t *v = &trk[t].v[i];
            if (v->on)
                printf("    trk %d v%d: note %d gate %d est %d elev %.3f echo %d fade %.2f pend %d t %d sm.est %d lvl %.4f\n", t, i,
                       v->note, v->gate, v->est, (double)v->elev, v->echo, (double)v->fade, v->pending, v->t, v->e.s.est,
                       (double)v->e.s.lvl);
        }
    for (i = 0; i < EN_NPART; i++)
        if (dv[i].on)
            printf("    drum %d: est %d elev %.3f\n", i, dv[i].est, (double)dv[i].elev);
    printf("    fade_g %.3f fx_dive %.2f fx_now %d\n", (double)fade_g, (double)fx_dive, en_fx_now);
}
#endif
