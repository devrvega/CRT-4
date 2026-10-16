/* SPDX-License-Identifier: GPL-3.0-only */
/* The NES (and the Famicom's expansion chips). The 2A03's channels as the hardware made them:
 *   pulse      an 11-bit period, duty 12.5 / 25 / 50 / 75 %, a 4-bit volume, its DAC's curve
 *   triangle   32 steps of 4 bits, no volume at all (a note is on or cut), never reset
 *   noise      a 15-bit register (or its 93-step metallic mode) at one of sixteen rates
 *   DMC        1-bit delta samples: a 7-bit counter stepped up or down two
 * and from the Famicom's cartridges: the VRC6 (pulses of eight duties, a sawtooth of seven steps that folds
 * over when it is driven), the FDS (a 64-step wavetable, its modulator, its soft low-pass), the N163 (4-bit
 * wavetables) and the 5B (squares, and its envelope run fast enough to be a buzzing waveform). Its periods
 * are rounded as the registers did (high notes go a little out of tune), its music driver runs at 60 frames
 * a second (50 on a PAL machine). The output: the Famicom's low-pass, or a TV's speaker. Included once. */

enum { NES_PULSE, NES_TRI, NES_NOISE, NES_DMC, NES_VRC6P, NES_SAW, NES_FDS, NES_N163, NES_5B };
typedef struct {
    drv_t d;
    uint8_t ch;                        /* NES_* */
    uint8_t duty;                      /* the duty (pulse 0..3, VRC6 0..7), the wave (FDS, N163), the noise rate */
    uint8_t dseq;                      /* a duty sequence (1: 12.5-25-50-25 ..., 2: 50 then 25) */
    uint8_t nshort;                    /* the noise's metallic mode */
    uint8_t smp;                       /* a DMC sample (the bank's) */
    uint8_t tri;                       /* a drum: the triangle under the noise (its pitch drop in eighths) */
    uint8_t echo;                      /* a lead's own short echo, in frames */
} nes_patch_t;

