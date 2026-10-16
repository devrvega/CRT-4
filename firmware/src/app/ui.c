/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. The whole screen dresses as the console in use (crt_theme), its scene is that
 * console's (scenes.c).
 *
 *   ALGORITHM      the track: 1 drums, 2 3 4 the others    PRESETS   its sound (the kit, on the drums)
 *   GLO + ALGORITHM   the consoles, like cartridges on a shelf: let go of GLO and it switches
 *   PLAY           run / stop                              REC       write (live, or step by step stopped)
 *   keys           drums: black keys the parts (they play), white keys the part's 16 steps;
 *                  synths: a keyboard (Steps page: the white keys are the steps)
 *   HOME SEQ LFO ENV ARP SEL FX GLO   the pages: Patterns, Steps, Sound, Track, Chords, Mixer, FX, Setup;
 *                  held, HOME, ARP and FX turn the white keys into patterns, chords and effects at once
 *   hold a step    its knobs (note, length, A and B; the drums' tune and decay); OCT- slide, OCT+ ratchet
 *   ENV + a key    an accent                               EDIT + a key   clears it
 *   OCT- OCT+      the keyboard's octave (drums: the part)  SAVE   saves (and by itself, stopped)
 *   SELECT         the pages (HOME held: the tempo)         KNOB 1-4   the page's four values */
#include "../dsp/fastmath.h"

/* the pages; SELECT steps through them in this order (the panel's) */
enum { V_PATTERN, V_STEPS, V_SOUND, V_TRACK, V_CHORD, V_MIX, V_FX, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Patterns", "Steps", "Sound", "Track", "Chords", "Mixer", "FX", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, B_LFO, B_ENV, B_ARP, B_SEL, B_FX, B_GLO};

/* a knob's job: what kind of value (high byte) and which */
enum { K_NONE = 0, K_PAR = 0x100, K_TP = 0x200, K_SK = 0x300, K_LVL = 0x400, K_PLEN = 0x500, K_DEMO = 0x600,
       K_STEP = 0x700, K_LEDS = 0x800, K_GLOB = 0x900 };
static const uint16_t VIEW_KNOB[NVIEWS][4] = {
    {K_PAR | P_TEMPO, K_PAR | P_SWING, K_PLEN, K_PAR | P_KEY},
    {K_SK | 0, K_SK | 1, K_SK | 2, K_SK | 3},
    {K_SK | 0, K_SK | 1, K_SK | 2, K_SK | 3},
    {K_TP | TP_LEVEL, K_TP | TP_PAN, K_TP | TP_SEND, K_TP | TP_GLIDE},
    {K_TP | TP_FOLLOW, K_PAR | P_KEY, K_PAR | P_SWING, K_PAR | P_TEMPO},
    {K_LVL | 0, K_LVL | 1, K_LVL | 2, K_LVL | 3},
    {K_PAR | P_FX1, K_PAR | P_FX2, K_PAR | P_FX3, K_PAR | P_FX4},
    {K_GLOB | G_TUNE, K_GLOB | G_MIDI, K_GLOB | G_CLOCK, K_DEMO},
};

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used, keys_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t demo_done;                 /* SAVE held on Setup: the demo went in this hold */
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[5];
    /* a sequence of white keys being entered: a chain of patterns (1) or of chords (2) */
    uint8_t seq_kind, seq_n, seq_btn;
    uint8_t seq[EN_CHAIN_MAX];
    /* the keyboard */
    uint8_t key_note[NKEYS];           /* the note each held key plays, 0 none */
    uint8_t key_trk[NKEYS];
    uint8_t hand[EN_NSYN][EN_NNOTE];   /* the notes in hand: what a step gets when it is switched on */
    uint8_t hand_new;                  /* the next key starts a new hand (no key held) */
    uint8_t cursor;                    /* writing while stopped: the step the next note goes to */
    uint8_t cursor_hit;                /* ... and it has a note from the keys held now */
    uint8_t fx_key;                    /* the effect held, + 1 */
    uint8_t rec;                       /* writing (the engine's en_recording follows) */
    uint8_t shelf;                     /* GLO + ALGORITHM: the consoles on their shelf */
    int8_t shelf_pos;
    uint32_t boot_t;                   /* a console's power-on (its animation), since */
    uint8_t booting;                       /* writing (the engine's en_recording follows) */
    /* what the scenes need: when each track and part last played */
    uint8_t seen_on[EN_NTRACK], seen_part[EN_NPART];
    uint32_t note_t[EN_NTRACK], part_t[EN_NPART], ncount[EN_NTRACK];
} ui;

static int slen(const char *s)                   /* (the firmware's libc has no strlen) */
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void itoa_s2(int v, char *b)              /* with its sign */
{
    if (v > 0)
        *b++ = '+';
    else if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    itoa_u((uint32_t)v, b);
}
static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
static void pat_name(int p, char *b)             /* "P01" */
{
    b[0] = 'P';
    b[1] = (char)('0' + (p + 1) / 10);
    b[2] = (char)('0' + (p + 1) % 10);
    b[3] = 0;
}

static int ui_rec(void) { return ui.rec; }
static int trk(void) { return song.track; }
static int is_drum(void) { return song.track == 0; }
static int cur_pat(void) { return en_playing ? en_pat_now : en_cfg.pat; }
static en_pat_t *patp(void) { return &en_pat[cur_pat() % EN_NPAT]; }
static int snd_of(int t) { return en_cfg.tp[t][TP_SOUND]; }

static void mark_cfg(void) { project_dirty(PD_SONG); }
static void mark_glob(void) { project_dirty(PD_GLOB); }
static void mark_pat(int p) { project_dirty((int)PD_PAT(p)); }
int ui_dirty(void) { return project_is_dirty(); }

/* ------------------------------------------------------------- the keys --- */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int white_of(int k)                       /* key k is the i-th white key, or -1 */
{
    int i;
    for (i = 0; i < 16; i++)
        if (WHITE_KEY[i] == k)
            return i;
    return -1;
}
static int black_of(int k)
{
    int i;
    for (i = 0; i < 11; i++)
        if (BLACK_KEY[i] == k)
            return i;
    return -1;
}
static int key_midi(int k) { return 53 + k + 12 * song.oct[(trk() + EN_NSYN - 1) % EN_NSYN]; }   /* key 0 is F3 */

/* the steps held (white keys, on a page where they are steps) */
static uint16_t steps_held(void);

/* ------------------------------------------------------------ the knobs --- */
static int sk_kit(void) { return snd_of(0) % EN_NKIT; }
static int con(void) { return en_con; }


static int knob_get(int k)
{
    int i = k & 0xFF;
    switch (k & 0xFF00) {
    case K_PAR: return en_cfg.par[i];
    case K_GLOB: return en_glob.v[i];
    case K_TP: return en_cfg.tp[trk()][i];
    case K_SK:
        if (is_drum())
            return en_cfg.dk[sk_kit()][song.part][i];
        return en_cfg.sk[trk() - 1][snd_of(trk())][i];
    case K_LVL: return en_cfg.tp[i][TP_LEVEL];
    case K_PLEN: return patp()->len;
    case K_DEMO: return 0;
    case K_LEDS: return glob.leds;
    case K_STEP: {
        uint16_t h = steps_held();
        int s = 0;
        while (s < 15 && !(h >> s & 1u))
            s++;
        if (is_drum()) {
            int v = patp()->dlock[i & 1][s];
            return v == EN_NOLOCK ? 50 : v;
        } else {
            const en_step_t *e = &patp()->syn[trk() - 1][s];
            if (i == 0)
                return e->note[0] ? e->note[0] : ui.hand[trk() - 1][0];
            if (i == 1)
                return e->len;
            return e->lock[i - 2] == EN_NOLOCK ? en_cfg.sk[trk() - 1][snd_of(trk())][i - 2] : e->lock[i - 2];
        }
    }
    default: return 0;
    }
}
static void knob_range(int k, int *lo, int *hi)
{
    int i = k & 0xFF;
    *lo = 0;
    *hi = 100;
    switch (k & 0xFF00) {
    case K_PAR:
        *lo = en_param_info(i)->lo;
        *hi = en_param_info(i)->hi;
        break;
    case K_GLOB:
        *lo = en_glob_info(i)->lo;
        *hi = en_glob_info(i)->hi;
        break;
    case K_TP:
        *lo = en_tparam_info(i)->lo;
        *hi = en_tparam_info(i)->hi;
        break;
    case K_PLEN:
        *lo = 1;
        *hi = EN_NSTEP;
        break;
    case K_DEMO: *hi = 0; break;
    case K_LEDS: *hi = 2; break;
    case K_STEP:
        if (!is_drum() && i == 0) {
            *lo = 12;
            *hi = 120;
        } else if (!is_drum() && i == 1) {
            *lo = 1;
            *hi = 64;
        }
        break;
    case K_NONE: *hi = 0; break;
    default: break;
    }
}
static const char *knob_label(int k)
{
    int i = k & 0xFF;
    static const char *const LV[EN_NTRACK] = {"Drums", "Track 2", "Track 3", "Track 4"};
    switch (k & 0xFF00) {
    case K_PAR:
        if (i >= P_FX1 && i <= P_FX4)
            return en_console(con())->fx_knob[i - P_FX1];
        return en_param_info(i)->name;
    case K_GLOB: return en_glob_info(i)->name;
    case K_TP:
        if (i == TP_SEND)              /* what the track sends to: the console's echo, reverb or effect */
            return con() == CON_PS1 ? "Reverb" : con() == CON_N64 ? "Effect" : "Echo";
        return en_tparam_info(i)->name;
    case K_SK: return is_drum() ? EN_DRUM_KNOB[i] : en_sound(con(), snd_of(trk()))->knob[i];
    case K_LVL: return LV[i];
    case K_PLEN: return "Length";
    case K_DEMO: return "Demo";
    case K_LEDS: return "Lights";
    case K_STEP:
        if (is_drum())
            return i == 0 ? "Tune" : i == 1 ? "Decay" : "";
        if (i == 0)
            return "Note";
        if (i == 1)
            return "Length";
        return en_sound(con(), snd_of(trk()))->knob[i - 2];
    default: return "";
    }
}
static void knob_text(int k, char *b)
{
    int i = k & 0xFF, v = knob_get(k);
    switch (k & 0xFF00) {
    case K_PAR: en_param_text(i, v, b); break;
    case K_GLOB: en_glob_text(i, v, b); break;
    case K_TP: en_tparam_text(trk(), i, v, b); break;
    case K_SK:
        if (is_drum())
            en_drum_text(i, v, b);
        else
            en_sound_text(con(), snd_of(trk()), i, v, b);
        break;
    case K_LVL: itoa_u((uint32_t)v, b); break;
    case K_PLEN: itoa_u((uint32_t)v, b); break;
    case K_DEMO: {
        const char *n = EN_DEMO_NAME[con()];
        int j;
        for (j = 0; j < 13 && n[j]; j++)
            b[j] = n[j];
        b[j] = 0;
        break;
    }
    case K_LEDS: {
        const char *n = v == 2 ? "Keys" : v ? "On" : "Off";
        while ((*b++ = *n++))
            ;
        break;
    }
    case K_STEP:
        if (is_drum()) {
            itoa_s2(v - 50, b);
        } else if (i == 0) {
            const char *n = EN_NOTE_NAME[v % 12];
            while (*n)
                *b++ = *n++;
            itoa_s2(v / 12 - 1, b);
            if (b[0] == '+') {
                int j;
                for (j = 0; b[j]; j++)
                    b[j] = b[j + 1];
            }
        } else if (i == 1) {                    /* in steps: 1, 1.25 .. 16 */
            static const char *const Q[4] = {"", ".25", ".5", ".75"};
            const char *n;
            itoa_u((uint32_t)(v / 4), b);
            while (*b)
                b++;
            n = Q[v % 4];
            while ((*b++ = *n++))
                ;
        } else {
            en_sound_text(con(), snd_of(trk()), i - 2, v, b);
        }
        break;
    default: b[0] = 0; break;
    }
}
static int knob_zones(int k)                     /* a switch of a few positions: turned a position a detent */
{
    if ((k & 0xFF00) == K_PAR && (k & 0xFF) >= P_FX1 && (k & 0xFF) <= P_FX4)
        return en_console(con())->fx_zones[(k & 0xFF) - P_FX1];
    if (k == (K_TP | TP_PAN))
        return en_console(con())->pan == PAN_LCR ? 3 : en_console(con())->pan == PAN_MONO ? 1 : 0;
    if ((k & 0xFF00) == K_SK && !is_drum())
        return en_sound_zones(con(), snd_of(trk()), k & 0xFF);
    if ((k & 0xFF00) == K_STEP && !is_drum() && (k & 0xFF) >= 2)
        return en_sound_zones(con(), snd_of(trk()), (k & 0xFF) - 2);
    return 0;
}

static void set_step_knob(int i, int v);
static void knob_set(int k, int v)
{
    int lo, hi, i = k & 0xFF;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if ((k & 0xFF00) == K_STEP) {
        set_step_knob(i, v);
        return;
    }
    if (v == knob_get(k))
        return;
    switch (k & 0xFF00) {
    case K_PAR: en_cfg.par[i] = (int16_t)v; break;
    case K_GLOB:
        en_glob.v[i] = (int16_t)v;
        mark_glob();
        return;
    case K_TP: en_cfg.tp[trk()][i] = (uint8_t)v; break;
    case K_SK:
        if (is_drum())
            en_cfg.dk[sk_kit()][song.part][i] = (uint8_t)v;
        else
            en_cfg.sk[trk() - 1][snd_of(trk())][i] = (uint8_t)v;
        break;
    case K_LVL: en_cfg.tp[i][TP_LEVEL] = (uint8_t)v; break;
    case K_PLEN:
        patp()->len = (uint8_t)v;
        mark_pat(cur_pat());
        return;
    case K_DEMO:
        say("HOLD SAVE TO LOAD", 0);
        return;
    case K_LEDS:
        glob.leds = (uint8_t)v;
        mark_glob();
        return;
    default: return;
    }
    mark_cfg();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick; a longer quick turn
 * speeds up to 8 a detent. Small ranges never accelerate. */
static uint8_t accel_off;                        /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi, z;
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    if ((z = knob_zones(k)) == 1)                /* (a pan on a mono console: there is nowhere to go) */
        return;
    if (z > 1) {                                 /* the next position */
        int pos = (knob_get(k) * (z - 1) + 50) / 100 + (e > 0 ? 1 : -1);
        pos = pos < 0 ? 0 : pos >= z ? z - 1 : pos;
        knob_set(k, (pos * 100 + (z - 1) / 2) / (z - 1));
    } else {
        knob_set(k, knob_get(k) + accel(role, e, hi - lo));
    }
    if (ui.rec && en_playing && !is_drum() && (k == (K_SK | 0) || k == (K_SK | 1)))
        en_knob(trk(), k & 1);                   /* a knob turned while writing: on the step */
}

/* ------------------------------------------------------------ the steps --- */
static int step_page(void)                       /* the white keys are steps here */
{
    if (ui.btn >> B_HOME & 1u || ui.btn >> B_ARP & 1u || ui.btn >> B_FX & 1u)
        return 0;
    if (ui.view == V_STEPS)
        return 1;
    return is_drum() && (ui.view == V_SOUND || ui.view == V_TRACK || ui.view == V_SETUP);
}
static uint16_t steps_held(void)
{
    uint16_t h = 0;
    int i;
    if (!step_page())
        return 0;
    for (i = 0; i < 16; i++)
        if (ui.keys >> WHITE_KEY[i] & 1u)
            h |= (uint16_t)(1u << i);
    return h;
}

static void set_step_knob(int i, int v)          /* a held step's value (all the steps held) */
{
    uint16_t h = steps_held();
    en_pat_t *p = patp();
    int s;
    for (s = 0; s < 16; s++) {
        if (!(h >> s & 1u))
            continue;
        if (is_drum()) {
            if (i < 2)
                p->dlock[i][s] = (uint8_t)v;
        } else {
            en_step_t *e = &p->syn[trk() - 1][s];
            if (!e->note[0]) {                   /* an empty step held and turned: it gets the hand */
                int k;
                for (k = 0; k < EN_NNOTE; k++)
                    e->note[k] = ui.hand[trk() - 1][k];
                e->len = 4;
            }
            if (i == 0) {                        /* the note: the whole chord moves */
                int d = v - e->note[0], k;
                for (k = 0; k < EN_NNOTE && e->note[k]; k++)
                    e->note[k] = (uint8_t)(e->note[k] + d < 12 ? 12 : e->note[k] + d > 120 ? 120 : e->note[k] + d);
            } else if (i == 1) {
                e->len = (uint8_t)v;
            } else {
                e->lock[i - 2] = (uint8_t)v;
            }
        }
    }
    for (s = 0; s < 16; s++)
        if (ui.keys >> WHITE_KEY[s] & 1u && h >> s & 1u)
            ui.keys_used |= 1u << WHITE_KEY[s];  /* turned while held: no toggle when it is let go */
    mark_pat(cur_pat());
}

static void step_toggle(int s)
{
    en_pat_t *p = patp();
    if (is_drum()) {
        p->hit[song.part] ^= (uint16_t)(1u << s);
        if (!(p->hit[song.part] >> s & 1u)) {
            int k, any = 0;
            for (k = 0; k < EN_NPART; k++)
                any |= p->hit[k] >> s & 1u;
            if (!any)
                p->acc &= (uint16_t)~(1u << s);
        }
    } else {
        en_step_t *e = &p->syn[trk() - 1][s];
        int k;
        if (e->note[0]) {
            for (k = 0; k < EN_NNOTE; k++)
                e->note[k] = 0;
            e->fl = 0;
            e->lock[0] = e->lock[1] = EN_NOLOCK;
        } else {
            for (k = 0; k < EN_NNOTE; k++)
                e->note[k] = ui.hand[trk() - 1][k];
            e->len = 4;
            e->fl = 0;
            if (!en_playing)                     /* stopped: hear what went in */
                for (k = 0; k < EN_NNOTE && e->note[k]; k++)
                    en_note(trk(), e->note[k], 1, 0);
        }
    }
    mark_pat(cur_pat());
}

static void step_clear(int s)
{
    en_pat_t *p = patp();
    if (is_drum()) {
        int k;
        for (k = 0; k < EN_NPART; k++)
            p->hit[k] &= (uint16_t)~(1u << s);
        p->acc &= (uint16_t)~(1u << s);
        p->dlock[0][s] = p->dlock[1][s] = EN_NOLOCK;
    } else {
        en_step_t *e = &p->syn[trk() - 1][s];
        int k;
        for (k = 0; k < EN_NNOTE; k++)
            e->note[k] = 0;
        e->fl = 0;
        e->len = 4;
        e->lock[0] = e->lock[1] = EN_NOLOCK;
    }
    mark_pat(cur_pat());
}

static void step_accent(int s)
{
    en_pat_t *p = patp();
    if (is_drum()) {
        p->acc ^= (uint16_t)(1u << s);
        if (p->acc >> s & 1u)
            p->hit[song.part] |= (uint16_t)(1u << s);
    } else {
        en_step_t *e = &p->syn[trk() - 1][s];
        if (!e->note[0]) {
            int k;
            for (k = 0; k < EN_NNOTE; k++)
                e->note[k] = ui.hand[trk() - 1][k];
            e->len = 4;
        }
        e->fl ^= EN_F_ACC;
    }
    mark_pat(cur_pat());
}

static void clear_track(void)                    /* EDIT + REC: this track, in this pattern */
{
    en_pat_t *p = patp();
    int s;
    if (is_drum()) {
        int k;
        for (k = 0; k < EN_NPART; k++)
            p->hit[k] = 0;
        p->acc = 0;
        for (s = 0; s < EN_NSTEP; s++)
            p->dlock[0][s] = p->dlock[1][s] = EN_NOLOCK;
    } else {
        for (s = 0; s < EN_NSTEP; s++) {
            int k;
            en_step_t *e = &p->syn[trk() - 1][s];
            for (k = 0; k < EN_NNOTE; k++)
                e->note[k] = 0;
            e->fl = 0;
            e->len = 4;
            e->lock[0] = e->lock[1] = EN_NOLOCK;
        }
    }
    mark_pat(cur_pat());
}

/* ------------------------------------------------- patterns, chords, tracks --- */
static void seq_commit(void)
{
    char t[24];
    int i;
    if (!ui.seq_n) {
        ui.seq_kind = 0;
        return;
    }
    if (ui.seq_kind == 1) {
        if (ui.seq_n == 1) {
            en_cfg.pat = ui.seq[0];
            en_cfg.chain_n = 0;
            en_pattern(ui.seq[0]);
            pat_name(ui.seq[0], t);
            say("PATTERN", t);
        } else {
            for (i = 0; i < ui.seq_n; i++)
                en_cfg.chain[i] = ui.seq[i];
            en_cfg.chain_n = ui.seq_n;
            en_cfg.pat = ui.seq[0];
            en_chain_restart();
            itoa_u(ui.seq_n, t);
            say("CHAIN", t);
        }
    } else {
        for (i = 0; i < ui.seq_n; i++)
            en_cfg.chords[i] = ui.seq[i];
        en_cfg.chords_n = ui.seq_n;
        if (ui.seq_n == 1) {
            en_chord_name(ui.seq[0], en_cfg.par[P_KEY], t);
            say("CHORD", t);
        } else {
            itoa_u(ui.seq_n, t);
            say("CHORDS", t);
        }
        en_chain_restart();
    }
    mark_cfg();
    ui.seq_kind = 0;
    ui.seq_n = 0;
}

static void seq_key(int kind, int i)             /* a pattern or chord key: the first, or the next of a chain */
{
    if (ui.seq_kind != kind) {
        if (ui.seq_kind)
            seq_commit();
        ui.seq_kind = (uint8_t)kind;
        ui.seq_n = 0;
    }
    if (ui.seq_n < EN_CHAIN_MAX)
        ui.seq[ui.seq_n++] = (uint8_t)i;
}

static void set_track(int t)
{
    char b[16];
    t = (t % EN_NTRACK + EN_NTRACK) % EN_NTRACK;
    if (t != song.track) {
        song.track = (uint8_t)t;
        mark_cfg();
    }
    ui.touched = -1;
    if (t == 0) {
        b[0] = 0;
        caps(b, en_kit_name(con(), sk_kit()), sizeof b);
        say("DRUMS", b);
    } else {
        char n[4] = {'T', 'R', (char)('1' + t), 0};
        caps(b, en_sound(con(), snd_of(t))->name, sizeof b);
        say(n, b);
    }
}

static void set_sound(int d)
{
    char b[16], a[24];
    int t = trk(), n = t == 0 ? EN_NKIT : EN_NSOUND, s = ((int)snd_of(t) + d % n + n) % n;
    en_cfg.tp[t][TP_SOUND] = (uint8_t)s;
    mark_cfg();
    if (t == 0) {
        caps(b, en_kit_name(con(), s), sizeof b);
        say("KIT", b);
    } else {
        caps(b, en_sound(con(), s)->name, sizeof b);
        caps(a, en_sound(con(), s)->after, sizeof a);
        say(b, a);
    }
}

static void set_part(int p)
{
    char b[24];
    song.part = (uint8_t)((p % EN_NPART + EN_NPART) % EN_NPART);
    caps(b, en_part_long(con(), sk_kit(), song.part), sizeof b);
    say("PART", b);
    mark_cfg();
}

/* -------------------------------------------------------------- consoles --- */
static void scene_boot(void);
static void switch_console(int c)                /* stop, save this one's song, load that one's, power it on */
{
    char b[24];
    en_play(0);
    en_panic();
    if (project_console(c))
        say("NOT SAVED", "FLASH ERROR");
    en_set_console(c);
    ui.rec = 0;
    en_rec(0);
    ui.view = V_SOUND;
    ui.touched = -1;
    ui.sig[0] = ui.sig[1] = ui.sig[2] = ui.sig[3] = ui.sig[4] = 0;
    scene_boot();
    caps(b, en_console(c)->name, sizeof b);
    say(b, 0);
}
void ui_console(int c)                           /* the shelf's switch, asked from elsewhere (a development build) */
{
    if (c >= 0 && c < EN_NCON && c != en_cfg.con)
        switch_console(c);
}

/* ------------------------------------------------------------- buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void toggle_play(void)
{
    if (en_playing) {
        en_play(0);
        say("STOP", 0);
    } else {
        en_play(1);
        say("PLAY", 0);
    }
}

static void button(int b)                        /* a button let go, unless it was used with something */
{
    int v;
    for (v = 0; v < NVIEWS; v++)                 /* a page's button: its page; again: back to the sound */
        if (VIEW_BTN[v] == b) {
            set_view(ui.view == v && v != V_SOUND ? V_SOUND : v);
            return;
        }
    switch (b) {
    case B_REC:
        ui.rec ^= 1u;
        if (ui.rec) {
            en_rec(1);
            ui.cursor = 0;
            ui.cursor_hit = 0;
            say("WRITE", en_playing ? "LIVE" : (is_drum() ? "STEPS" : "STEP BY STEP"));
        } else {
            en_rec(0);
            say("WRITE OFF", 0);
        }
        break;
    case B_EDIT:
        if (ui_rec() && !en_playing && !is_drum()) {   /* writing step by step: a rest */
            ui.cursor = (uint8_t)((ui.cursor + 1) % patp()->len);
            say("REST", 0);
        }
        break;
    case B_OCTDN:
    case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        if (is_drum()) {
            set_part(song.part + d);
        } else {
            int8_t *o = &song.oct[trk() - 1];
            char t[8];
            *o = (int8_t)(*o + d < -3 ? -3 : *o + d > 3 ? 3 : *o + d);
            itoa_s2(*o, t);
            say("OCTAVE", t);
            mark_cfg();
        }
        break;
    }
    case B_SAVE:
        if (project_save() == 0)
            say("SAVED", 0);
        else
            say("NOT SAVED", "FLASH ERROR");
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): channels 1-4 play tracks 1-4 (the drums as General MIDI's kit);
 * clock and start / stop drive the steps */
static const uint8_t GM_PART[EN_NPART] = {36, 38, 41, 45, 48, 37, 39, 42, 46, 49, 56};
static uint32_t midi_rx_count;                   /* MIDI messages in (the device's SysEx status reports it) */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24, ch = st & 15u;
        midi_rx_count++;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        } else if ((st & 0xE0u) == 0x80u && ch < EN_NTRACK) {
            int on = (st & 0xF0u) == 0x90u && v;
            if (ch == 0 || ch == 9) {
                int p;
                for (p = 0; p < EN_NPART; p++)
                    if (GM_PART[p] == n && on)
                        en_hit(p, v > 100);
            } else {
                en_note((int)ch, (int)n, on, v > 100);
            }
        }
    }
}

