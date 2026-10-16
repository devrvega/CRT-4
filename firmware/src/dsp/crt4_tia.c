/* SPDX-License-Identifier: GPL-3.0-only */
/* The Atari 2600: the TIA's two sound channels. A channel is a 5-bit frequency divider (AUDF: the 31.4 kHz
 * audio clock divided by 1..32), a waveform (AUDC: sixteen settings, made of more dividers and of 4-, 5- and
 * 9-bit polynomial counters) and a 4-bit volume (AUDV). A note becomes the divider that comes nearest, so
 * the 2600's scale is its own: near the notes, rarely on them, and that is its sound. Some sounds play both
 * channels at once (two dividers beating). The driver runs at 60 frames a second (50 on PAL, its clock a
 * little slower). Included once. */

typedef struct {
    drv_t d;
    uint8_t audc;                      /* the waveform */
    uint8_t waves[4];                  /* the waveforms Wave picks from (0: none) */
    uint8_t two;                       /* the second channel: its waveform (0: one channel) */
    int8_t two_off;                    /* its note, over the first (semitones) */
} tia_patch_t;

static float tia_clock = 31399.5f;
static uint8_t tia_p4[15], tia_p5[31], tia_p9[511], tia_d31[31];

static void tia_init(void)
{
    uint32_t r, i;
    for (r = 0xFu, i = 0; i < 15u; i++) {       /* x^4 + x^3 + 1 */
        tia_p4[i] = (uint8_t)(r & 1u);
        r = (r >> 1) | ((((r >> 0) ^ (r >> 1)) & 1u) << 3);
    }
    for (r = 0x1Fu, i = 0; i < 31u; i++) {      /* x^5 + x^3 + 1 */
        tia_p5[i] = (uint8_t)(r & 1u);
        r = (r >> 1) | ((((r >> 0) ^ (r >> 2)) & 1u) << 4);
    }
    for (r = 0x1FFu, i = 0; i < 511u; i++) {    /* x^9 + x^5 + 1 */
        tia_p9[i] = (uint8_t)(r & 1u);
        r = (r >> 1) | ((((r >> 0) ^ (r >> 4)) & 1u) << 8);
    }
    for (i = 0; i < 31u; i++)                   /* the divide-by-31: two clocks a turn, 13 and 18 apart */
        tia_d31[i] = (uint8_t)(i == 0u || i == 18u);
}

/* the period, in divider ticks, of a waveform's tone (what a note is divided by) */
static float tia_mul(int audc)
{
    switch (audc & 15) {
    case 4:
    case 5: return 2.0f;
    case 12:
    case 13: return 6.0f;
    case 6:
    case 10: return 31.0f;
    case 14: return 93.0f;
    case 1: return 15.0f;
    case 2: return 232.5f;
    case 7:
    case 9: return 31.0f;
    case 15: return 93.0f;
    case 3: return 232.5f;
    default: return 31.0f;              /* the noise: brighter as the note goes up */
    }
}

/* a channel's state: its divider phase (ticks), its counters' places, its output */
typedef struct {
    uint8_t p4, p5, out;
    uint16_t p9;
    uint8_t d3;
} tia_ch_t;
static inline void tia_tick(tia_ch_t *c, int audc)
{
    int clk;
    if ((audc & 15) == 0 || (audc & 15) == 11) {
        c->out = 1;
        return;
    }
    if ((audc & 12) == 12) {           /* 12..15: the clock divided by 3 first */
        if (++c->d3 < 3)
            return;
        c->d3 = 0;
    }
    c->p5 = (uint8_t)((c->p5 + 1u) % 31u);
    clk = !(audc & 2) || (!(audc & 1) && tia_d31[c->p5]) || ((audc & 1) && tia_p5[c->p5]);
    if (!clk)
        return;
    if (audc & 4) {                    /* a pure tone: the output turns over */
        c->out ^= 1u;
    } else if (audc & 8) {
        if ((audc & 15) == 8) {
            c->p9 = (uint16_t)((c->p9 + 1u) % 511u);
            c->out = tia_p9[c->p9];
        } else {
            c->out = tia_p5[c->p5];
        }
    } else {
        c->p4 = (uint8_t)((c->p4 + 1u) % 15u);
        c->out = tia_p4[c->p4];
    }
}

