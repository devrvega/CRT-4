/* SPDX-License-Identifier: GPL-3.0-only */
/* The PC Engine (TurboGrafx-16): the HuC6280's six channels. Each plays a 32-step table of 5-bit values,
 * written by the game (so every game had its own waves), at a 12-bit period, with a 5-bit volume in steps of
 * 1.5 dB and its own left and right; two of them can be noise; the second can modulate the first's pitch
 * (the LFO: slow for a vibrato, at audio rate for something near FM); and a channel can be written sample
 * by sample (DDA: 5-bit drums and voices). The CD-ROM's ADPCM (an OKI MSM5205) played its strings, choirs
 * and guitars. The driver: 60 frames a second, tables rewritten between notes. Included once. */

enum { PC_WAVE, PC_NOISE, PC_DDA, PC_CD };
typedef struct {
    drv_t d;
    uint8_t ch;                        /* PC_* */
    uint8_t wave;                      /* its table */
    uint8_t waves[4];                  /* what Wave picks from: its tables + 1 (0: no more) */
    uint8_t lfo;                       /* the modulating channel's table (0: none) */
    uint8_t lratio;                    /* its pitch: x 1/8 of the note's (0: a slow LFO, Hz in vspd) */
    uint8_t ldepth;                    /* how far it moves the pitch, 0..100 */
    uint8_t smp;                       /* a DDA or CD sample */
    uint8_t morph;                     /* a table it turns into, frame by frame (0: none) */
} pce_patch_t;

#define PCE_NW 16
static uint8_t pce_wt[PCE_NW][32];     /* the tables (made at the start): 5-bit, 0..31 */
static const char *const PCE_WN[PCE_NW] = {"Saw", "Round", "Hollow", "Buzz", "Square", "Narrow", "Sine", "Organ",
                                           "Piano", "Bell", "Slap", "Lead", "Bright", "Tri", "Horn", "Voice"};
static void pce_init(void)
{
    int w, i, k;
    for (w = 0; w < PCE_NW; w++)
        for (i = 0; i < 32; i++) {
            float x = (float)i / 32.0f, s = 0.0f;
            switch (w) {
            case 0: s = 1.0f - 2.0f * x; break;
            case 1: for (k = 1; k <= 4; k++) s += sine(x * (float)k) / (float)k * 0.85f; break;
            case 2: for (k = 1; k <= 7; k += 2) s += sine(x * (float)k) / (float)k; break;
            case 3: s = x < 0.25f ? 0.9f : -0.4f; break;
            case 4: s = x < 0.5f ? 0.85f : -0.85f; break;
            case 5: s = x < 0.125f ? 0.95f : -0.2f; break;
            case 6: s = sine(x); break;
            case 7: s = 0.6f * sine(x) + 0.4f * sine(2.0f * x) + 0.3f * sine(4.0f * x) + 0.2f * sine(8.0f * x); break;
            case 8: s = 0.7f * sine(x) + 0.35f * sine(2.0f * x) + 0.2f * sine(3.0f * x) + 0.15f * sine(5.0f * x); break;
            case 9: s = 0.6f * sine(x) + 0.4f * sine(3.0f * x) + 0.35f * sine(7.0f * x); break;
            case 10: s = 0.6f * sine(x) + 0.5f * sine(2.0f * x) + 0.45f * sine(5.0f * x) + 0.3f * sine(9.0f * x); break;
            case 11: s = 0.5f * (1.0f - 2.0f * x) + 0.4f * (x < 0.5f ? 1.0f : -1.0f); break;
            case 12: s = 0.55f * (1.0f - 2.0f * x) + 0.45f * (x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x); break;
            case 13: s = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; break;
            case 14: for (k = 1; k <= 8; k++) s += sine(x * (float)k) * (k == 1 ? 0.6f : 0.9f / (float)k); break;
            default: s = 0.5f * sine(x) + 0.5f * sine(3.0f * x) + 0.3f * sine(4.0f * x); break;
            }
            k = (int)((fm_clampf(s, -1.0f, 1.0f) + 1.0f) * 15.5f);
            pce_wt[w][i] = (uint8_t)(k < 0 ? 0 : k > 31 ? 31 : k);
        }
}
static inline float pce_tab(const uint8_t *w, float ph) { return ((float)w[(int)(ph * 32.0f) & 31] - 15.5f) * (1.0f / 15.5f); }
/* 5 bits of volume, 1.5 dB a step */
static inline float pce_vol(float x) { return vol_db(x, 1.5f, 31); }