/* HELP CARDS (X0X's): hold one button alone and a card says what it does */
#define HELP_HOLD 900u
#define DEMO_HOLD 1500u
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0 && !(i == B_SAVE && ui.view == V_SETUP)) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

/* ------------------------------------------------------------ key presses --- */
static void key_down(int k)
{
    int w = white_of(k), bk = black_of(k);
    uint32_t b = ui.btn;
    int accent = (b >> B_ENV & 1u) != 0, edit = (b >> B_EDIT & 1u) != 0;
    if (b >> B_FX & 1u || (ui.view == V_FX && !(b >> B_HOME & 1u) && !(b >> B_ARP & 1u))) {
        if (w >= 0) {                            /* an effect, while it is held */
            if (edit) {
                int s;
                for (s = 0; s < EN_NSTEP; s++)
                    if (patp()->fx[s] == w + 1)
                        patp()->fx[s] = 0;
                mark_pat(cur_pat());
                ui.btn_used |= 1u << B_EDIT;
                say("CLEARED", EN_FX_NAME[w + 1]);
            } else {
                char t[24];
                ui.fx_key = (uint8_t)(w + 1);
                en_fx(w + 1);
                caps(t, EN_FX_NAME[w + 1], sizeof t);
                say("FX", t);
            }
            ui.btn_used |= 1u << B_FX;
        }
        return;
    }
    if (b >> B_HOME & 1u || ui.view == V_PATTERN) {
        if (w >= 0) {
            ui.btn_used |= b & (1u << B_HOME | 1u << B_SAVE | 1u << B_EDIT);
            if (b >> B_SAVE & 1u) {              /* SAVE + a pattern: this pattern copied there */
                char t[8], u[24];
                en_pat[w] = *patp();
                mark_pat(w);
                pat_name(cur_pat(), u);
                pat_name(w, t);
                u[3] = ' ';
                u[4] = '>';
                u[5] = ' ';
                u[6] = t[0];
                u[7] = t[1];
                u[8] = t[2];
                u[9] = 0;
                say("COPIED", u);
            } else if (edit) {
                char t[8];
                en_pat_clear(&en_pat[w]);
                mark_pat(w);
                pat_name(w, t);
                say("CLEARED", t);
            } else {
                if (b >> B_HOME & 1u)
                    ui.seq_btn = B_HOME;
                seq_key(1, w);
            }
        }
        return;
    }
    if (b >> B_ARP & 1u || ui.view == V_CHORD) {
        if (w >= 0) {
            if (edit) {
                en_cfg.chords_n = 0;
                mark_cfg();
                ui.btn_used |= 1u << B_EDIT;
                say("CHORDS", "OFF");
            } else {
                if (b >> B_ARP & 1u) {
                    ui.seq_btn = B_ARP;
                    ui.btn_used |= 1u << B_ARP;
                }
                seq_key(2, w);
            }
        }
        return;
    }
    if (ui.view == V_MIX) {
        if (w >= 0 && w < EN_NTRACK) {
            char t[8] = {'T', 'R', (char)('1' + w), 0};
            en_cfg.mute ^= (uint8_t)(1u << w);
            say(en_cfg.mute >> w & 1u ? "MUTE" : "UNMUTE", w ? t : "DRUMS");
            mark_cfg();
        } else if (w >= 4 && w < 8) {
            set_track(w - 4);
        }
        return;
    }
    if (is_drum()) {
        if (bk >= 0) {                           /* a part: chosen, and played (written while writing) */
            if (edit) {
                patp()->hit[bk] = 0;
                mark_pat(cur_pat());
                ui.btn_used |= 1u << B_EDIT;
                {
                    char t[24];
                    caps(t, en_part_long(con(), sk_kit(), bk), sizeof t);
                    say("CLEARED", t);
                }
            } else {
                if (song.part != bk) {
                    song.part = (uint8_t)bk;
                    mark_cfg();
                }
                en_hit(bk, accent);
                if (accent)
                    ui.btn_used |= 1u << B_ENV;
            }
        } else if (w >= 0) {
            if (edit) {
                step_clear(w);
                ui.btn_used |= 1u << B_EDIT;
                ui.keys_used |= 1u << k;
            } else if (accent) {
                step_accent(w);
                ui.btn_used |= 1u << B_ENV;
                ui.keys_used |= 1u << k;
            }
            /* else: toggled when let go (unless its knobs were turned) */
        }
        return;
    }
    if (step_page()) {                           /* a synth track's steps */
        if (w >= 0) {
            if (edit) {
                step_clear(w);
                ui.btn_used |= 1u << B_EDIT;
                ui.keys_used |= 1u << k;
            } else if (accent) {
                step_accent(w);
                ui.btn_used |= 1u << B_ENV;
                ui.keys_used |= 1u << k;
            }
        }
        return;
    }
    {   /* the keyboard */
        int t = trk(), n = key_midi(k), i;
        if (n < 12 || n > 120)
            return;
        ui.key_note[k] = (uint8_t)n;
        ui.key_trk[k] = (uint8_t)t;
        en_note(t, n, 1, accent);
        if (accent)
            ui.btn_used |= 1u << B_ENV;
        if (ui.hand_new) {                       /* the hand: the notes held together */
            for (i = 0; i < EN_NNOTE; i++)
                ui.hand[t - 1][i] = 0;
            ui.hand_new = 0;
        }
        for (i = 0; i < EN_NNOTE && ui.hand[t - 1][i]; i++)
            ;
        if (i < EN_NNOTE) {
            int j = i;
            while (j > 0 && ui.hand[t - 1][j - 1] > n) {
                ui.hand[t - 1][j] = ui.hand[t - 1][j - 1];
                j--;
            }
            ui.hand[t - 1][j] = (uint8_t)n;
        }
        if (ui_rec() && !en_playing) {           /* writing step by step: the cursor's step */
            en_step_t *e = &patp()->syn[t - 1][ui.cursor % EN_NSTEP];
            if (!ui.cursor_hit) {
                for (i = 0; i < EN_NNOTE; i++)
                    e->note[i] = 0;
                e->fl = accent ? EN_F_ACC : 0;
                e->len = 4;
                e->lock[0] = e->lock[1] = EN_NOLOCK;
                ui.cursor_hit = 1;
            }
            for (i = 0; i < EN_NNOTE; i++)
                e->note[i] = ui.hand[t - 1][i];
            mark_pat(cur_pat());
        }
    }
}