static int tia_wave(const sv_t *v)
{
    const tia_patch_t *p = (const tia_patch_t *)v->patch;
    int n = 0;
    if (!has_kt(v->d, KT_WAVE))
        return p->audc;
    while (n < 4 && p->waves[n])
        n++;
    if (!n)
        return p->audc;
    return p->waves[(int)(kv(v, KT_WAVE, 0.0f) * ((float)n - 0.01f))];
}

/* the note's divider: AUDF for this waveform, the one nearest (from 1..32); the tick rate it makes */
static float tia_rate(float f, int audc)
{
    float m = tia_mul(audc), div = ((audc & 12) == 12) ? 3.0f : 1.0f, base = tia_clock / div;
    float tone = chip_quant(f, base, m, 0.0f, 1.0f, 32.0f);
    return tone * m;                   /* ticks a second */
}

static void tia_start(sv_t *v, int legato)
{
    chip_st *c = &v->e.c;
    if (legato)
        return;
    c->ph = c->ph2 = 0.0f;
    c->aux[0] = c->aux[1] = c->aux[2] = c->aux[3] = 0;
}
static void tia_frame(sv_t *v)
{
    const tia_patch_t *p = (const tia_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float note = v->pitch + v->poff, sr = eng->rate, vol = v->elev * fm_minf(v->vel, 1.0f);
    int w = tia_wave(v);
    if (has_kt(v->d, KT_BEND) && v->frame < 8)
        note += kv(v, KT_BEND, 0.0f) * 12.0f * (1.0f - (float)v->frame * 0.125f);
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (has_kt(v->d, KT_TONE))
        note += (kv(v, KT_TONE, 0.5f) - 0.5f) * 24.0f;
    c->inc = tia_rate(mtof(note), w) / sr;
    c->cnt2 = (uint32_t)w;
    c->acc = (float)(int)(vol * 15.0f + 0.5f) * (1.0f / 15.0f);
    if (p->two) {
        c->pwd = tia_rate(mtof(note + (float)p->two_off), p->two) / sr;
        c->wt[0] = (float)p->two;
    }
}
static void tia_render(sv_t *v, uint32_t n, float *out)
{
    const tia_patch_t *p = (const tia_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    tia_ch_t *a = (tia_ch_t *)(void *)&c->aux[0], *b = (tia_ch_t *)(void *)&c->aux[2];
    int w = (int)c->cnt2, w2 = p->two;
    float g = c->acc, inc = c->inc, inc2 = c->pwd;
    uint32_t j;
    if (g <= 0.0f) {
        c->ph += inc * (float)n;
        c->ph -= fm_floorf(c->ph);
        if (v->est >= 3)
            v->done = 1;
        return;
    }
    for (j = 0; j < n; j++) {
        float s = 0.0f, y;
        int k = 0;
        c->ph += inc;
        while (c->ph >= 1.0f && k < 8) {
            c->ph -= 1.0f;
            tia_tick(a, w);
            s += (float)a->out;
            k++;
        }
        if (c->ph >= 1.0f)
            c->ph -= fm_floorf(c->ph);
        y = k ? s / (float)k : (float)a->out;
        if (w2) {
            float s2 = 0.0f;
            int k2 = 0;
            c->ph2 += inc2;
            while (c->ph2 >= 1.0f && k2 < 8) {
                c->ph2 -= 1.0f;
                tia_tick(b, w2);
                s2 += (float)b->out;
                k2++;
            }
            if (c->ph2 >= 1.0f)
                c->ph2 -= fm_floorf(c->ph2);
            y = 0.6f * (y + (k2 ? s2 / (float)k2 : (float)b->out));
        }
        out[j] += (y - 0.5f) * 1.6f * g;
    }
}

static const en_sound_t TIA_SND[EN_NSOUND];
#define TIA_SND_PATCH(s) (TIA_SND[(unsigned)(s) % EN_NSOUND].patch)
static int tia_wave_text(int snd, int pos, char *b)
{
    const tia_patch_t *p = (const tia_patch_t *)TIA_SND_PATCH(snd);
    static const char *const N[16] = {"Off", "Buzz", "Rumble", "Poly", "Square", "Square", "Low", "Growl",
                                      "Noise", "Growl", "Low", "Off", "Mid", "Mid", "Deep", "Grit"};
    int a = p->waves[pos & 3];
    if (!a)
        return 0;
    scopy(b, N[a & 15], 10);
    return 1;
}

static void tia_control(void)
{
    int pal = fx_pos(2);
    tia_clock = pal ? 31113.0f : 31399.5f;
    drv_hz = pal ? 50.0f : 60.0f;
}

/* ------------------------------------------------------------------- the sounds --- */
#define TD(k0, k1, k2, k3, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct) {{k0, k1, k2, k3}, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct}
static const tia_patch_t TIA_P[EN_NSOUND] = {
    {TD(KT_DECAY, KT_SUSTAIN, KT_WAVE, KT_OCTAVE, 0, 40, 50, 10, 0, 30, 0, 0, 0, 0, 0, -1), 1, {1, 3, 15, 0}, 0, 0},
    {TD(KT_WAVE, KT_DECAY, KT_SUSTAIN, KT_BEND, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, -1), 6, {6, 12, 4, 0}, 0, 0},
    {TD(KT_DECAY, KT_SUSTAIN, KT_RELEASE, KT_VIB, 0, 45, 60, 15, 0, 20, 10, 0, 0, 0, 0, 0), 2, {0}, 0, 0},
    {TD(KT_DECAY, KT_SUSTAIN, KT_RELEASE, KT_BEND, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, 0), 15, {0}, 0, 0},
    {TD(KT_WAVE, KT_ARPSPD, KT_ARPMODE, KT_DECAY, 0, 55, 40, 15, 0, 30, 0, 0, 0, 2, DF_AUTOARP, 0), 12, {6, 12, 4, 0}, 0, 0},
    {TD(KT_WAVE, KT_ATTACK, KT_RELEASE, KT_VIB, 15, 0, 100, 25, 0, 30, 20, 0, 0, 0, 0, 0), 12, {6, 12, 4, 0}, 4, 12},
    {TD(KT_DECAY, KT_WAVE, KT_RELEASE, KT_OCTAVE, 0, 38, 0, 20, 0, 30, 0, 0, 0, 0, 0, 0), 4, {4, 12, 0, 0}, 4, 12},
    {TD(KT_WAVE, KT_ATTACK, KT_RELEASE, KT_VIB, 25, 0, 100, 30, 0, 20, 20, 0, 0, 0, 0, -1), 7, {7, 3, 15, 1}, 0, 0},
    {TD(KT_VIB, KT_DECAY, KT_SUSTAIN, KT_RELEASE, 0, 40, 70, 15, 30, 40, 25, 0, 0, 0, 0, 0), 4, {0}, 0, 0},
    {TD(KT_VIB, KT_DECAY, KT_SUSTAIN, KT_RELEASE, 0, 40, 70, 15, 30, 40, 25, 0, 0, 0, 0, 0), 1, {0}, 0, 0},
    {TD(KT_VIB, KT_DECAY, KT_SUSTAIN, KT_BEND, 0, 40, 70, 15, 30, 40, 25, 0, 0, 0, 0, 0), 3, {0}, 0, 0},
    {TD(KT_VIB, KT_DECAY, KT_WAVE, KT_SUSTAIN, 0, 40, 80, 20, 80, 8, 0, 0, 0, 0, 0, 0), 4, {4, 12, 6, 0}, 0, 0},
    {TD(KT_SWEEP, KT_DECAY, KT_WAVE, KT_SUSTAIN, 0, 40, 0, 5, 0, 30, 0, -32, 0, 0, 0, 1), 4, {4, 12, 6, 0}, 0, 0},
    {TD(KT_TONE, KT_WAVE, KT_DECAY, KT_VIB, 0, 0, 100, 20, 60, 60, 0, 0, 0, 0, 0, -1), 2, {2, 3, 7, 0}, 0, 0},
    {TD(KT_SWEEP, KT_DECAY, KT_WAVE, KT_OCTAVE, 0, 40, 0, 5, 0, 30, 0, 24, 6, 0, 0, 0), 12, {12, 4, 6, 0}, 0, 0},
    {TD(KT_DECAY, KT_TONE, KT_RELEASE, KT_WAVE, 0, 55, 0, 10, 0, 30, 0, -2, 0, 0, 0, 0), 8, {8, 3, 2, 0}, 0, 0},
};
static const en_sound_t TIA_SND[EN_NSOUND] = {
    {"Buzz", "TIA poly4", {"Decay", "Sustain", "Wave", "Octave"}, {40, 50, 0, 50}, {0, 0, 3, 3}, &TIA_P[0]},
    {"Pure", "TIA dividers", {"Wave", "Decay", "Sustain", "Thump"}, {0, 40, 55, 0}, {3, 0, 0, 0}, &TIA_P[1]},
    {"Rumble", "TIA div15 poly", {"Decay", "Sustain", "Release", "Vibrato"}, {45, 60, 15, 0}, {0, 0, 0, 0}, &TIA_P[2]},
    {"Div", "TIA poly5 div6", {"Decay", "Sustain", "Release", "Thump"}, {40, 55, 10, 0}, {0, 0, 0, 0}, &TIA_P[3]},
    {"Arp chords", "TIA arpeggios", {"Wave", "Speed", "Mode", "Decay"}, {50, 20, 0, 55}, {3, 6, 4, 0}, &TIA_P[4]},
    {"Organ", "both channels", {"Wave", "Attack", "Release", "Vibrato"}, {50, 15, 25, 0}, {3, 0, 0, 0}, &TIA_P[5]},
    {"Chime", "both channels", {"Decay", "Wave", "Release", "Octave"}, {38, 0, 20, 50}, {0, 2, 0, 3}, &TIA_P[6]},
    {"Drone", "TIA poly5", {"Wave", "Attack", "Release", "Vibrato"}, {0, 25, 30, 0}, {4, 0, 0, 0}, &TIA_P[7]},
    {"Square", "TIA div2", {"Vibrato", "Decay", "Sustain", "Release"}, {30, 40, 70, 15}, {0, 0, 0, 0}, &TIA_P[8]},
    {"Buzz lead", "TIA poly4", {"Vibrato", "Decay", "Sustain", "Release"}, {30, 40, 70, 15}, {0, 0, 0, 0}, &TIA_P[9]},
    {"Poly lead", "TIA poly5-poly4", {"Vibrato", "Decay", "Sustain", "Thump"}, {30, 40, 70, 0}, {0, 0, 0, 0}, &TIA_P[10]},
    {"Siren", "TIA div2", {"Depth", "Decay", "Wave", "Sustain"}, {80, 40, 0, 80}, {0, 0, 3, 0}, &TIA_P[11]},
    {"Laser", "TIA sweep", {"Sweep", "Decay", "Wave", "Sustain"}, {15, 40, 0, 0}, {0, 0, 3, 0}, &TIA_P[12]},
    {"Engine", "TIA rumble", {"Rev", "Wave", "Decay", "Wobble"}, {50, 0, 0, 60}, {0, 3, 0, 0}, &TIA_P[13]},
    {"Ricochet", "TIA sweep", {"Sweep", "Decay", "Wave", "Octave"}, {85, 40, 0, 50}, {0, 0, 3, 3}, &TIA_P[14]},
    {"Blast", "TIA poly9", {"Decay", "Tone", "Release", "Wave"}, {55, 50, 10, 0}, {0, 0, 0, 3}, &TIA_P[15]},
};

#define TIA_DRUM(audc, dec, sw, swt) {TD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, dec, 0, 0, 0, 0, 0, sw, swt, 0, DF_NOREL, 0), audc, {0}, 0, 0}
static const tia_patch_t TIA_DP[2][EN_NPART] = {
    {TIA_DRUM(6, 30, -40, 6), TIA_DRUM(8, 30, 0, 0), TIA_DRUM(6, 32, -12, 10), TIA_DRUM(6, 30, -12, 10), TIA_DRUM(12, 28, -12, 10),
     TIA_DRUM(1, 8, 0, 0), TIA_DRUM(8, 24, 0, 0), TIA_DRUM(8, 10, 0, 0), TIA_DRUM(8, 34, 0, 0), TIA_DRUM(8, 52, 0, 0),
     TIA_DRUM(4, 30, -48, 0)},
    {TIA_DRUM(2, 26, 0, 0), TIA_DRUM(3, 26, 0, 0), TIA_DRUM(4, 22, 16, 8), TIA_DRUM(4, 20, 16, 8), TIA_DRUM(4, 18, 16, 8),
     TIA_DRUM(4, 5, 0, 0), TIA_DRUM(7, 26, 0, 0), TIA_DRUM(8, 12, 0, 0), TIA_DRUM(8, 40, 0, 0), TIA_DRUM(2, 55, 0, 0),
     TIA_DRUM(12, 36, -30, 0)},
};
#undef TIA_DRUM
static const dkit_t TIA_KIT[EN_NKIT] = {
    {"TIA", {{"KCK", "Kick", &TIA_DP[0][0], 45, 0, 0}, {"SNR", "Snare", &TIA_DP[0][1], 60, 0, 0},
             {"LT", "Low tom", &TIA_DP[0][2], 50, 0, 0}, {"MT", "Mid tom", &TIA_DP[0][3], 56, 0, 0},
             {"HT", "High tom", &TIA_DP[0][4], 62, 0, 0}, {"CLK", "Click", &TIA_DP[0][5], 84, 0, 0},
             {"CLP", "Clap", &TIA_DP[0][6], 66, 0, 0}, {"CH", "Closed hat", &TIA_DP[0][7], 84, 0, 1},
             {"OH", "Open hat", &TIA_DP[0][8], 84, 0, 1}, {"CRS", "Crash", &TIA_DP[0][9], 72, 0, 0},
             {"ZAP", "Zap", &TIA_DP[0][10], 84, 0, 0}}},
    {"Poly", {{"THD", "Thud", &TIA_DP[1][0], 40, 0, 0}, {"CRN", "Crunch", &TIA_DP[1][1], 50, 0, 0},
              {"BL1", "Bloop low", &TIA_DP[1][2], 55, 0, 0}, {"BL2", "Bloop mid", &TIA_DP[1][3], 62, 0, 0},
              {"BL3", "Bloop high", &TIA_DP[1][4], 69, 0, 0}, {"TCK", "Tick", &TIA_DP[1][5], 88, 0, 0},
              {"RTL", "Rattle", &TIA_DP[1][6], 60, 0, 0}, {"HSS", "Hiss short", &TIA_DP[1][7], 84, 0, 1},
              {"HSL", "Hiss long", &TIA_DP[1][8], 84, 0, 1}, {"RMB", "Rumble", &TIA_DP[1][9], 36, 0, 0},
              {"LSR", "Laser", &TIA_DP[1][10], 80, 0, 0}}},
};
#undef TD
static const engine_t TIA_ENG = {44100.0f, 60.0f, tia_start, tia_frame, tia_render, 0};
