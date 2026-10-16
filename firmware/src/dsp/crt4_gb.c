/* SPDX-License-Identifier: GPL-3.0-only */
/* The Game Boy: four channels.
 *   pulse 1, 2   duty 12.5 / 25 / 50 / 75 %, an 11-bit frequency (131072 / (2048 - x)), a 4-bit volume;
 *                the first has a hardware sweep
 *   wave         32 steps of 4 bits, written by the game; only four volumes (100, 50, 25 %, off), so it
 *                does not fade, it steps; trackers made it a synth by rewriting the table as a note plays
 *                (and speak, with tables of vowels), and played 4-bit drum kits through it
 *   noise        a 15-bit register, or a 7-bit one (a metallic tone)
 * Each channel goes left, right or both (no in between); its output has a high-pass (a capacitor), and on
 * the first model a soft top. The driver: 59.7 frames a second. Included once. */

enum { GB_PULSE, GB_WAVE, GB_NOISE, GB_SMP };
typedef struct {
    drv_t d;
    uint8_t ch;                        /* GB_* */
    uint8_t duty;                      /* pulse: 0..3; wave: its table; noise: 1 the 7-bit mode */
    uint8_t waves[4];                  /* what Wave picks from: + 1 (0: no more) */
    uint8_t morph;                     /* the wave synth: the table it turns into (+ 1; 0 none) */
    uint8_t wobble;                    /* ... back and forth */
    uint8_t echo;                      /* ping-pong: the echo's delay, in frames, on the other side */
    uint8_t alt;                       /* every other note of an arpeggio on the other side */
    uint8_t smp;                       /* a wave-kit sample */
    uint8_t speech;                    /* the vowels */
} gb_patch_t;

#define GB_NW 13
static uint8_t gb_wt[GB_NW][32];
static const char *const GB_WN[GB_NW] = {"Saw", "Square", "Tri", "Sine", "Round", "Organ", "Hollow", "Thin",
                                         "A", "E", "I", "O", "U"};