static void key_up(int k)
{
    int w = white_of(k);
    if (ui.key_note[k]) {                        /* a keyboard note: off */
        en_note(ui.key_trk[k], ui.key_note[k], 0, 0);
        ui.key_note[k] = 0;
        if (!(ui.keys & ((1u << NKEYS) - 1u)))
            ui.hand_new = 1;
        if (ui_rec() && !en_playing && ui.cursor_hit && !(ui.keys & ((1u << NKEYS) - 1u))) {
            ui.cursor = (uint8_t)((ui.cursor + 1) % patp()->len);
            ui.cursor_hit = 0;
        }
        return;
    }
    if (ui.fx_key && w + 1 == ui.fx_key) {       /* the effect let go: another still held, or none */
        int i, next = 0;
        for (i = 0; i < 16; i++)
            if (ui.keys >> WHITE_KEY[i] & 1u)
                next = i + 1;
        ui.fx_key = (uint8_t)next;
        en_fx(next);
        return;
    }
    if (w >= 0 && step_page() && !(ui.keys_used >> k & 1u))
        step_toggle(w);
    ui.keys_used &= ~(1u << k);
    if (ui.seq_kind && !ui.seq_btn && !(ui.keys & ((1u << NKEYS) - 1u)))
        seq_commit();                            /* the page's keys all let go: the chain is in */
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    const uint32_t octs = 1u << B_OCTDN | 1u << B_OCTUP;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    if ((btn & octs) == octs)                   /* both held: the update mode's countdown */
        ui.btn_used |= octs;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if (i == B_SAVE)
                ui.demo_done = 0;
            if (i == B_PLAY) {
                toggle_play();
                if (btn >> B_FX & 1u)
                    ui.btn_used |= 1u << B_FX;
            }
            if (i == B_REC && (btn >> B_EDIT & 1u)) {   /* EDIT + REC: the track cleared */
                clear_track();
                ui.btn_used |= m | 1u << B_EDIT;
                say("CLEARED", is_drum() ? "DRUMS" : "TRACK");
            }
            if ((i == B_OCTDN || i == B_OCTUP) && steps_held() && !is_drum()) {   /* a held step: slide, ratchet */
                uint16_t h = steps_held();
                int s;
                for (s = 0; s < 16; s++)
                    if (h >> s & 1u) {
                        en_step_t *st = &patp()->syn[trk() - 1][s];
                        if (!st->note[0])
                            continue;
                        if (i == B_OCTDN)
                            st->fl ^= EN_F_SLIDE;
                        else
                            st->fl = (uint8_t)((st->fl & 0xCFu) | ((((st->fl >> 4) + 1u) & 3u) << 4));
                        {
                            static const char *const RAT[4] = {"X1", "X2", "X3", "X4"};
                            say(i == B_OCTDN ? (st->fl & EN_F_SLIDE ? "SLIDE" : "NO SLIDE") : "RATCHET",
                                i == B_OCTDN ? 0 : RAT[(st->fl >> 4) & 3u]);
                        }
                        ui.keys_used |= 1u << WHITE_KEY[s];
                    }
                mark_pat(cur_pat());
                ui.btn_used |= m;
            }
        } else {
            if (i == B_FX && ui.fx_key && !(btn >> B_FX & 1u) && ui.view != V_FX) {
                ui.fx_key = 0;                   /* FX let go: the effect too */
                en_fx(0);
            }
            if (ui.seq_kind && ui.seq_btn == i)  /* the chain's button let go: the chain is in */
                seq_commit(), ui.seq_btn = 0;
            if (i == B_GLO && ui.shelf) {        /* the shelf let go: that console in */
                ui.shelf = 0;
                if (ui.shelf_pos != con())
                    switch_console(ui.shelf_pos);
            }
            if (!(ui.btn_used & m) && i != B_PLAY)
                button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u)
                key_down((int)i);
            else
                key_up((int)i);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            char t[8];
            knob_set(K_PAR | P_TEMPO, en_cfg.par[P_TEMPO] + accel(EN_SELECT, e, 200));
            ui.btn_used |= 1u << B_HOME;
            itoa_u((uint32_t)en_cfg.par[P_TEMPO], t);
            say("TEMPO", t);
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (btn >> B_GLO & 1u) {                 /* GLO held: the consoles on their shelf */
            if (!ui.shelf) {
                ui.shelf = 1;
                ui.shelf_pos = (int8_t)con();
            }
            ui.shelf_pos = (int8_t)((ui.shelf_pos + (e > 0 ? 1 : -1) + EN_NCON) % EN_NCON);
            ui.btn_used |= 1u << B_GLO;
            ui.help = 0;
        } else {
            set_track(song.track + (e > 0 ? 1 : -1));
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_sound(e > 0 ? 1 : -1);
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && ui.view == V_SETUP && !ui.demo_done && plat_ms() - ui.down_t[B_SAVE] > DEMO_HOLD) {
        ui.demo_done = 1;                        /* SAVE held on Setup: the console's demo back (not saved yet) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        en_play(0);
        en_panic();
        project_defaults();
        project_dirty(PD_SONG | PD_PAT(0) | PD_PAT(BANK_PATS));
        {
            char t[16];
            caps(t, EN_DEMO_NAME[con()], sizeof t);
            say("DEMO", t);
        }
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            int k = VIEW_KNOB[ui.view][i];
            if (steps_held() && (ui.view == V_STEPS || ui.view == V_SOUND || (is_drum() && ui.view != V_MIX)))
                k = is_drum() ? (i < 2 ? (int)(K_STEP | i) : K_NONE) : (int)(K_STEP | i);
            turn(EN_K1 + (int)i, k, e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------ the screen --- */
/* each console's dress: the panel under the screen, the screen (a TV's, a Game Boy's), its ink, the tracks'
 * colours from the console's palette, its windows; the pixel font and pixel knobs (the 8- and 16-bit
 * consoles), or the smooth face and dials (the 32- and 64-bit ones); a CRT's scanlines on the scene */
typedef struct {
    uint16_t body, panel, line, dim, text;       /* under the screen */
    uint16_t bezel, scr, ink, mid, ghost;        /* the screen: its frame, its ground, lit, half, unlit */
    uint16_t trk[EN_NTRACK];
    uint16_t win, winb;                          /* a window (a card, the shelf): its ground, its border */
    uint8_t pixel;                               /* the pixel font and pixel knobs */
    uint8_t scan;                                /* scanlines on the scene: how dark (0: none) */
} theme_t;
static const theme_t THEMES[EN_NCON] = {
    {RGB(26, 19, 12), RGB(64, 44, 26), RGB(96, 68, 40), RGB(160, 128, 92), RGB(244, 224, 186),
     RGB(48, 34, 20), RGB(0, 0, 0), RGB(252, 188, 116), RGB(180, 120, 60), RGB(44, 30, 20),
     {RGB(252, 140, 76), RGB(108, 144, 252), RGB(236, 236, 104), RGB(236, 108, 196)}, RGB(0, 0, 0), RGB(252, 188, 116), 1, 70},
    {RGB(53, 40, 121), RGB(80, 66, 160), RGB(108, 94, 181), RGB(160, 150, 225), RGB(255, 255, 255),
     RGB(108, 94, 181), RGB(53, 40, 121), RGB(176, 166, 255), RGB(120, 108, 200), RGB(68, 56, 140),
     {RGB(255, 119, 119), RGB(170, 255, 238), RGB(238, 238, 119), RGB(204, 68, 204)}, RGB(53, 40, 121), RGB(176, 166, 255), 1, 50},
    {RGB(30, 30, 30), RGB(62, 62, 62), RGB(88, 88, 88), RGB(160, 160, 160), RGB(252, 252, 252),
     RGB(12, 12, 12), RGB(0, 0, 0), RGB(252, 252, 252), RGB(188, 188, 188), RGB(40, 40, 40),
     {RGB(248, 56, 0), RGB(60, 188, 252), RGB(248, 184, 0), RGB(248, 120, 248)}, RGB(0, 0, 0), RGB(252, 252, 252), 1, 70},
    {RGB(26, 26, 28), RGB(56, 56, 60), RGB(80, 80, 86), RGB(150, 150, 156), RGB(255, 255, 255),
     RGB(10, 10, 10), RGB(0, 0, 28), RGB(255, 255, 255), RGB(255, 136, 0), RGB(30, 30, 64),
     {RGB(255, 136, 0), RGB(0, 200, 255), RGB(255, 230, 0), RGB(255, 60, 120)}, RGB(0, 0, 48), RGB(255, 136, 0), 1, 60},
    {RGB(10, 10, 10), RGB(40, 40, 48), RGB(64, 64, 72), RGB(150, 150, 165), RGB(240, 240, 255),
     RGB(0, 0, 0), RGB(0, 0, 0), RGB(255, 255, 255), RGB(36, 108, 255), RGB(24, 24, 40),
     {RGB(255, 72, 72), RGB(36, 144, 255), RGB(255, 216, 0), RGB(72, 216, 72)}, RGB(0, 0, 80), RGB(36, 144, 255), 1, 80},
    {RGB(139, 172, 15), RGB(155, 188, 15), RGB(48, 98, 48), RGB(48, 98, 48), RGB(15, 56, 15),
     RGB(84, 88, 104), RGB(155, 188, 15), RGB(15, 56, 15), RGB(48, 98, 48), RGB(139, 172, 15),
     {RGB(15, 56, 15), RGB(48, 98, 48), RGB(15, 56, 15), RGB(48, 98, 48)}, RGB(155, 188, 15), RGB(15, 56, 15), 1, 0},
    {RGB(40, 36, 56), RGB(82, 72, 130), RGB(100, 92, 150), RGB(170, 164, 200), RGB(248, 248, 248),
     RGB(16, 14, 24), RGB(8, 8, 24), RGB(248, 248, 248), RGB(120, 112, 200), RGB(36, 32, 64),
     {RGB(230, 48, 64), RGB(240, 200, 40), RGB(60, 170, 90), RGB(60, 120, 230)}, RGB(24, 40, 140), RGB(248, 248, 248), 1, 50},
    {RGB(40, 40, 44), RGB(70, 72, 78), RGB(96, 98, 106), RGB(160, 162, 170), RGB(232, 234, 240),
     RGB(8, 8, 10), RGB(10, 10, 16), RGB(232, 234, 240), RGB(110, 130, 170), RGB(30, 32, 40),
     {RGB(64, 200, 160), RGB(232, 72, 72), RGB(232, 120, 200), RGB(90, 140, 232)}, RGB(20, 24, 40), RGB(160, 180, 220), 0, 36},
    {RGB(36, 36, 40), RGB(64, 64, 70), RGB(88, 88, 96), RGB(160, 160, 168), RGB(240, 240, 240),
     RGB(0, 0, 0), RGB(0, 0, 0), RGB(255, 255, 255), RGB(200, 200, 200), RGB(40, 40, 40),
     {RGB(230, 40, 40), RGB(40, 180, 60), RGB(250, 200, 0), RGB(40, 100, 230)}, RGB(0, 0, 60), RGB(250, 200, 0), 0, 24},
};
static const theme_t *th(void) { return &THEMES[en_con % EN_NCON]; }

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
/* a box as the theme draws them: pixel themes square, the others rounded */
static void tbox(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    if (th()->pixel)
        cv_rect(x, y, w, h, c);
    else
        cv_round(x, y, w, h, 3, c);
}
/* text as the theme writes it: the pixel font (capitals) or the smooth face; small (labels) or big (values) */
static int32_t ttext(int32_t x, int32_t y, int big, const char *s, uint16_t c)
{
    if (th()->pixel) {                           /* labels in capitals; values as they are (Bb3, 12.5%) */
        char u[32];
        if (!big) {
            caps(u, s, sizeof u);
            s = u;
        }
        return cv_ptext(x, y + (big ? 0 : 3), s, big ? 2 : 1, c);
    }
    return cv_text(x, y, big ? &FONT_B : &FONT_XS, s, c);
}
static int32_t ttext_w(int big, const char *s);
/* the same, keeping the text's case (a chord's name: Bb is not BB) */
static int32_t ttext_raw(int32_t x, int32_t y, const char *s, uint16_t c)
{
    if (th()->pixel)
        return cv_ptext(x, y + 3, s, 1, c);
    return cv_text(x, y, &FONT_XS, s, c);
}
static int32_t ttext_raw_w(const char *s) { return th()->pixel ? ptext_w(s, 1) : text_w(&FONT_XS, s); }
/* a name cut to fit w (a track's tab) */
static void tfit(char *s, int32_t w)
{
    int n = slen(s);
    while (n > 1 && ttext_w(0, s) > w)
        s[--n] = 0;
}
static int32_t ttext_w(int big, const char *s)
{
    if (th()->pixel) {
        char u[32];
        if (!big) {
            caps(u, s, sizeof u);
            s = u;
        }
        return ptext_w(s, big ? 2 : 1);
    }
    return text_w(big ? &FONT_B : &FONT_XS, s);
}
static void ttext_c(int32_t cx, int32_t y, int big, const char *s, uint16_t c) { ttext(cx - ttext_w(big, s) / 2, y, big, s, c); }

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc, and a knob's ring */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
        }
}
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg, int bipolar)
{
    const float th_ = 3.4f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th_, rm = ro - th_ * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f, lo, hi;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f;
    hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x, dy = (float)y, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        float el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th_ * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th_ * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, th_ * 0.5f, fill);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th_ * 0.5f + 1.3f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th_ * 0.5f + 0.6f, fill);
    }
}
/* the pixel themes' knob: a meter of eleven blocks (from the middle, both ways, when it is a +/- value) */
static void meter(int32_t cx, int32_t cy, float v, uint16_t off, uint16_t on, int bipolar)
{
    int k, n = 11, lit = (int)(fm_clampf(v, 0.0f, 1.0f) * 10.0f + 0.5f);
    for (k = 0; k < n; k++) {
        int on_ = bipolar ? ((lit >= 5 && k >= 5 && k <= lit) || (lit < 5 && k <= 5 && k >= lit)) : k <= lit && v > 0.0f;
        int32_t h = 4 + k;                       /* growing, as the old level meters drew them */
        cv_rect(cx - 27 + k * 5, cy + 7 - h, 4, h, on_ ? on : off);
    }
}
static int blink(void) { return (int)((plat_ms() / 160u) & 1u); }