static float nes_cpu = 1789773.0f;
static const float NES_DUTY[4] = {0.125f, 0.25f, 0.5f, 0.75f};
static const uint16_t NES_NOISE_PER[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
static uint8_t fds_wave[4][64];        /* 6-bit tables, made at the start */
static uint8_t n163_wave[4][32];       /* 4-bit */

/* the DACs: one pulse alone, and the triangle / noise / DMC side (the 2A03's resistor ladders), made
 * relative to a pulse at full volume */
static inline float nes_dac_p(float v15) { return v15 <= 0.0f ? 0.0f : (95.88f / (8128.0f / v15 + 100.0f)) * 6.6921f; }
static inline float nes_dac_t(float v15) { return v15 <= 0.0f ? 0.0f : (159.79f / (8227.0f / v15 + 100.0f)) * 6.6921f; }
static inline float nes_dac_n(float v15) { return v15 <= 0.0f ? 0.0f : (159.79f / (12241.0f / v15 + 100.0f)) * 6.6921f; }

static void nes_tables(void)
{
    int i, w;
    for (i = 0; i < 64; i++) {         /* FDS: organ, flute, strings (a soft saw), a hollow square */
        float x = (float)i * (1.0f / 64.0f), s[4];
        s[0] = 0.55f * sine(x) + 0.3f * sine(2.0f * x) + 0.2f * sine(3.0f * x) + 0.12f * sine(4.0f * x) + 0.1f * sine(6.0f * x);
        s[1] = 0.85f * sine(x) + 0.12f * sine(2.0f * x) + 0.05f * sine(3.0f * x);
        s[2] = 0.6f * sine(x) + 0.3f * sine(2.0f * x) + 0.2f * sine(3.0f * x) + 0.15f * sine(4.0f * x) + 0.1f * sine(5.0f * x) + 0.07f * sine(6.0f * x);
        s[3] = 0.7f * sine(x) + 0.23f * sine(3.0f * x) + 0.14f * sine(5.0f * x) + 0.1f * sine(7.0f * x);
        for (w = 0; w < 4; w++) {
            int q = (int)((s[w] * 0.95f + 1.0f) * 31.5f + 0.5f);
            fds_wave[w][i] = (uint8_t)(q < 0 ? 0 : q > 63 ? 63 : q);
        }
    }
    for (i = 0; i < 32; i++) {         /* N163: a bright bell, a reedy organ, a soft saw, a narrow pulse */
        float x = (float)i * (1.0f / 32.0f), s[4];
        s[0] = 0.6f * sine(x) + 0.35f * sine(3.0f * x) + 0.3f * sine(5.0f * x + 0.2f);
        s[1] = 0.5f * sine(x) + 0.45f * sine(2.0f * x) + 0.3f * sine(4.0f * x);
        s[2] = 1.0f - 2.0f * x;
        s[3] = x < 0.18f ? 0.9f : -0.25f;
        for (w = 0; w < 4; w++) {
            int q = (int)((fm_clampf(s[w], -1.0f, 1.0f) + 1.0f) * 7.5f + 0.5f);
            n163_wave[w][i] = (uint8_t)(q < 0 ? 0 : q > 15 ? 15 : q);
        }
    }
}

static void nes_start(sv_t *v, int legato)
{
    const nes_patch_t *p = (const nes_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    if (legato)
        return;
    if (p->ch == NES_PULSE || p->ch == NES_VRC6P)
        c->ph = 0.0f;                  /* a new note restarts the duty's sequencer (a click, as on the console) */
    if (!c->lfsr)
        c->lfsr = 1u;
    c->cnt = 0;
    c->acc = 0.0f;
    if (p->ch == NES_DMC) {
        int w = has_kt(v->d, KT_WAVE) ? (int)(kv(v, KT_WAVE, 0.0f) * 2.99f) : 0;
        const bk_smp_t *s = bank_smp(p->smp + w);
        c->cnt2 = (uint32_t)(p->smp + w);
        c->smp = s->data;
        c->slen = s->nsamp;
        c->spos = 0;
        c->sfrac = 0.0f;
        c->dlt = s->x0;             /* the counter's start (a $4011 write) */
    }
    c->mod = 0.0f;
}

static void nes_frame(sv_t *v)
{
    const nes_patch_t *p = (const nes_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float note = v->pitch + v->poff, f, vol = v->elev * fm_minf(v->vel, 1.0f), sr = eng->rate;
    if (has_kt(v->d, KT_BEND)) {       /* a bend into the note: the bass's thump */
        float b = kv(v, KT_BEND, 0.0f);
        if (v->frame < 8)
            note += b * 12.0f * (1.0f - (float)v->frame * 0.125f);
    }
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    f = mtof(note);
    c->cnt++;
    switch (p->ch) {
    case NES_PULSE: {
        int d = p->duty;
        if (has_kt(v->d, KT_WAVE))
            d = (int)(kv(v, KT_WAVE, 0.0f) * 3.99f);
        if (p->dseq == 1) {            /* a duty that moves: 12.5, 25, 50, 25 ... (Speed: frames each) */
            static const uint8_t SEQ[4] = {0, 1, 2, 1};
            int spd = 1 + (int)(kv(v, KT_SPEED, 0.3f) * 7.0f);
            d = SEQ[(v->frame / (uint16_t)spd) & 3u];
        } else if (p->dseq == 2) {     /* 50 for the attack, then 25 */
            d = v->frame < 3 ? 2 : 1;
        }
        c->pw = NES_DUTY[d & 3];
        f = chip_quant(f, nes_cpu, 16.0f, 1.0f, 8.0f, 2047.0f);
        c->inc = f / sr;
        c->acc = nes_dac_p((float)(int)(vol * 15.0f + 0.5f));
        if (p->echo) {                 /* its own echo: the other pulse, so many frames behind, at half volume */
            int w = (int)(c->cnt & 15u), r = (int)((c->cnt - p->echo) & 15u);
            c->wt[w] = vol;
            c->wt[16 + w] = c->inc;
            c->hold = c->cnt > p->echo ? nes_dac_p((float)(int)(c->wt[r] * 0.5f * 15.0f + 0.5f)) : 0.0f;
            c->ph2 = c->wt[16 + r];
            v->done = (uint8_t)(v->done && c->hold <= 0.0f);   /* the echo outlasts the note */
        }
        break;
    }
    case NES_TRI:
        f = chip_quant(f, nes_cpu, 32.0f, 1.0f, 2.0f, 2047.0f);
        c->inc = f / sr;
        c->acc = v->elev > 0.02f ? nes_dac_t(15.0f) * 0.5f : 0.0f;   /* on or off: it has no volume */
        if (p->tri && v->frame < 6)                                   /* a kick: the triangle dropped fast */
            c->inc *= fm_exp2f((float)p->tri * 0.125f * (float)(6 - (int)v->frame) * (1.0f / 12.0f) *
                               (0.5f + kv(v, KT_TONE, 0.5f)));
        break;
    case NES_NOISE: {
        int idx = (int)p->duty + (int)((note - 60.0f) * -0.5f);
        if (has_kt(v->d, KT_TONE))
            idx += (int)((0.5f - kv(v, KT_TONE, 0.5f)) * 8.0f);
        idx = idx < 0 ? 0 : idx > 15 ? 15 : idx;
        c->inc = (nes_cpu / (float)NES_NOISE_PER[idx]) / sr;
        c->acc = nes_dac_n((float)(int)(vol * 15.0f + 0.5f)) * 0.5f;
        if (p->tri) {                                                 /* a snare's body: a pulse, two frames */
            c->pw = v->frame < 2 ? 0.5f : 0.0f;
            c->ph2 = mtof(note - 14.0f) / sr;
        }
        break;
    }
    case NES_DMC: {
        const bk_smp_t *s = bank_smp((int)c->cnt2);
        c->srate = (float)s->rate * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / sr;
        c->acc = v->elev > 0.02f ? 1.0f : 0.0f;
        break;
    }
    case NES_VRC6P: {
        int d = p->duty;
        if (has_kt(v->d, KT_WAVE))
            d = (int)(kv(v, KT_WAVE, 0.0f) * 7.99f);
        c->pw = (float)(d + 1) * (1.0f / 16.0f);
        f = chip_quant(f, nes_cpu, 16.0f, 1.0f, 8.0f, 4095.0f);
        c->inc = f / sr;
        c->acc = (float)(int)(vol * 15.0f + 0.5f) * (1.0f / 15.0f);
        break;
    }
    case NES_SAW: {                    /* the rate added seven times a turn: above 42 it folds over */
        float a = (float)(int)(vol * (42.0f + 21.0f * kv(v, KT_DRIVE, 0.0f)) + 0.5f);
        f = chip_quant(f, nes_cpu, 14.0f, 1.0f, 8.0f, 4095.0f);
        c->inc = f / sr;
        c->acc = a;
        break;
    }
    case NES_FDS: {
        int w = p->duty;
        if (has_kt(v->d, KT_WAVE))
            w = (int)(kv(v, KT_WAVE, 0.0f) * 3.99f);
        c->cnt2 = (uint32_t)w;
        c->inc = f / sr;
        c->acc = vol;
        c->pwd = kv(v, KT_MOD, 0.2f);  /* the modulator's depth, and its rate (slow, or the note's) */
        {
            float spd = kv(v, KT_SPEED, 0.3f);
            c->pw = spd < 0.5f ? (1.0f + 14.0f * spd * 2.0f) / sr : f * (spd < 0.75f ? 1.0f : 2.0f) / sr;
        }
        break;
    }
    case NES_N163: {
        int w = p->duty;
        if (has_kt(v->d, KT_WAVE))
            w = (int)(kv(v, KT_WAVE, 0.0f) * 3.99f);
        c->cnt2 = (uint32_t)w;
        c->inc = f / sr;
        c->acc = (float)(int)(vol * 15.0f + 0.5f) * (1.0f / 15.0f);
        break;
    }
    case NES_5B: {
        float det = (kv(v, KT_DETUNE, 0.0f)) * 0.3f;
        f = chip_quant(f, nes_cpu, 16.0f, 0.0f, 1.0f, 4095.0f);
        c->inc = f / sr;
        c->ph2 = mtof(note + det) / sr;                      /* the envelope, run as a waveform */
        c->acc = vol_db(vol, 1.5f, 31);                     /* 1.5 dB steps */
        c->pw = kv(v, KT_MOD, 0.5f);                         /* the square's share */
        c->pwd = kv(v, KT_WAVE, 0.0f) < 0.5f ? 0.0f : 1.0f;  /* the envelope: a saw, or a triangle */
        break;
    }
    default: break;
    }
}

static void nes_render(sv_t *v, uint32_t n, float *out)
{
    const nes_patch_t *p = (const nes_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float g = c->acc, inc = c->inc, ph = c->ph;
    uint32_t j;
    switch (p->ch) {
    case NES_PULSE:
    case NES_VRC6P: {
        float w = c->pw;
        if (p->echo && c->hold > 0.0f) {                     /* the echo's pulse */
            float e = c->hold, i2 = c->ph2, q = c->mod;
            for (j = 0; j < n; j++) {
                q = wrap1(q + i2);
                out[j] += chip_pulse(q, i2, w) * e;
            }
            c->mod = q;
        }
        if (g <= 0.0f) {
            c->ph = wrap1(ph + inc * (float)n);
            if (p->echo && v->elev <= 0.0f && v->est >= 3 && c->hold > 0.0f)
                return;                                        /* the echo still rings */
            break;
        }
        for (j = 0; j < n; j++) {
            ph = wrap1(ph + inc);
            out[j] += chip_pulse(ph, inc, w) * g;
        }
        break;
    }
    case NES_TRI:
        for (j = 0; j < n; j++) {      /* it runs on whether it sounds or not */
            ph = wrap1(ph + inc);
            if (g > 0.0f)
                out[j] += tri32(ph) * g;
        }
        break;
    case NES_NOISE: {
        lfsr_fn st = p->nshort ? lfsr_nes_short : lfsr_nes;
        float w = c->pw, i2 = c->ph2;
        for (j = 0; j < n; j++) {
            float y = noise_run(c, inc * (1.0f / 1.0f) / 1.0f, st) * g;
            if (w > 0.0f) {
                ph = wrap1(ph + i2);
                y += chip_pulse(ph, i2, w) * 0.6f * g;
            }
            out[j] += y;
        }
        break;
    }
    case NES_DMC: {
        const bk_smp_t *s = bank_smp((int)c->cnt2);
        float y = (float)(c->dlt - 64) * (1.0f / 64.0f);
        for (j = 0; j < n; j++) {
            c->sfrac += c->srate;
            while (c->sfrac >= 1.0f) { /* a bit: the counter up or down two */
                uint32_t b;
                c->sfrac -= 1.0f;
                if (c->spos >= c->slen) {
                    if (s->loop != BK_NOLOOP) {
                        c->spos = s->loop;
                    } else {
                        v->done = 1;
                        break;
                    }
                }
                b = (c->smp[c->spos >> 3] >> (c->spos & 7u)) & 1u;
                c->spos++;
                if (b) {
                    if (c->dlt <= 125)
                        c->dlt += 2;
                } else if (c->dlt >= 2) {
                    c->dlt -= 2;
                }
                y = (float)(c->dlt - 64) * (1.0f / 64.0f);
            }
            out[j] += y * 0.9f * g;
        }
        if (v->elev <= 0.0f)
            v->done = 1;
        return;
    }
    case NES_SAW: {
        float a = g;                   /* the accumulator's top five bits: a 7-step ramp, folding past 42 */
        if (a <= 0.0f) {
            c->ph = wrap1(ph + inc * (float)n);
            return;
        }
        for (j = 0; j < n; j++) {
            int s;
            ph = wrap1(ph + inc);
            s = (int)(ph * 7.0f);
            out[j] += ((float)(((int)(a * (float)s)) & 0xFF) * (1.0f / 8.0f) - a * 0.375f) * (1.0f / 31.0f) * 1.3f;
        }
        break;
    }
    case NES_FDS: {
        const uint8_t *w = fds_wave[c->cnt2 & 3u];
        float mph = c->mod, minc = c->pw, depth = c->pwd * 0.06f, lp = c->lp, k = 0.25f;
        for (j = 0; j < n; j++) {
            float m;
            mph = wrap1(mph + minc);
            m = mph < 0.5f ? 4.0f * mph - 1.0f : 3.0f - 4.0f * mph;   /* the mod table: a triangle */
            ph = wrap1(ph + inc * (1.0f + depth * m * 8.0f));
            lp += k * (((float)w[(int)(ph * 64.0f) & 63] - 31.5f) * (1.0f / 31.5f) * g - lp);   /* its low-pass */
            out[j] += lp * 0.8f;
        }
        c->mod = mph;
        c->lp = lp;
        break;
    }
    case NES_N163: {
        const uint8_t *w = n163_wave[c->cnt2 & 3u];
        if (g <= 0.0f) {
            c->ph = wrap1(ph + inc * (float)n);
            return;
        }
        for (j = 0; j < n; j++) {
            ph = wrap1(ph + inc);
            out[j] += wave4(w, ph) * 0.5f * g;
        }
        break;
    }
    case NES_5B: {
        float e = c->ph2, ph2 = c->mod, sq = c->pw, tri = c->pwd;
        for (j = 0; j < n; j++) {
            float env;
            ph = wrap1(ph + inc);
            ph2 = wrap1(ph2 + e);
            env = tri > 0.5f ? (ph2 < 0.5f ? 2.0f * ph2 : 2.0f - 2.0f * ph2) : 1.0f - ph2;   /* the envelope's shape */
            env = (float)(int)(env * 31.0f) * (1.0f / 31.0f);                               /* in its 32 steps */
            out[j] += (env - 0.5f) * (1.0f - 0.5f * sq) * g + chip_pulse(ph, inc, 0.5f) * sq * g * 0.6f;
        }
        c->mod = ph2;
        break;
    }
    default: break;
    }
    c->ph = ph;
    if (v->elev <= 0.0f && v->est >= 3 && !(p->ch == NES_PULSE && p->echo && c->hold > 0.0f))
        v->done = 1;
}

/* the lead's own echo: the driver hears it as a second voice; here only its delay (in frames, x2) */

/* ------------------------------------------------------------------- the sounds --- */
#define D(k0, k1, k2, k3, a, d, s, r) {{k0, k1, k2, k3}, a, d, s, r, 0, 30, 0, 0, 0, 0, 0, 0}
static const nes_patch_t NES_P[EN_NSOUND] = {
    /* basses */
    {{{KT_DECAY, KT_BEND, KT_VIB, KT_OCTAVE}, 0, 0, 0, 0, 0, 30, 20, 0, 0, 0, DF_NOREL, -1}, NES_TRI, 0, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_DECAY, KT_SUSTAIN, KT_BEND}, 0, 35, 40, 5, 0, 30, 20, 0, 0, 0, 0, -1}, NES_PULSE, 1, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_RELEASE, KT_VIB, KT_OCTAVE}, 0, 0, 100, 0, 0, 30, 20, 0, 0, 0, DF_NOREL, -1}, NES_DMC, 0, 0, 0, BK_NES_BASS, 0, 0},
    {{{KT_DRIVE, KT_DECAY, KT_SUSTAIN, KT_BEND}, 0, 40, 55, 10, 0, 30, 20, 0, 0, 0, 0, -1}, NES_SAW, 0, 0, 0, 0, 0, 0},
    /* keys */
    {{{KT_WAVE, KT_ARPSPD, KT_ARPMODE, KT_DECAY}, 0, 55, 45, 15, 0, 30, 0, 0, 0, 1, DF_AUTOARP, 0}, NES_PULSE, 2, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_DECAY, KT_RELEASE, KT_VIB}, 0, 45, 0, 20, 0, 30, 30, 0, 0, 1, 0, 0}, NES_PULSE, 2, 2, 0, 0, 0, 0},
    {{{KT_WAVE, KT_MOD, KT_SPEED, KT_ATTACK}, 15, 0, 100, 25, 0, 30, 0, 0, 0, 1, 0, 0}, NES_FDS, 0, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_ATTACK, KT_DECAY, KT_VIB}, 0, 50, 50, 20, 0, 30, 30, 0, 0, 1, 0, 0}, NES_N163, 1, 0, 0, 0, 0, 0},
    /* leads */
    {{{KT_VIB, KT_DECAY, KT_SUSTAIN, KT_BEND}, 0, 40, 70, 15, 35, 40, 25, 0, 0, 1, 0, 0}, NES_PULSE, 0, 0, 0, 0, 0, 0},
    {{{KT_VIB, KT_DECAY, KT_SUSTAIN, KT_RELEASE}, 0, 40, 70, 20, 35, 40, 25, 0, 0, 1, 0, 0}, NES_PULSE, 1, 0, 0, 0, 0, 0},
    {{{KT_SPEED, KT_VIB, KT_DECAY, KT_SUSTAIN}, 0, 40, 70, 15, 30, 40, 25, 0, 0, 1, 0, 0}, NES_PULSE, 0, 1, 0, 0, 0, 0},
    {{{KT_WAVE, KT_VIB, KT_DECAY, KT_RELEASE}, 0, 40, 60, 25, 30, 40, 25, 0, 0, 1, 0, 0}, NES_PULSE, 1, 0, 0, 0, 0, 6},
    /* its own */
    {{{KT_SWEEP, KT_WAVE, KT_DECAY, KT_SUSTAIN}, 0, 45, 0, 5, 0, 30, 0, -24, 0, 1, 0, 0}, NES_PULSE, 1, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_VIB, KT_DECAY, KT_SUSTAIN}, 0, 40, 70, 15, 30, 40, 25, 0, 0, 1, 0, 0}, NES_VRC6P, 3, 0, 0, 0, 0, 0},
    {{{KT_WAVE, KT_MOD, KT_DECAY, KT_DETUNE}, 0, 45, 50, 10, 0, 30, 0, 0, 0, 1, 0, -1}, NES_5B, 0, 0, 0, 0, 0, 0},
    {{{KT_DECAY, KT_OCTAVE, KT_RELEASE, KT_NONE}, 0, 0, 100, 0, 0, 30, 0, 0, 0, 1, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_HIT, 0, 0},
};
static const en_sound_t NES_SND[EN_NSOUND] = {
    {"Triangle", "2A03 triangle", {"Cut", "Thump", "Vibrato", "Octave"}, {70, 30, 0, 50}, {0, 0, 0, 3}, &NES_P[0]},
    {"Pulse", "2A03 pulse", {"Duty", "Decay", "Sustain", "Thump"}, {33, 40, 35, 0}, {4, 0, 0, 0}, &NES_P[1]},
    {"DMC bass", "1-bit samples", {"Wave", "Release", "Vibrato", "Octave"}, {0, 10, 0, 50}, {3, 0, 0, 3}, &NES_P[2]},
    {"VRC6 saw", "Konami VRC6", {"Grit", "Decay", "Sustain", "Thump"}, {0, 40, 55, 0}, {0, 0, 0, 0}, &NES_P[3]},
    {"Arp chords", "2A03 arpeggios", {"Duty", "Speed", "Mode", "Decay"}, {66, 0, 0, 55}, {4, 6, 4, 0}, &NES_P[4]},
    {"Pulse piano", "2A03 pulse", {"Duty", "Decay", "Release", "Vibrato"}, {66, 45, 20, 0}, {4, 0, 0, 0}, &NES_P[5]},
    {"FDS wave", "Famicom Disk System", {"Wave", "Mod", "Speed", "Attack"}, {0, 20, 30, 15}, {4, 0, 0, 0}, &NES_P[6]},
    {"N163 wave", "Namco 163", {"Wave", "Attack", "Decay", "Vibrato"}, {33, 0, 50, 20}, {4, 0, 0, 0}, &NES_P[7]},
    {"12.5% lead", "2A03 pulse", {"Vibrato", "Decay", "Sustain", "Thump"}, {35, 40, 70, 0}, {0, 0, 0, 0}, &NES_P[8]},
    {"25% lead", "2A03 pulse", {"Vibrato", "Decay", "Sustain", "Release"}, {35, 40, 70, 20}, {0, 0, 0, 0}, &NES_P[9]},
    {"Duty sweep", "2A03 pulse", {"Speed", "Vibrato", "Decay", "Sustain"}, {30, 30, 40, 70}, {0, 0, 0, 0}, &NES_P[10]},
    {"Echo lead", "2A03 pulses", {"Duty", "Vibrato", "Decay", "Release"}, {33, 30, 40, 25}, {4, 0, 0, 0}, &NES_P[11]},
    {"Sweep zap", "2A03 sweep", {"Sweep", "Duty", "Decay", "Sustain"}, {20, 33, 45, 0}, {0, 4, 0, 0}, &NES_P[12]},
    {"VRC6 lead", "Konami VRC6", {"Duty", "Vibrato", "Decay", "Sustain"}, {43, 30, 40, 70}, {8, 0, 0, 0}, &NES_P[13]},
    {"5B buzz", "Sunsoft 5B", {"Shape", "Square", "Decay", "Detune"}, {0, 50, 45, 0}, {2, 0, 0, 0}, &NES_P[14]},
    {"DMC hit", "1-bit samples", {"Decay", "Octave", "Release", ""}, {100, 50, 0, 0}, {0, 3, 0, 0}, &NES_P[15]},
};