static void gb_init(void)
{
    /* the vowels: a voice's harmonics weighted by its formants, at a man's pitch (as the speech tables were) */
    static const float VF[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
    int w, i, k;
    for (w = 0; w < GB_NW; w++)
        for (i = 0; i < 32; i++) {
            float x = (float)i / 32.0f, s = 0.0f;
            switch (w) {
            case 0: s = 1.0f - 2.0f * x; break;
            case 1: s = x < 0.5f ? 0.9f : -0.9f; break;
            case 2: s = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; break;
            case 3: s = sine(x); break;
            case 4: s = sine(x) * 0.8f + sine(2.0f * x) * 0.3f; break;
            case 5: s = 0.5f * sine(x) + 0.4f * sine(2.0f * x) + 0.3f * sine(3.0f * x) + 0.25f * sine(4.0f * x); break;
            case 6: s = 0.6f * sine(x) + 0.35f * sine(3.0f * x) + 0.2f * sine(5.0f * x); break;
            case 7: s = x < 0.15f ? 0.9f : -0.2f; break;
            default: {
                const float *fm = VF[w - 8];
                for (k = 1; k <= 16; k++) {
                    float fk = 130.0f * (float)k, a = 0.0f;
                    int q;
                    for (q = 0; q < 3; q++) {
                        float d = (fk - fm[q]) / (90.0f + 40.0f * (float)q);
                        a += 1.0f / (1.0f + d * d) * (q == 0 ? 1.0f : q == 1 ? 0.6f : 0.35f);
                    }
                    s += a * sine(x * (float)k) / (float)k * 2.2f;
                }
                break;
            }
            }
            k = (int)((fm_clampf(s, -1.0f, 1.0f) + 1.0f) * 7.5f + 0.5f);
            gb_wt[w][i] = (uint8_t)(k < 0 ? 0 : k > 15 ? 15 : k);
        }
}

static int gb_pick(const sv_t *v)
{
    const gb_patch_t *p = (const gb_patch_t *)v->patch;
    int n = 0;
    if (!has_kt(v->d, KT_WAVE))
        return p->duty;
    while (n < 4 && p->waves[n])
        n++;
    return n ? p->waves[(int)(kv(v, KT_WAVE, 0.0f) * ((float)n - 0.01f))] - 1 : p->duty;
}

static void gb_start(sv_t *v, int legato)
{
    const gb_patch_t *p = (const gb_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    if (legato)
        return;
    if (!c->lfsr)
        c->lfsr = 0x7FFFu;
    c->cnt = 0;
    c->hold = 0.0f;
    if (p->ch == GB_SMP) {
        const bk_smp_t *s = bank_smp(p->smp);
        c->smp = s->data;
        c->slen = s->nsamp;
        c->spos = 0;
        c->sfrac = 0.0f;
    }
}

static void gb_frame(sv_t *v)
{
    const gb_patch_t *p = (const gb_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float note = v->pitch + v->poff, sr = eng->rate, vol = v->elev * fm_minf(v->vel, 1.0f), f;
    if (has_kt(v->d, KT_BEND) && v->frame < 8)
        note += kv(v, KT_BEND, 0.0f) * 12.0f * (1.0f - (float)v->frame * 0.125f);
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (v->trk < 0 && p->ch != GB_SMP)
        note += (kv(v, KT_TONE, 0.5f) - 0.5f) * 24.0f;
    f = mtof(note);
    c->cnt++;
    switch (p->ch) {
    case GB_PULSE: {
        static const float DUTY[4] = {0.125f, 0.25f, 0.5f, 0.75f};
        int d = gb_pick(v);
        if (p->speech && v->frame < 3)
            d = 2;
        c->pw = DUTY[d & 3];
        f = chip_quant(f, 4194304.0f, 32.0f, 0.0f, 1.0f, 2048.0f);
        c->inc = f / sr;
        c->acc = (float)(int)(vol * 15.0f + 0.5f) * (1.0f / 15.0f);
        if (p->echo) {                 /* the ping-pong: the line again, so many frames on, the other side */
            int del = 2 + (int)(kv(v, KT_SPEED, 0.5f) * 14.0f), w = (int)(c->cnt & 15u), r = (int)((c->cnt - (uint32_t)del) & 15u);
            c->wt[w] = c->acc;
            c->wt[16 + w] = c->inc;
            c->sv[0] = c->cnt > (uint32_t)del ? (float)(int)(c->wt[r] * 0.6f * 15.0f + 0.5f) * (1.0f / 15.0f) : 0.0f;
            c->sv[1] = c->wt[16 + r];
            v->done = (uint8_t)(v->done && c->sv[0] <= 0.0f);
        }
        if (p->alt)
            c->mod = (float)(v->ci & 1);                /* this arpeggio note's side */
        break;
    }
    case GB_WAVE: {
        int w = gb_pick(v), w2;
        float m;
        if (p->speech) {               /* the vowel the knob points at, moving to the next as it speaks */
            float x = kv(v, KT_VOWEL, 0.0f) * 4.0f + (float)v->frame * kv(v, KT_SPEED, 0.0f) * 0.15f;
            int a = (int)x % 5;
            w = 8 + a;
            w2 = 8 + (a + 1) % 5;
            m = x - fm_floorf(x);
        } else if (p->morph) {         /* the synth: the table turning into another (and back, wobbling) */
            float spd = 0.01f + 0.12f * kv(v, KT_SPEED, 0.4f), ph = (float)v->frame * spd;
            w2 = p->morph - 1;
            if (p->wobble) {
                ph -= fm_floorf(ph);
                m = (ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph) * kv(v, KT_MOD, 0.8f);
            } else {
                m = fm_minf(ph, 1.0f);
            }
        } else {
            w2 = w;
            m = 0.0f;
        }
        {   /* the table written: each step 4 bits */
            int i;
            for (i = 0; i < 32; i++) {
                float a = (float)gb_wt[w % GB_NW][i], b = (float)gb_wt[w2 % GB_NW][i];
                c->wt[i] = (float)(int)(a + (b - a) * m + 0.5f) * (2.0f / 15.0f) - 1.0f;
            }
        }
        f = chip_quant(f, 2097152.0f, 32.0f, 0.0f, 1.0f, 2048.0f);
        c->inc = f / sr;
        {   /* its volume: four steps only */
            int q = vol > 0.75f ? 0 : vol > 0.37f ? 1 : vol > 0.12f ? 2 : 3;
            static const float WV[4] = {1.0f, 0.5f, 0.25f, 0.0f};
            c->acc = WV[q];
        }
        break;
    }
    case GB_NOISE: {
        float rate = p->duty ? f * 127.0f : 4000.0f * fm_exp2f((note - 60.0f) * (1.0f / 6.0f));
        c->inc = fm_minf(rate, 262144.0f) / sr;
        c->acc = (float)(int)(vol * 15.0f + 0.5f) * (1.0f / 15.0f);
        break;
    }
    default: {
        const bk_smp_t *s = bank_smp(p->smp);
        c->srate = (float)s->rate * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / sr;
        break;
    }
    }
}

static void gb_render(sv_t *v, uint32_t n, float *out)
{
    const gb_patch_t *p = (const gb_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float g = c->acc * 0.55f, ph = c->ph, inc = c->inc;
    float *o = out;
    uint32_t j;
    if (p->alt && alt_seg && c->mod > 0.5f) {    /* this arpeggio note on the other side */
        o = alt_seg;
        alt_used = 1;
    }
    switch (p->ch) {
    case GB_PULSE: {
        float w = c->pw;
        if (p->echo && c->sv[0] > 0.0f && alt_seg) {
            float e = c->sv[0] * 0.55f, i2 = c->sv[1], q = c->ph2;
            float *ao = alt_seg;
            alt_used = 1;
            for (j = 0; j < n; j++) {
                q = wrap1(q + i2);
                ao[j] += chip_pulse(q, i2, w) * e;
            }
            c->ph2 = q;
        }
        if (g > 0.0f)
            for (j = 0; j < n; j++) {
                ph = wrap1(ph + inc);
                o[j] += chip_pulse(ph, inc, w) * g;
            }
        else
            ph = wrap1(ph + inc * (float)n);
        break;
    }
    case GB_WAVE:
        if (g > 0.0f)
            for (j = 0; j < n; j++) {
                ph = wrap1(ph + inc);
                o[j] += c->wt[(int)(ph * 32.0f) & 31] * g;
            }
        else
            ph = wrap1(ph + inc * (float)n);
        break;
    case GB_NOISE:
        for (j = 0; j < n; j++)
            o[j] += noise_run(c, inc, p->duty ? lfsr_gb7 : lfsr_nes) * g * 0.8f;
        break;
    default: {                         /* a wave kit: 4 bits through the wave channel */
        const bk_smp_t *s = bank_smp(p->smp);
        float y = c->hold;
        for (j = 0; j < n; j++) {
            c->sfrac += c->srate;
            while (c->sfrac >= 1.0f) {
                c->sfrac -= 1.0f;
                if (++c->spos >= c->slen) {
                    v->done = 1;
                    break;
                }
                y = pcm4_at(s->data, c->spos);
            }
            o[j] += y * 0.6f * fm_minf(v->vel, 1.2f);
        }
        c->hold = y;
        return;
    }
    }
    c->ph = ph;
    if (v->elev <= 0.0f && v->est >= 3 && !(p->echo && c->sv[0] > 0.0f))
        v->done = 1;
}

/* ------------------------------------------------------------------- the sounds --- */
#define BD(k0, k1, k2, k3, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct) {{k0, k1, k2, k3}, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct}
static const gb_patch_t GB_P[EN_NSOUND] = {
    {BD(KT_WAVE, KT_DECAY, KT_BEND, KT_OCTAVE, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, -1), GB_WAVE, 0, {1, 2, 3, 5}, 0, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_DECAY, KT_SUSTAIN, KT_BEND, 0, 40, 45, 8, 0, 30, 0, 0, 0, 0, 0, -1), GB_PULSE, 2, {1, 2, 3, 4}, 0, 0, 0, 0, 0, 0},
    {BD(KT_SPEED, KT_MOD, KT_WAVE, KT_RELEASE, 0, 0, 100, 15, 0, 30, 0, 0, 0, 0, 0, -1), GB_WAVE, 0, {1, 2, 7, 0}, 4, 1, 0, 0, 0, 0},
    {BD(KT_DECAY, KT_SUSTAIN, KT_BEND, KT_OCTAVE, 0, 40, 70, 10, 0, 30, 0, 0, 0, 0, 0, -1), GB_WAVE, 3, {0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_ARPSPD, KT_ARPMODE, KT_DECAY, 0, 55, 40, 15, 0, 30, 0, 0, 0, 1, DF_AUTOARP, 0), GB_PULSE, 2, {1, 2, 3, 4}, 0, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_ATTACK, KT_RELEASE, KT_VIB, 10, 0, 100, 25, 0, 40, 20, 0, 0, 0, 0, 0), GB_WAVE, 5, {6, 7, 5, 0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_DECAY, KT_RELEASE, KT_VIB, 0, 30, 0, 10, 0, 30, 30, 0, 0, 0, 0, 0), GB_PULSE, 0, {1, 2, 3, 0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_SPEED, KT_WAVE, KT_RELEASE, KT_VIB, 0, 0, 100, 30, 0, 30, 25, 0, 0, 0, 0, 0), GB_WAVE, 3, {4, 5, 6, 0}, 1, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_VIB, KT_DECAY, KT_SUSTAIN, 0, 40, 70, 15, 30, 40, 25, 0, 0, 0, 0, 0), GB_PULSE, 1, {1, 2, 3, 4}, 0, 0, 0, 0, 0, 0},
    {BD(KT_SWEEP, KT_WAVE, KT_VIB, KT_DECAY, 0, 40, 70, 15, 25, 40, 25, 16, 6, 0, 0, 0), GB_PULSE, 1, {1, 2, 3, 0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_SPEED, KT_WAVE, KT_VIB, KT_RELEASE, 0, 0, 100, 20, 25, 40, 25, 0, 0, 0, 0, 0), GB_WAVE, 7, {1, 2, 7, 0}, 4, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_SPEED, KT_VIB, KT_DECAY, 0, 40, 60, 25, 25, 40, 25, 0, 0, 0, 0, 0), GB_PULSE, 1, {1, 2, 3, 0}, 0, 0, 8, 0, 0, 0},
    {BD(KT_VOWEL, KT_SPEED, KT_VIB, KT_RELEASE, 0, 0, 100, 15, 20, 40, 15, 0, 0, 0, 0, -1), GB_WAVE, 8, {0}, 0, 0, 0, 0, 0, 1},
    {BD(KT_DECAY, KT_SUSTAIN, KT_SWEEP, KT_RELEASE, 0, 40, 40, 15, 0, 30, 0, 0, 0, 0, 0, 0), GB_NOISE, 1, {0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_BEND, KT_DECAY, KT_WAVE, KT_OCTAVE, 0, 40, 60, 10, 0, 30, 0, 0, 0, 0, 0, -1), GB_WAVE, 3, {4, 1, 2, 0}, 0, 0, 0, 0, 0, 0},
    {BD(KT_WAVE, KT_ARPSPD, KT_ARPMODE, KT_DECAY, 0, 55, 40, 15, 0, 30, 0, 0, 0, 2, DF_AUTOARP, 0), GB_PULSE, 2, {1, 2, 3, 4}, 0, 0, 0, 1, 0, 0},
};
static const en_sound_t GB_SND[EN_NSOUND] = {
    {"Wave bass", "DMG wave channel", {"Wave", "Decay", "Thump", "Octave"}, {0, 40, 25, 50}, {4, 0, 0, 3}, &GB_P[0]},
    {"Pulse bass", "DMG pulse", {"Duty", "Decay", "Sustain", "Thump"}, {66, 40, 45, 0}, {4, 0, 0, 0}, &GB_P[1]},
    {"Wobble", "DMG wave synth", {"Speed", "Depth", "Wave", "Release"}, {40, 80, 0, 15}, {0, 0, 3, 0}, &GB_P[2]},
    {"Sub", "DMG wave channel", {"Decay", "Sustain", "Thump", "Octave"}, {40, 70, 20, 50}, {0, 0, 0, 3}, &GB_P[3]},
    {"Arp chords", "DMG arpeggios", {"Duty", "Speed", "Mode", "Decay"}, {66, 0, 0, 55}, {4, 6, 4, 0}, &GB_P[4]},
    {"Wave organ", "DMG wave channel", {"Wave", "Attack", "Release", "Vibrato"}, {0, 10, 25, 0}, {3, 0, 0, 0}, &GB_P[5]},
    {"Pluck", "DMG pulse", {"Duty", "Decay", "Release", "Vibrato"}, {0, 30, 10, 0}, {3, 0, 0, 0}, &GB_P[6]},
    {"Wave pad", "DMG wave synth", {"Speed", "Wave", "Release", "Vibrato"}, {20, 0, 30, 20}, {0, 3, 0, 0}, &GB_P[7]},
    {"Pulse lead", "DMG pulse", {"Duty", "Vibrato", "Decay", "Sustain"}, {33, 30, 40, 70}, {4, 0, 0, 0}, &GB_P[8]},
    {"Sweep lead", "DMG pulse sweep", {"Sweep", "Duty", "Vibrato", "Decay"}, {70, 50, 25, 40}, {0, 3, 0, 0}, &GB_P[9]},
    {"Wave synth", "DMG wave synth", {"Speed", "Wave", "Vibrato", "Release"}, {35, 0, 25, 20}, {0, 3, 0, 0}, &GB_P[10]},
    {"Ping-pong", "DMG pulses, L / R", {"Duty", "Delay", "Vibrato", "Decay"}, {50, 40, 25, 40}, {3, 0, 0, 0}, &GB_P[11]},
    {"Speech", "DMG wave vowels", {"Vowel", "Speed", "Vibrato", "Release"}, {0, 30, 20, 15}, {0, 0, 0, 0}, &GB_P[12]},
    {"Metal noise", "DMG 7-bit noise", {"Decay", "Sustain", "Sweep", "Release"}, {40, 40, 50, 15}, {0, 0, 0, 0}, &GB_P[13]},
    {"Kick bass", "DMG wave channel", {"Kick", "Decay", "Wave", "Octave"}, {60, 40, 0, 50}, {0, 0, 3, 3}, &GB_P[14]},
    {"Stereo arp", "DMG pulses, L / R", {"Duty", "Speed", "Mode", "Decay"}, {66, 20, 0, 55}, {4, 6, 4, 0}, &GB_P[15]},
};
static int gb_wave_text(int snd, int pos, char *b)
{
    static const char *const DUTY[4] = {"12.5%", "25%", "50%", "75%"};
    const gb_patch_t *p = (const gb_patch_t *)GB_SND[(unsigned)snd % EN_NSOUND].patch;
    int w = p->waves[pos & 3];
    if (!w)
        return 0;
    scopy(b, p->ch == GB_PULSE ? DUTY[(w - 1) & 3] : GB_WN[(w - 1) % GB_NW], 10);
    return 1;
}