/* the bands: the screen's top (status), its middle (the scene), its bottom (the steps), then the body below */
#define B0_Y 0
#define B0_H 25
#define B1_Y 25
#define B1_H 97
#define B2_Y 122
#define B2_H 30
#define B3_Y 152
#define B3_H 88
#define K0 (B3_Y + 20)                   /* the knobs' strip */
#define SCR_X 3
#define SCR_Y 3
#define SCR_W 234
#define SCR_H 147

static void band(int32_t y, int32_t h, uint16_t bg)   /* a band of the screen, drawn in screen co-ordinates */
{
    cv_begin(240, (uint32_t)h, bg);
    cv_oy = -y;
    cv_y0 = 0;
    cv_y1 = h;
}
static void screen_glass(void)                   /* the screen and its frame (a TV's bezel; the Game Boy's) */
{
    const theme_t *t = th();
    cv_round(SCR_X - 2, SCR_Y - 2, SCR_W + 4, SCR_H + 4, th()->pixel ? 2 : 6, t->bezel);
    cv_round(SCR_X, SCR_Y, SCR_W, SCR_H, th()->pixel ? 1 : 5, t->scr);
}

/* the status: the pattern (and the chain), the chord; writing, playing, the tempo. A message takes the
 * middle for a moment. */
static void draw_status(void)
{
    char t[24];
    const theme_t *T = th();
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, (uint32_t)cur_pat() | (uint32_t)en_pat_next << 8 | (uint32_t)en_cfg.chain_n << 16),
                           (uint32_t)en_playing | (uint32_t)ui.rec << 1 | (uint32_t)msg << 2 | (uint32_t)en_ext << 3 |
                               (uint32_t)song.track << 4 | (uint32_t)snd_of(trk()) << 8 | (uint32_t)en_chord_now << 16),
                      (uint32_t)en_cfg.par[P_TEMPO] | (uint32_t)en_cfg.chords_n << 10 | (uint32_t)en_chain_pos << 18 |
                          (uint32_t)ui_dirty() << 26 | (ui.view == V_SETUP ? (plat_cpu_pct() << 12) | 1u << 31 : 0u));
    h = hash(h, en_con);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1] | (uint32_t)ui.msg[1][0] << 16), ui.msg_until);
    if (en_playing)
        h = hash(h, (en_steps / 4u) & 1u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    band(B0_Y, B0_H, T->body);
    screen_glass();
    int32_t xl;                                  /* where the left part ends (a message starts there) */
    {   /* left: the pattern, the chain's place, the chord */
        int32_t x;
        pat_name(cur_pat(), t);
        x = ttext(9, 6, 0, t, T->ink);
        if (en_cfg.chain_n > 1) {
            char u[8];
            itoa_u((uint32_t)(en_playing ? en_chain_pos + 1 : 1), u);
            x = ttext(x + 3, 6, 0, u, T->mid);
            x = ttext(x, 6, 0, "/", T->mid);
            itoa_u(en_cfg.chain_n, u);
            x = ttext(x, 6, 0, u, T->mid);
        }
        xl = x;
        if (en_cfg.chords_n) {
            int c = en_playing && en_chord_now != 0xFF ? en_chord_now : en_cfg.chords[0];
            en_chord_name(c, en_cfg.par[P_KEY], t);
            tbox(x + 5, 6, ttext_raw_w(t) + 6, 14, T->ink);
            ttext_raw(x + 8, 6, t, T->scr);
        }
    }
    {   /* right: the tempo, playing / stopped, writing */
        int32_t x = 231;
        int bpm = en_cfg.par[P_TEMPO];
        if (en_ext) {
            x -= ttext_w(0, "EXT");
            ttext(x, 6, 0, "EXT", T->ink);
        } else {
            itoa_u((uint32_t)bpm, t);
            x -= ttext_w(0, t);
            ttext(x, 6, 0, t, T->ink);
        }
        x -= 13;
        if (en_playing) {                        /* a play triangle, lit on the beat */
            int j, on = ((en_steps / 4u) & 1u) == 0u;
            for (j = 0; j < 5; j++)
                cv_rect(x + j, 7 + j, 1, 11 - 2 * j, on ? T->ink : T->mid);
        } else {
            cv_rect(x, 8, 8, 8, T->ink);
        }
        x -= 27;
        tbox(x, 7, 23, 12, ui.rec ? T->trk[0] : T->ghost);   /* REC: lit, or its ghost */
        ttext(x + 2, 6, 0, "REC", ui.rec ? T->scr : T->mid);
        if (ui_dirty())
            cv_rect(x - 6, 12, 3, 3, T->mid);
        if (ui.view == V_SETUP) {
            char c[12] = "CPU ";
            itoa_u(plat_cpu_pct(), c + 4);
            ttext(x - 12 - ttext_w(0, c), 6, 0, c, T->mid);
        }
    }
    if (msg) {                                   /* the message: over all but the pattern, for a moment */
        int32_t w = ttext_w(0, ui.msg[0]) + (ui.msg[1][0] ? 6 + ttext_w(0, ui.msg[1]) : 0), x;
        cv_rect(xl + 3, 5, 234 - xl - 3, 17, T->scr);
        x = 140 - w / 2;
        if (x < xl + 6)
            x = xl + 6;
        x = ttext(x, 6, 0, ui.msg[0], T->ink);
        if (ui.msg[1][0])
            ttext(x + 6, 6, 0, ui.msg[1], T->mid);
    }
    cv_blit(0, B0_Y);
}