/* the kits: the 2A03's (the triangle's kick, the noise's snares and hats), and one of DMC samples with the
 * noise channel's hats, as the later games did */
#define NOISE_DRUM(rate, sh, dec, tri) {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, dec, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_NOISE, rate, 0, sh, 0, tri, 0}
static const nes_patch_t NES_DP[2][EN_NPART] = {
    {
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_TRI, 0, 0, 0, 0, 32, 0},
        NOISE_DRUM(7, 0, 32, 1),
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 35, 0, 0, 0, 0, 0, -10, 10, 0, DF_NOREL, 0}, NES_TRI, 0, 0, 0, 0, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 33, 0, 0, 0, 0, 0, -10, 10, 0, DF_NOREL, 0}, NES_TRI, 0, 0, 0, 0, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 31, 0, 0, 0, 0, 0, -10, 10, 0, DF_NOREL, 0}, NES_TRI, 0, 0, 0, 0, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_PULSE, 1, 0, 0, 0, 0, 0},
        NOISE_DRUM(5, 0, 26, 0),
        NOISE_DRUM(1, 0, 12, 0),
        NOISE_DRUM(1, 0, 36, 0),
        NOISE_DRUM(3, 0, 52, 0),
        NOISE_DRUM(4, 1, 30, 0),
    },
    {
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_KICK, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_SNARE, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_TOM, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_TOM, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_TIMP, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_RIM, 0, 0},
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_CLAP, 0, 0},
        NOISE_DRUM(0, 0, 12, 0),
        NOISE_DRUM(0, 0, 38, 0),
        NOISE_DRUM(2, 0, 55, 0),
        {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, DF_NOREL, 0}, NES_DMC, 0, 0, 0, BK_NES_HIT, 0, 0},
    },
};
static const dkit_t NES_KIT[EN_NKIT] = {
    {"2A03", {{"KCK", "Kick", &NES_DP[0][0], 38, 0, 0}, {"SNR", "Snare", &NES_DP[0][1], 62, 0, 0},
              {"LT", "Low tom", &NES_DP[0][2], 50, 0, 0}, {"MT", "Mid tom", &NES_DP[0][3], 55, 0, 0},
              {"HT", "High tom", &NES_DP[0][4], 60, 0, 0}, {"BLP", "Blip", &NES_DP[0][5], 84, 0, 0},
              {"CLP", "Clap", &NES_DP[0][6], 60, 0, 0}, {"CH", "Closed hat", &NES_DP[0][7], 60, 0, 1},
              {"OH", "Open hat", &NES_DP[0][8], 60, 0, 1}, {"CRS", "Crash", &NES_DP[0][9], 60, 0, 0},
              {"MTL", "Metal", &NES_DP[0][10], 60, 0, 0}}},
    {"DMC", {{"KCK", "Kick", &NES_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &NES_DP[1][1], 60, 0, 0},
             {"LT", "Low tom", &NES_DP[1][2], 55, 0, 0}, {"HT", "High tom", &NES_DP[1][3], 62, 0, 0},
             {"TMP", "Timpani", &NES_DP[1][4], 60, 0, 0}, {"RIM", "Rim", &NES_DP[1][5], 60, 0, 0},
             {"CLP", "Clap", &NES_DP[1][6], 60, 0, 0}, {"CH", "Closed hat", &NES_DP[1][7], 60, 0, 1},
             {"OH", "Open hat", &NES_DP[1][8], 60, 0, 1}, {"CRS", "Crash", &NES_DP[1][9], 60, 0, 0},
             {"HIT", "Orchestra hit", &NES_DP[1][10], 60, 0, 0}}},
};
#undef NOISE_DRUM
#undef D