#define GDR(ch, duty, dec, sw, swt) {BD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, dec, 0, 0, 0, 0, 0, sw, swt, 0, DF_NOREL, 0), ch, duty, {0}, 0, 0, 0, 0, 0, 0}
#define GDS(smp) {BD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0), GB_SMP, 0, {0}, 0, 0, 0, 0, smp, 0}
static const gb_patch_t GB_DP[2][EN_NPART] = {
    {GDR(GB_PULSE, 2, 28, -40, 6), GDR(GB_NOISE, 0, 30, 0, 0), GDR(GB_PULSE, 2, 32, -12, 10), GDR(GB_PULSE, 2, 30, -12, 10),
     GDR(GB_PULSE, 2, 28, -12, 10), GDR(GB_NOISE, 1, 8, 0, 0), GDR(GB_NOISE, 0, 24, 0, 0), GDR(GB_NOISE, 0, 10, 0, 0),
     GDR(GB_NOISE, 0, 36, 0, 0), GDR(GB_NOISE, 0, 52, 0, 0), GDR(GB_NOISE, 1, 30, 0, 0)},
    {GDS(BK_GB_KICK), GDS(BK_GB_SNARE), GDS(BK_GB_TOM), GDS(BK_GB_TOM), GDS(BK_GB_TOM), GDS(BK_GB_RIM),
     GDS(BK_GB_CLAP), GDS(BK_GB_CHAT), GDS(BK_GB_OHAT), GDS(BK_GB_CRASH), GDS(BK_GB_COW)},
};
#undef GDR
#undef GDS
#undef BD
static const dkit_t GB_KIT[EN_NKIT] = {
    {"Noise", {{"KCK", "Kick", &GB_DP[0][0], 45, 0, 0}, {"SNR", "Snare", &GB_DP[0][1], 68, 0, 0},
               {"LT", "Low tom", &GB_DP[0][2], 50, -100, 0}, {"MT", "Mid tom", &GB_DP[0][3], 56, 0, 0},
               {"HT", "High tom", &GB_DP[0][4], 62, 100, 0}, {"RIM", "Rim", &GB_DP[0][5], 84, 0, 0},
               {"CLP", "Clap", &GB_DP[0][6], 66, 0, 0}, {"CH", "Closed hat", &GB_DP[0][7], 84, 0, 1},
               {"OH", "Open hat", &GB_DP[0][8], 84, 0, 1}, {"CRS", "Crash", &GB_DP[0][9], 70, 0, 0},
               {"MTL", "Metal", &GB_DP[0][10], 72, 0, 0}}},
    {"Wave kit", {{"KCK", "Kick", &GB_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &GB_DP[1][1], 60, 0, 0},
                  {"LT", "Low tom", &GB_DP[1][2], 55, -100, 0}, {"MT", "Mid tom", &GB_DP[1][3], 60, 0, 0},
                  {"HT", "High tom", &GB_DP[1][4], 65, 100, 0}, {"RIM", "Rim", &GB_DP[1][5], 60, 0, 0},
                  {"CLP", "Clap", &GB_DP[1][6], 60, 0, 0}, {"CH", "Closed hat", &GB_DP[1][7], 60, 0, 1},
                  {"OH", "Open hat", &GB_DP[1][8], 60, 0, 1}, {"CRS", "Crash", &GB_DP[1][9], 60, 0, 0},
                  {"CB", "Cowbell", &GB_DP[1][10], 60, 0, 0}}},
};
static const engine_t GB_ENG = {44100.0f, 59.73f, gb_start, gb_frame, gb_render, 0};