/* a button's card (help_tick): its name, then what it does */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("PATTERNS: THE WHITE KEYS"); L("HOLD + KEYS: A CHAIN"); L("+ SELECT: TEMPO"); L("SAVE + KEY: COPY HERE"); break;
    case B_SEQ: L("STEPS: THE WHITE KEYS"); L("HOLD A STEP + KNOBS"); L("NOTE, LENGTH, A, B"); L("OCT- SLIDE, OCT+ RATCHET"); break;
    case B_LFO: L("THE SOUND: ITS KNOBS"); L("PRESETS: ANOTHER SOUND"); L("ALGORITHM: THE TRACK"); break;
    case B_ENV: L("THE TRACK: LEVEL, PAN,"); L("ECHO OR REVERB, GLIDE"); L("HOLD + A KEY: ACCENT"); break;
    case B_ARP: L("CHORDS: THE WHITE KEYS"); L("HOLD + KEYS: A CHAIN"); L("A CHORD A PATTERN"); L("EDIT + KEY: CHORDS OFF"); break;
    case B_SEL: L("THE MIXER: LEVELS"); L("KEYS 1-4: MUTES"); L("KEYS 5-8: THE TRACK"); break;
    case B_FX: L("HOLD + A WHITE KEY:"); L("AN EFFECT, WHILE HELD"); L("WRITING: ON THE STEPS"); L("THE CONSOLE'S ECHO / REVERB"); break;
    case B_GLO: L("SETUP: TUNE, MIDI, CLOCK"); L("HOLD SAVE: THE DEMO"); L("HOLD + ALGORITHM:"); L("ANOTHER CONSOLE"); break;
    case B_EDIT: L("HOLD + A KEY: CLEAR IT"); L("+ REC: CLEAR THE TRACK"); L("WRITING STOPPED: A REST"); break;
    case B_REC: L("WRITE: KEYS, KNOBS A B"); L("AND EFFECTS GO IN"); L("STOPPED: STEP BY STEP"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("DRUMS: THE PART BEFORE"); L("A STEP HELD: SLIDE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("DRUMS: THE NEXT PART"); L("A STEP HELD: RATCHET"); break;
    case B_SAVE: L("SAVES (STOPPED, IT SAVES"); L("BY ITSELF TOO)"); L("HOLD + A PATTERN: COPY"); break;
    default: break;
    }
#undef L
    return n;
}