static int pce_wave(const sv_t *v)
{
    const pce_patch_t *p = (const pce_patch_t *)v->patch;
    int n = 0;
    if (!has_kt(v->d, KT_WAVE))
        return p->wave;
    while (n < 4 && p->waves[n])
        n++;
    return n ? p->waves[(int)(kv(v, KT_WAVE, 0.0f) * ((float)n - 0.01f))] - 1 : p->wave;
}

/* the OKI's decoder (the bank's encoder is its twin) */
static int oki_step(int nib, int32_t *sig, uint32_t *idx)
{
    static const int16_t ST[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
                                   143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
                                   796, 876, 963, 1060, 1166, 1282, 1411, 1552};
    static const int8_t AD[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int st = ST[*idx], d = st >> 3, i;
    if (nib & 1)
        d += st >> 2;
    if (nib & 2)
        d += st >> 1;
    if (nib & 4)
        d += st;
    if (nib & 8)
        d = -d;
    *sig += d;
    *sig = *sig < -2048 ? -2048 : *sig > 2047 ? 2047 : *sig;
    i = (int)*idx + AD[nib & 7];
    *idx = (uint32_t)(i < 0 ? 0 : i > 48 ? 48 : i);
    return (int)*sig;
}

static void pce_start(sv_t *v, int legato)
{
    const pce_patch_t *p = (const pce_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    if (legato)
        return;
    if (!c->lfsr)
        c->lfsr = 1u;
    if (p->ch == PC_DDA || p->ch == PC_CD) {
        const bk_smp_t *s = bank_smp(p->smp);
        c->smp = s->data;
        c->slen = s->nsamp;
        c->spos = 0;
        c->sfrac = 0.0f;
        c->dlt = 0;                    /* the OKI's signal */
        c->cnt = 0;                    /* its step index */
        c->acc = 0.0f;
    }
    c->mod = 0.0f;
}
static void pce_frame(sv_t *v)
{
    const pce_patch_t *p = (const pce_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float note = v->pitch + v->poff, sr = eng->rate, f;
    if (has_kt(v->d, KT_BEND) && v->frame < 8)
        note += kv(v, KT_BEND, 0.0f) * 12.0f * (1.0f - (float)v->frame * 0.125f);
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (v->trk < 0 && p->ch != PC_DDA)
        note += (kv(v, KT_TONE, 0.5f) - 0.5f) * 24.0f;
    f = mtof(note);
    c->hold = pce_vol(v->elev * fm_minf(v->vel, 1.0f));
    switch (p->ch) {
    case PC_WAVE: {
        int w = pce_wave(v);
        if (p->morph) {                /* the table rewritten as the note goes: the first, then the other */
            float m = fm_minf((float)v->frame * (0.02f + 0.2f * kv(v, KT_SPEED, 0.3f)), 1.0f);
            if (m > 0.5f)
                w = p->morph;
        }
        c->cnt2 = (uint32_t)w;
        f = chip_quant(f, 3579545.0f, 32.0f, 0.0f, 1.0f, 4095.0f);
        c->inc = f / sr;
        if (p->lfo) {
            float r = (float)p->lratio * 0.125f, d = (float)p->ldepth * 0.01f;
            if (has_kt(v->d, KT_SPEED) && !p->morph)
                r = r > 0.0f ? 0.5f + 3.5f * kv(v, KT_SPEED, 0.5f) : 0.0f;
            d = kv(v, KT_MOD, d);
            c->pw = r > 0.0f ? c->inc * r : (2.0f + 8.0f * (float)p->d.vspd * 0.01f) / sr;
            c->pwd = d * (r > 0.0f ? 0.9f : 0.06f);
        } else {
            c->pwd = 0.0f;
        }
        break;
    }
    case PC_NOISE: {
        int nf = (int)((note - 30.0f) * 0.5f);
        nf = nf < 0 ? 0 : nf > 31 ? 31 : nf;
        c->inc = 3579545.0f / (64.0f * (float)(32 - nf)) / sr;
        break;
    }
    default: {
        const bk_smp_t *s = bank_smp(p->smp);
        c->srate = (float)s->rate * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / sr;
        break;
    }
    }
}
static void pce_render(sv_t *v, uint32_t n, float *out)
{
    const pce_patch_t *p = (const pce_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float g = c->hold * 0.75f, ph = c->ph, inc = c->inc;
    uint32_t j;
    switch (p->ch) {
    case PC_WAVE: {
        const uint8_t *w = pce_wt[c->cnt2 % PCE_NW], *lw = pce_wt[p->lfo % PCE_NW];
        float mph = c->mod, minc = c->pw, dep = c->pwd;
        if (g <= 0.0f && v->est >= 3) {
            v->done = 1;
            return;
        }
        for (j = 0; j < n; j++) {
            float i2 = inc;
            if (dep > 0.0f) {          /* the LFO: the other channel's table bends this one's period */
                mph = wrap1(mph + minc);
                i2 = inc * (1.0f + dep * pce_tab(lw, mph));
                if (i2 < 0.0f)
                    i2 = 0.0f;
            }
            ph = wrap1(ph + i2);
            out[j] += pce_tab(w, ph) * g;
        }
        c->mod = mph;
        break;
    }
    case PC_NOISE:
        for (j = 0; j < n; j++)
            out[j] += noise_run(c, inc, lfsr_17) * g * 0.8f;
        break;
    case PC_DDA: {                     /* 5 bits a sample, held */
        const bk_smp_t *s = bank_smp(p->smp);
        float y = c->acc;
        for (j = 0; j < n; j++) {
            c->sfrac += c->srate;
            while (c->sfrac >= 1.0f) {
                c->sfrac -= 1.0f;
                if (++c->spos >= c->slen) {
                    v->done = 1;
                    break;
                }
                y = ((float)s->data[c->spos] - 15.5f) * (1.0f / 15.5f);
            }
            out[j] += y * g;
        }
        c->acc = y;
        break;
    }
    default: {                         /* the CD's ADPCM: 4 bits, its step adapting; looped where the bank says */
        const bk_smp_t *s = bank_smp(p->smp);
        float y = c->acc;
        for (j = 0; j < n; j++) {
            c->sfrac += c->srate;
            while (c->sfrac >= 1.0f) {
                uint32_t b;
                int nib;
                c->sfrac -= 1.0f;
                if (c->spos >= c->slen) {
                    if (s->loop == BK_NOLOOP) {
                        v->done = 1;
                        break;
                    }
                    c->spos = s->loop;
                    c->dlt = s->x0;
                    c->cnt = s->x1;
                }
                b = s->data[c->spos >> 1];
                nib = (int)((c->spos & 1u) ? b & 15u : b >> 4);
                c->spos++;
                y = (float)oki_step(nib, &c->dlt, &c->cnt) * (1.0f / 2048.0f);
            }
            out[j] += y * g * 1.2f;
        }
        c->acc = y;
        break;
    }
    }
    c->ph = ph;
    if (v->elev <= 0.0f && v->est >= 3)
        v->done = 1;
}

/* ------------------------------------------------------------------- the sounds --- */
#define PD(k0, k1, k2, k3, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct) {{k0, k1, k2, k3}, a, d, s, r, vib, vspd, vdly, sw, swt, arp, fl, oct}
static const pce_patch_t PCE_P[EN_NSOUND] = {
    {PD(KT_WAVE, KT_DECAY, KT_SUSTAIN, KT_BEND, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, -1), PC_WAVE, 0, {1, 2, 3, 4}, 0, 0, 0, 0, 0},
    {PD(KT_DECAY, KT_WAVE, KT_RELEASE, KT_BEND, 0, 30, 30, 8, 0, 30, 0, 0, 0, 0, 0, -1), PC_WAVE, 10, {11, 13, 9, 0}, 0, 0, 0, 0, 0},
    {PD(KT_WAVE, KT_DECAY, KT_SUSTAIN, KT_BEND, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, -1), PC_WAVE, 4, {5, 4, 6, 0}, 0, 0, 0, 0, 0},
    {PD(KT_MOD, KT_SPEED, KT_DECAY, KT_SUSTAIN, 0, 40, 55, 10, 0, 30, 0, 0, 0, 0, 0, -1), PC_WAVE, 6, {0}, 6, 8, 45, 0, 0},
    {PD(KT_WAVE, KT_DECAY, KT_RELEASE, KT_VIB, 0, 50, 0, 25, 0, 30, 30, 0, 0, 0, 0, 0), PC_WAVE, 8, {9, 10, 2, 0}, 0, 0, 0, 0, 0},
    {PD(KT_WAVE, KT_ATTACK, KT_RELEASE, KT_VIB, 10, 0, 100, 25, 0, 40, 20, 0, 0, 0, 0, 0), PC_WAVE, 7, {8, 3, 15, 0}, 0, 0, 0, 0, 0},
    {PD(KT_SPEED, KT_MOD, KT_DECAY, KT_RELEASE, 0, 55, 0, 35, 0, 30, 0, 0, 0, 0, 0, 0), PC_WAVE, 6, {0}, 6, 28, 70, 0, 0},
    {PD(KT_ATTACK, KT_RELEASE, KT_VIB, KT_OCTAVE, 25, 0, 100, 40, 0, 30, 25, 0, 0, 0, 0, 0), PC_CD, 0, {0}, 0, 0, 0, BK_PCE_CDSTR, 0},
    {PD(KT_WAVE, KT_VIB, KT_DECAY, KT_SUSTAIN, 0, 40, 70, 15, 30, 40, 25, 0, 0, 0, 0, 0), PC_WAVE, 11, {12, 1, 13, 15}, 0, 0, 0, 0, 0},
    {PD(KT_VIB, KT_BEND, KT_DECAY, KT_SUSTAIN, 0, 40, 75, 15, 30, 45, 20, 0, 0, 0, 0, 0), PC_WAVE, 12, {0}, 0, 0, 0, 0, 0},
    {PD(KT_MOD, KT_SPEED, KT_VIB, KT_RELEASE, 0, 40, 70, 20, 30, 40, 20, 0, 0, 0, 0, 0), PC_WAVE, 13, {0}, 6, 16, 30, 0, 0},
    {PD(KT_RELEASE, KT_VIB, KT_OCTAVE, KT_BEND, 0, 0, 100, 25, 0, 40, 30, 0, 0, 0, 0, 0), PC_CD, 0, {0}, 0, 0, 0, BK_PCE_CDGTR, 0},
    {PD(KT_OCTAVE, KT_RELEASE, KT_NONE, KT_NONE, 0, 0, 100, 5, 0, 30, 0, 0, 0, 0, DF_HOLD, 0), PC_DDA, 0, {0}, 0, 0, 0, BK_PCE_VOICE, 0},
    {PD(KT_ATTACK, KT_RELEASE, KT_VIB, KT_OCTAVE, 30, 0, 100, 40, 0, 30, 25, 0, 0, 0, 0, 0), PC_CD, 0, {0}, 0, 0, 0, BK_PCE_CDCHOIR, 0},
    {PD(KT_SWEEP, KT_DECAY, KT_TONE, KT_RELEASE, 0, 50, 0, 10, 0, 30, 0, -16, 0, 0, 0, 1), PC_NOISE, 0, {0}, 0, 0, 0, 0, 0},
    {PD(KT_SWEEP, KT_SPEED, KT_DECAY, KT_WAVE, 0, 45, 0, 8, 0, 30, 0, 12, 24, 0, 0, 0), PC_WAVE, 4, {5, 13, 7, 10}, 0, 0, 0, 0, 0},
};
static const en_sound_t PCE_SND[EN_NSOUND] = {
    {"Wave bass", "HuC6280 wavetable", {"Wave", "Decay", "Sustain", "Thump"}, {0, 40, 55, 0}, {4, 0, 0, 0}, &PCE_P[0]},
    {"Slap", "HuC6280 wavetable", {"Decay", "Wave", "Release", "Thump"}, {30, 0, 8, 30}, {0, 3, 0, 0}, &PCE_P[1]},
    {"Square", "HuC6280 wavetable", {"Wave", "Decay", "Sustain", "Thump"}, {0, 40, 55, 0}, {3, 0, 0, 0}, &PCE_P[2]},
    {"LFO bass", "HuC6280 LFO", {"Mod", "Ratio", "Decay", "Sustain"}, {45, 30, 40, 55}, {0, 0, 0, 0}, &PCE_P[3]},
    {"Wave piano", "HuC6280 wavetable", {"Wave", "Decay", "Release", "Vibrato"}, {0, 50, 25, 0}, {3, 0, 0, 0}, &PCE_P[4]},
    {"Organ", "HuC6280 wavetable", {"Wave", "Attack", "Release", "Vibrato"}, {0, 10, 25, 0}, {3, 0, 0, 0}, &PCE_P[5]},
    {"Bells", "HuC6280 LFO", {"Ratio", "Mod", "Decay", "Release"}, {60, 70, 55, 35}, {0, 0, 0, 0}, &PCE_P[6]},
    {"CD strings", "CD-ROM ADPCM", {"Attack", "Release", "Vibrato", "Octave"}, {25, 40, 0, 50}, {0, 0, 0, 3}, &PCE_P[7]},
    {"Wave lead", "HuC6280 wavetable", {"Wave", "Vibrato", "Decay", "Sustain"}, {0, 30, 40, 70}, {4, 0, 0, 0}, &PCE_P[8]},
    {"Shmup lead", "HuC6280 wavetable", {"Vibrato", "Thump", "Decay", "Sustain"}, {30, 20, 40, 75}, {0, 0, 0, 0}, &PCE_P[9]},
    {"LFO lead", "HuC6280 LFO", {"Mod", "Ratio", "Vibrato", "Release"}, {30, 40, 30, 20}, {0, 0, 0, 0}, &PCE_P[10]},
    {"CD guitar", "CD-ROM ADPCM", {"Release", "Vibrato", "Octave", "Bend"}, {25, 0, 50, 0}, {0, 0, 3, 0}, &PCE_P[11]},
    {"DDA voice", "5-bit DDA", {"Octave", "Release", "", ""}, {50, 5, 0, 0}, {3, 0, 0, 0}, &PCE_P[12]},
    {"CD choir", "CD-ROM ADPCM", {"Attack", "Release", "Vibrato", "Octave"}, {30, 40, 0, 50}, {0, 0, 0, 3}, &PCE_P[13]},
    {"Noise sweep", "HuC6280 noise", {"Sweep", "Decay", "Tone", "Release"}, {30, 50, 50, 10}, {0, 0, 0, 0}, &PCE_P[14]},
    {"Power-up", "HuC6280 sweep", {"Sweep", "Speed", "Decay", "Wave"}, {75, 40, 45, 0}, {0, 0, 0, 4}, &PCE_P[15]},
};
static int pce_wave_text(int snd, int pos, char *b)
{
    const pce_patch_t *p = (const pce_patch_t *)PCE_SND[(unsigned)snd % EN_NSOUND].patch;
    if (!p->waves[pos & 3])
        return 0;
    scopy(b, PCE_WN[(p->waves[pos & 3] - 1) % PCE_NW], 10);
    return 1;
}

#define PDR(ch, w, dec, sw, swt, smp) {PD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, dec, 0, 0, 0, 0, 0, sw, swt, 0, DF_NOREL, 0), ch, w, {0}, 0, 0, 0, smp, 0}
#define PDG(smp) {PD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0), PC_DDA, 0, {0}, 0, 0, 0, smp, 0}
static const pce_patch_t PCE_DP[2][EN_NPART] = {
    {PDR(PC_WAVE, 6, 30, -40, 6, 0), PDR(PC_NOISE, 0, 30, 0, 0, 0), PDR(PC_WAVE, 6, 32, -12, 10, 0), PDR(PC_WAVE, 6, 30, -12, 10, 0),
     PDR(PC_WAVE, 6, 28, -12, 10, 0), PDR(PC_NOISE, 0, 6, 0, 0, 0), PDR(PC_NOISE, 0, 24, 0, 0, 0), PDR(PC_NOISE, 0, 10, 0, 0, 0),
     PDR(PC_NOISE, 0, 34, 0, 0, 0), PDR(PC_NOISE, 0, 52, 0, 0, 0), PDR(PC_WAVE, 12, 30, -48, 0, 0)},
    {PDG(BK_PCE_KICK), PDG(BK_PCE_SNARE), PDG(BK_PCE_TOM), PDG(BK_PCE_TOM), PDG(BK_PCE_TOM), PDG(BK_PCE_RIM),
     PDG(BK_PCE_CLAP), PDG(BK_PCE_CHAT), PDG(BK_PCE_OHAT), PDG(BK_PCE_CRASH), PDG(BK_PCE_VOICE)},
};
#undef PDR
#undef PDG
#undef PD
static const dkit_t PCE_KIT[EN_NKIT] = {
    {"PSG", {{"KCK", "Kick", &PCE_DP[0][0], 45, 0, 0}, {"SNR", "Snare", &PCE_DP[0][1], 84, 0, 0},
             {"LT", "Low tom", &PCE_DP[0][2], 50, -40, 0}, {"MT", "Mid tom", &PCE_DP[0][3], 56, 0, 0},
             {"HT", "High tom", &PCE_DP[0][4], 62, 40, 0}, {"RIM", "Rim", &PCE_DP[0][5], 96, 20, 0},
             {"CLP", "Clap", &PCE_DP[0][6], 80, -20, 0}, {"CH", "Closed hat", &PCE_DP[0][7], 96, 30, 1},
             {"OH", "Open hat", &PCE_DP[0][8], 96, 30, 1}, {"CRS", "Crash", &PCE_DP[0][9], 86, -30, 0},
             {"ZAP", "Zap", &PCE_DP[0][10], 84, 0, 0}}},
    {"DDA", {{"KCK", "Kick", &PCE_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &PCE_DP[1][1], 60, 0, 0},
             {"LT", "Low tom", &PCE_DP[1][2], 55, -40, 0}, {"MT", "Mid tom", &PCE_DP[1][3], 60, 0, 0},
             {"HT", "High tom", &PCE_DP[1][4], 65, 40, 0}, {"RIM", "Rim", &PCE_DP[1][5], 60, 20, 0},
             {"CLP", "Clap", &PCE_DP[1][6], 60, -20, 0}, {"CH", "Closed hat", &PCE_DP[1][7], 60, 30, 1},
             {"OH", "Open hat", &PCE_DP[1][8], 60, 30, 1}, {"CRS", "Crash", &PCE_DP[1][9], 60, -30, 0},
             {"HO", "Ho!", &PCE_DP[1][10], 55, 0, 0}}},
};
static const engine_t PCE_ENG = {44100.0f, 60.0f, pce_start, pce_frame, pce_render, 0};