static void nes_init(void) { nes_tables(); }
static void nes_control(void)
{
    int pal = fx_pos(2);
    nes_cpu = pal ? 1662607.0f : 1789773.0f;
    drv_hz = pal ? 50.007f : 60.0988f;
}
static int nes_wave_text(int snd, int pos, char *b)
{
    static const char *const DUTY[4] = {"12.5%", "25%", "50%", "75%"};
    static const char *const FDS[4] = {"Organ", "Flute", "Strings", "Hollow"};
    static const char *const N163[4] = {"Bell", "Reed", "Saw", "Narrow"};
    static const char *const DMC[3] = {"Round", "Hollow", "Slap"};
    static const char *const SHP[2] = {"Saw", "Triangle"};
    static const char *const V6[8] = {"1/16", "2/16", "3/16", "4/16", "5/16", "6/16", "7/16", "8/16"};
    const nes_patch_t *p = (const nes_patch_t *)NES_SND[(unsigned)snd % EN_NSOUND].patch;
    switch (p->ch) {
    case NES_PULSE: scopy(b, DUTY[pos & 3], 10); return 1;
    case NES_VRC6P: scopy(b, V6[pos & 7], 10); return 1;
    case NES_FDS: scopy(b, FDS[pos & 3], 10); return 1;
    case NES_N163: scopy(b, N163[pos & 3], 10); return 1;
    case NES_DMC: scopy(b, DMC[pos % 3], 10); return 1;
    case NES_5B: scopy(b, SHP[pos & 1], 10); return 1;
    default: return 0;
    }
}
static const engine_t NES_ENG = {44100.0f, 60.0988f, nes_start, nes_frame, nes_render, 0};