static void window(int32_t x, int32_t y, int32_t w, int32_t h)   /* a window in the console's manner */
{
    const theme_t *T = th();
    if (T->pixel) {                              /* a game's: a ground and a double border */
        cv_rect(x, y, w, h, T->winb);
        cv_rect(x + 2, y + 2, w - 4, h - 4, T->win);
        cv_rect(x + 4, y + 4, w - 8, 1, T->winb);
        cv_rect(x + 4, y + h - 5, w - 8, 1, T->winb);
        cv_rect(x + 4, y + 4, 1, h - 8, T->winb);
        cv_rect(x + w - 5, y + 4, 1, h - 8, T->winb);
    } else {
        cv_round(x, y, w, h, 5, T->winb);
        cv_round(x + 2, y + 2, w - 4, h - 4, 4, T->win);
    }
}
static void draw_help(void)
{
    const char *l[HELP_LINES];
    const theme_t *T = th();
    int n = help_lines(ui.help - 1, l), k;
    window(10, 29, 220, 90);
    ttext_c(120, 36, 1, BTN_LABEL[ui.help - 1], T->winb);
    for (k = 0; k < n; k++)
        ttext_c(120, 58 + k * 13, 0, l[k], T->text == T->winb ? T->ink : RGB(255, 255, 255));
}

extern void (*ui_mark)(int part);
#define UI_MARK_(p)                                                                                                   \
    do {                                                                                                              \
        if (ui_mark)                                                                                                  \
            ui_mark(p);                                                                                               \
    } while (0)
#include "scenes.c"

/* the steps: the track's sixteen (the drums: the part's, the others as dots), where the pattern is */
#define STEP_X 12
static int32_t step_x(int s) { return STEP_X + s * 13 + (s / 4) * 3; }
static void draw_steps(void)
{
    const en_pat_t *p = patp();
    const theme_t *T = th();
    uint32_t h = hash(2166136261u, (uint32_t)song.track | (uint32_t)song.part << 4 | (uint32_t)p->len << 8 |
                                       (uint32_t)(en_playing ? en_step : 0xFF) << 16 | (uint32_t)cur_pat() << 24);
    int s, len = p->len, play = en_playing ? en_step : -1, curs = ui_rec() && !en_playing && !is_drum() ? ui.cursor : -1;
    uint16_t held = steps_held(), c = T->trk[song.track];
    h = hash(h, (uint32_t)held | (uint32_t)(curs >= 0 ? blink() : 0) << 16 | (uint32_t)ui.view << 20 | (uint32_t)en_con << 24);
    if (is_drum()) {
        int k;
        for (k = 0; k < EN_NPART; k++)
            h = hash(h, p->hit[k]);
        h = hash(h, p->acc);
    } else {
        for (s = 0; s < EN_NSTEP; s++) {
            const en_step_t *e = &p->syn[trk() - 1][s];
            h = hash(h, (uint32_t)e->note[0] | (uint32_t)e->fl << 8 | (uint32_t)e->len << 16 | (uint32_t)e->lock[0] << 24);
        }
    }
    for (s = 0; s < EN_NSTEP; s++)
        h = hash(h, p->fx[s]);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    band(B2_Y, B2_H, T->body);
    screen_glass();
    if (en_con == CON_GB)                        /* (the Game Boy has no colours: its darkest) */
        c = T->ink;
    for (s = 0; s < EN_NSTEP; s++) {
        int32_t x = step_x(s), y = 127;
        int on = 0, acc = 0, other = 0, slide = 0, rat = 1, lock = 0;
        if (is_drum()) {
            int k;
            on = (p->hit[song.part] >> s & 1u) != 0;
            acc = on && (p->acc >> s & 1u);
            for (k = 0; k < EN_NPART; k++)
                if (k != song.part && (p->hit[k] >> s & 1u))
                    other++;
            lock = p->dlock[0][s] != EN_NOLOCK || p->dlock[1][s] != EN_NOLOCK;
        } else {
            const en_step_t *e = &p->syn[trk() - 1][s];
            on = e->note[0] != 0;
            acc = on && (e->fl & EN_F_ACC);
            slide = on && (e->fl & EN_F_SLIDE);
            rat = on ? (int)EN_RATCHET(e->fl) : 1;
            lock = on && (e->lock[0] != EN_NOLOCK || e->lock[1] != EN_NOLOCK);
            other = on && e->note[1] ? 2 : 0;
        }
        if (s >= len) {                          /* past the pattern's end */
            cv_rect(x + 2, y + 6, 7, 1, T->ghost);
            continue;
        }
        if (on) {
            tbox(x, acc ? y : y + 3, 11, acc ? 12 : 9, held >> s & 1u ? T->mid : c);
            if (rat > 1) {
                int r;
                for (r = 1; r < rat; r++)
                    cv_rect(x + r * 11 / rat, y + 4, 1, 6, T->scr);
            }
            if (other && !is_drum())
                cv_rect(x + 2, y + 5, 7, 1, T->scr);    /* a chord */
            if (slide)
                cv_rect(x + 11, y + 7, s % 4 == 3 ? 5 : 2, 2, c);
        } else {
            tbox(x, y + 3, 11, 9, held >> s & 1u ? T->mid : T->ghost);
        }
        if (is_drum() && other)                  /* the other parts on this step */
            cv_rect(x + 4, y + 14, 3, 2, T->mid);
        if (lock)
            cv_rect(x + 9, y - 2, 2, 2, T->ink);
        if (p->fx[s])
            cv_rect(x, y - 2, 4, 2, T->mid);
        cv_rect(x, y + 18, 11, 3, s == play ? T->ink : T->ghost);   /* the playhead */
        if (s == curs && blink())
            cv_rect(x - 1, y + 2, 13, 11, T->mid);
    }
    cv_blit(0, B2_Y);
}

/* the body: the four tracks (their sounds, what they play, muted), then the page's knobs */
/* the tracks' strip: each one's sound, its level as it plays (redrawn by itself: the levels move) */
#define B3_TH 19
static void draw_tracks(void)
{
    const theme_t *T = th();
    uint32_t h = hash(2166136261u, (uint32_t)song.track | (uint32_t)en_cfg.mute << 4 | (uint32_t)en_con << 8);
    int t;
    for (t = 0; t < EN_NTRACK; t++)
        h = hash(h, (uint32_t)snd_of(t) | (uint32_t)(en_level[t] * 8.0f) << 8);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    band(B3_Y, B3_TH, T->body);
    for (t = 0; t < EN_NTRACK; t++) {
        int32_t x = 3 + t * 59, w = 56, sel = t == song.track, mute = en_cfg.mute >> t & 1u;
        char n[12];
        int lv = (int)(fm_clampf(en_level[t], 0.0f, 1.0f) * (float)(w - 8));
        uint16_t tc = T->trk[t], on = sel ? (en_con == CON_GB ? T->text : tc) : T->panel;
        tbox(x, B3_Y + 1, w, 17, on);
        n[0] = (char)('1' + t);
        n[1] = 0;
        ttext(x + 4, B3_Y + 2, 0, n, sel ? T->body : (en_con == CON_GB ? T->text : tc));
        caps(n, t == 0 ? en_kit_name(en_con, snd_of(0) % EN_NKIT) : en_sound(en_con, snd_of(t))->name, sizeof n);
        tfit(n, w - 15);
        ttext(x + 13, B3_Y + 2, 0, n, mute ? T->dim : sel ? T->body : T->text);
        if (mute)
            cv_rect(x + 12, B3_Y + 9, w - 16, 1, sel ? T->body : T->dim);
        if (lv > 0 && !mute)
            cv_rect(x + 4, B3_Y + 15, lv, 2, sel ? T->body : (en_con == CON_GB ? T->text : tc));
    }
    cv_blit(0, B3_Y);
}

static void draw_knobs(void)
{
    const theme_t *T = th();
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)(uint8_t)ui.touched << 8 | (uint32_t)song.track << 12 | (uint32_t)en_cfg.mute << 16 |
                                       (uint32_t)song.part << 20 | (uint32_t)en_con << 26), i;
    int now_touch = plat_ms() < ui.touch_until, held = steps_held() != 0;
    uint16_t c = T->trk[song.track];
    draw_tracks();
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(held && (ui.view == V_STEPS || ui.view == V_SOUND || is_drum()) ? (int)(K_STEP | i) : VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch | (uint32_t)held << 1);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    band(B3_Y + B3_TH, B3_H - B3_TH, T->body);
    {   /* the rule; at its right end, where you are: the page, then its dots; at its left, the drum part */
        char tt[16];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt, x0 = 4;
        caps(tt, held ? (is_drum() ? "Step lock" : "Step") : VIEW_NAME[ui.view], sizeof tt);
        xt = xd - 5 - ttext_w(0, tt);
        if (is_drum() && (ui.view == V_SOUND || ui.view == V_STEPS || held)) {
            char pn[16];
            caps(pn, en_part_long(en_con, sk_kit(), song.part), sizeof pn);
            x0 = ttext(5, K0 - 1, 0, pn, T->text) + 6;
        } else if (ui.view == V_SETUP) {             /* Setup: which CRT-4 this is */
            x0 = ttext_raw(5, K0 - 1, "CRT-4 v" EN_VERSION, T->dim) + 6;
        }
        if (xt - 9 > x0)
            cv_rect(x0, K0 + 5, xt - 9 - x0, 1, T->line);
        ttext(xt, K0 - 1, 0, tt, en_con == CON_GB ? T->text : c);
        for (k = 0; k < NVIEWS; k++)
            tbox(xd + k * 6, K0 + 3, 4, 4, k == ui.view ? (en_con == CON_GB ? T->text : c) : T->line);
    }
    for (i = 0; i < 4; i++) {
        int k = held && (ui.view == V_STEPS || ui.view == V_SOUND || (is_drum() && ui.view != V_MIX))
                    ? (is_drum() ? (i < 2 ? (int)(K_STEP | i) : K_NONE) : (int)(K_STEP | i))
                    : VIEW_KNOB[ui.view][i];
        int lo, hi, v, hot = now_touch && ui.touched == (int)i, z;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char tt[16];
        uint16_t bg = hot ? T->panel : T->body, fill = hot ? (en_con == CON_GB ? T->text : c) : T->text;
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, K0 + 16, 1, 46, T->line);
        if (hot)
            tbox(x + 3, K0 + 11, 54, 57, T->panel);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(tt, knob_label(k), sizeof tt);
        if (!tt[0])
            continue;
        ttext_c(cx, K0 + 12, 0, tt, hot ? T->text : T->dim);
        z = knob_zones(k);
        if (z > 1 || hi - lo < 6) {              /* a switch: its positions as pips */
            int n = z > 1 ? z : hi - lo + 1, j, pw = n > 4 ? (n > 6 ? 4 : 6) : 9, gap = n > 6 ? 2 : 3, w0 = n * (pw + gap) - gap;
            int pos = z > 1 ? (v * (z - 1) + 50) / 100 : v - lo;
            if (n > 1)
                for (j = 0; j < n; j++)
                    tbox(cx - w0 / 2 + j * (pw + gap), K0 + 33, pw, 8, j == pos ? fill : T->line);
        } else {
            int bip = (k & 0xFF00) == K_GLOB && lo < 0;
            if ((k & 0xFF00) == K_TP && (k & 0xFF) == TP_PAN)
                bip = 1;
            if ((k & 0xFF00) == K_STEP && is_drum())
                bip = 1;
            if ((k & 0xFF00) == K_SK && is_drum() && (k & 0xFF) == 0)
                bip = 1;
            if (T->pixel)
                meter(cx, K0 + 34, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, T->line, fill, bip);
            else
                arc(cx, K0 + 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, T->line, fill, bg, bip);
        }
        knob_text(k, tt);
        if (ttext_w(1, tt) > 56) {               /* (a long value: the small size, as it is, cut to fit) */
            int n = slen(tt);
            while (n > 1 && ttext_raw_w(tt) > 58)
                tt[--n] = 0;
            ttext_raw(cx - ttext_raw_w(tt) / 2, K0 + 49, tt, hot ? fill : T->text);
        } else
            ttext_c(cx, K0 + 48, 1, tt, hot ? fill : T->text);
    }
    cv_blit(0, B3_Y + B3_TH);
}

/* ------------------------------------------------------------- the lights --- */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    const int bl = blink();
    const en_pat_t *p = patp();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.rec)
        b |= 1u << B_REC;
    if (ui.fx_key)
        b |= 1u << B_FX;
    if (!is_drum()) {
        if (song.oct[trk() - 1] > 0)
            b |= 1u << B_OCTUP;
        if (song.oct[trk() - 1] < 0)
            b |= 1u << B_OCTDN;
    }
    if (glob.leds) {
        int play = en_playing ? en_step : -1;
        if ((ui.btn >> B_HOME & 1u) || ui.view == V_PATTERN) {   /* the patterns: the chain lit, the one playing blinks */
            for (i = 0; i < EN_NPAT; i++) {
                int in = (en_cfg.chain_n ? 0 : (int)i == en_cfg.pat), j;
                for (j = 0; j < en_cfg.chain_n; j++)
                    if (en_cfg.chain[j] == i)
                        in = 1;
                if ((int)i == cur_pat() && en_playing)
                    in = bl;
                if (in)
                    k |= 1u << WHITE_KEY[i];
            }
        } else if ((ui.btn >> B_ARP & 1u) || ui.view == V_CHORD) {
            for (i = 0; i < EN_NCHORD; i++) {
                int in = 0, j;
                for (j = 0; j < en_cfg.chords_n; j++)
                    if (en_cfg.chords[j] == i)
                        in = 1;
                if (en_playing && en_chord_now == i)
                    in = bl;
                if (in)
                    k |= 1u << WHITE_KEY[i];
            }
        } else if ((ui.btn >> B_FX & 1u) || ui.view == V_FX) {
            if (ui.fx_key)
                k |= 1u << WHITE_KEY[ui.fx_key - 1];
            for (i = 0; i < EN_NSTEP; i++)
                if (p->fx[i])
                    k |= 1u << WHITE_KEY[p->fx[i] - 1];
        } else if (ui.view == V_MIX) {
            for (i = 0; i < EN_NTRACK; i++) {
                if (!(en_cfg.mute >> i & 1u))
                    k |= 1u << WHITE_KEY[i];
                if (i == song.track)
                    k |= 1u << WHITE_KEY[4 + i];
            }
        } else if (step_page()) {                /* the steps; the playhead blinks through them */
            for (i = 0; i < EN_NSTEP && i < p->len; i++) {
                int on = is_drum() ? (p->hit[song.part] >> i & 1u) : p->syn[trk() - 1][i].note[0] != 0;
                if ((int)i == play)
                    on = !on;
                if (on)
                    k |= 1u << WHITE_KEY[i];
            }
            if (is_drum())
                k |= 1u << BLACK_KEY[song.part];
        } else if (!is_drum()) {                 /* the keyboard: the keys held */
            k |= ui.keys & ((1u << NKEYS) - 1u);
        }
    }
    plat_glow(glob.leds == 1);
    plat_play_red(ui.rec);
    plat_leds(b, k);
}

/* AUTOSAVE: a few seconds after a change, while stopped and quiet, nothing touched (a flash erase silences
 * the audio for a moment: never while it plays; SAVE saves then) */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    int t;
    float lvl = 0.0f;
    for (t = 0; t < EN_NTRACK; t++)
        lvl = fm_maxf(lvl, en_level[t]);
    if (!ui_dirty() || en_playing || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || lvl > 0.01f)
        return;
    project_save();
}

void ui_init(void)
{
    int t, i;
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.view = V_SOUND;
    ui.hand_new = 1;
    for (t = 0; t < EN_NSYN; t++) {
        for (i = 0; i < EN_NNOTE; i++)
            ui.hand[t][i] = 0;
        ui.hand[t][0] = (uint8_t)(60 + 12 * song.oct[t]);
    }
    lcd_fill(0, 0, 240, 240, th()->body);
    scene_boot();                                /* the console's power-on */
}

void (*ui_mark)(int part);                       /* a development build's clock: where a frame's time goes */

void ui_frame(void)
{
    UI_MARK_(0);
    if (ui.rec && en_playing)                    /* writing live: the engine writes the pattern playing */
        mark_pat(en_pat_now);
    input();
    autosave();
    UI_MARK_(1);
    draw_status();
    UI_MARK_(2);
    draw_scene();
    UI_MARK_(3);
    draw_steps();
    UI_MARK_(4);
    draw_knobs();
    UI_MARK_(5);
    leds();
    UI_MARK_(6);
    ui.frame++;
}

void ui_input_only(void) { input(); }
