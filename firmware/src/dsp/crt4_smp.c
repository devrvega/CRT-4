/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample consoles: a voice plays a sample from the bank in the console's own format, decoding it a
 * block at a time as the console's DSP did, at a pitch (a step through it), under the console's envelope.
 *   SNES         BRR (16 samples in 9 bytes), the S-DSP at 32 kHz, its 4-point Gaussian interpolation (a
 *                soft top: the SNES's sound), its ADSR stepped by its rate table, the GAIN for releases;
 *                notes no more than two octaves over a sample's root (its pitch register's limit)
 *   PlayStation  the SPU's ADPCM (28 in 16 bytes), at 44.1 kHz, the same Gaussian interpolation, its
 *                exponential envelope; two octaves up at most
 *   N64          its ADPCM kind (16 in 9 bytes, the sample's own predictors), mixed at 22 kHz (or 32), a
 *                cubic between samples, the audio library's straight-line envelope; an octave up at most
 * Included once. */

typedef struct {
    drv_t d;                           /* its att, dec, sus, rel: the envelope (0..100) */
    uint16_t smp;                      /* the bank's sample */
    uint8_t srate;                     /* the sustain's fall (0: held) */
    uint8_t slice;                     /* the break's slice + 1 (0: the whole sample) */
    int8_t pdrop;                      /* a drum's pitch, falling from this many semitones over */
    uint8_t gain;                      /* its level, x 1/64 */
} smp_patch_t;

static float gauss[256][4];            /* the Gaussian's four weights, for 256 places between samples */
static const uint16_t SNES_RATE[32] = {0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
                                       64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};

static void smp_init(void)
{
    int f, k;
    for (f = 0; f < 256; f++) {        /* a Gaussian of sigma 0.63 samples (the S-DSP's, near enough), summing to 1 */
        float x = (float)f / 256.0f, w[4], s = 0.0f;
        for (k = 0; k < 4; k++) {
            float d = (float)(k - 1) - x;
            w[k] = fm_expf(-1.255f * d * d);
            s += w[k];
        }
        for (k = 0; k < 4; k++)
            gauss[f][k] = w[k] / s * 0.998f;
    }
}

/* ---------------------------------------------------------------- the decoders --- */
static inline int16_t cl16(int32_t x) { return (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x); }
static void dec_brr(smp_st *e, const uint8_t *b)       /* 9 bytes: 16 samples */
{
    int sh = b[0] >> 4, flt = (b[0] >> 2) & 3, i;
    int32_t p1 = e->p1, p2 = e->p2;
    for (i = 0; i < 16; i++) {
        int nib = (b[1 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 15;
        int32_t s = nib >= 8 ? nib - 16 : nib, q2 = p2 >> 1;
        if (sh <= 12)
            s = (s * (1 << sh)) >> 1;
        else
            s = s < 0 ? -2048 : 0;
        switch (flt) {
        case 1: s += p1 >> 1; s += (-p1) >> 5; break;
        case 2: s += p1; s -= q2; s += q2 >> 4; s += (p1 * -3) >> 6; break;
        case 3: s += p1; s -= q2; s += (p1 * -13) >> 7; s += (q2 * 3) >> 4; break;
        default: break;
        }
        s = cl16(s);
        s = (int16_t)(s * 2);
        e->buf[i] = (int16_t)s;
        p2 = p1;
        p1 = s;
    }
    e->p1 = p1;
    e->p2 = p2;
}
static void dec_spu(smp_st *e, const uint8_t *b)       /* 16 bytes: 28 samples */
{
    static const int8_t POS[5] = {0, 60, 115, 98, 122}, NEG[5] = {0, 0, -52, -55, -60};
    int sh = b[0] & 15, flt = (b[0] >> 4) & 7, i;
    int32_t p1 = e->p1, p2 = e->p2;
    if (sh > 12)
        sh = 9;
    if (flt > 4)
        flt = 0;
    for (i = 0; i < 28; i++) {
        int nib = (b[2 + (i >> 1)] >> ((i & 1) ? 4 : 0)) & 15;
        int32_t s = (int16_t)(nib << 12) >> sh;
        s += (p1 * POS[flt] + p2 * NEG[flt] + 32) >> 6;
        s = cl16(s);
        e->buf[i] = (int16_t)s;
        p2 = p1;
        p1 = s;
    }
    e->p1 = p1;
    e->p2 = p2;
}
static void dec_vad(smp_st *e, const uint8_t *b, const int16_t *book)   /* 9 bytes: 16 samples */
{
    int sc = b[0] >> 4, pr = b[0] & 3, i;
    int32_t p1 = e->p1, p2 = e->p2, c0 = book[2 * pr], c1 = book[2 * pr + 1];
    for (i = 0; i < 16; i++) {
        int nib = (b[1 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 15;
        int32_t s = (nib >= 8 ? nib - 16 : nib) * (1 << sc);
        s += (c0 * p1 + c1 * p2 + 1024) >> 11;
        s = cl16(s);
        e->buf[i] = (int16_t)s;
        p2 = p1;
        p1 = s;
    }
    e->p1 = p1;
    e->p2 = p2;
}
/* the block holding sample i, decoded (blocks are only ever read in order, or from the loop's start) */
static void smp_block(smp_st *e, uint32_t blk)
{
    const bk_smp_t *s = (const bk_smp_t *)e->s;
    switch (s->fmt) {
    case BF_BRR: dec_brr(e, s->data + 9u * blk); break;
    case BF_SPU: dec_spu(e, s->data + 16u * blk); break;
    default: dec_vad(e, s->data + 9u * blk, s->book); break;
    }
}

/* ------------------------------------------------------------------- the voice --- */
static uint32_t smp_end(const sv_t *v)                 /* where it stops (the sample's end, or its slice's) */
{
    const smp_patch_t *p = (const smp_patch_t *)v->patch;
    const bk_smp_t *s = (const bk_smp_t *)v->e.s.s;
    if (p->slice)
        return BK_BREAK_SLICE[p->slice] < s->nsamp ? BK_BREAK_SLICE[p->slice] : s->nsamp;
    return s->nsamp;
}
static void smp_start(sv_t *v, int legato)
{
    const smp_patch_t *p = (const smp_patch_t *)v->patch;
    smp_st *e = &v->e.s;
    const bk_smp_t *s = bank_smp(p->smp);
    if (legato) {
        e->est = 0;                    /* the envelope again (the sample goes on) */
        return;
    }
    e->s = s;
    e->bn = s->fmt == BF_SPU ? 28 : 16;
    e->fmt = s->fmt;
    e->p1 = e->p2 = 0;
    e->pos = p->slice ? BK_BREAK_SLICE[p->slice - 1] : 0;    /* (the absolute sample, in e->pos) */
    e->frac = 0.0f;
    e->hist[0] = e->hist[1] = e->hist[2] = e->hist[3] = 0;
    smp_block(e, e->pos / e->bn);
    e->hist[3] = e->buf[e->pos % e->bn];
    e->env = 0;
    e->lvl = 0.0f;
    e->est = 0;
    e->ecnt = 0;
    e->g2 = 0.0f;
}
static void smp_release(sv_t *v) { v->e.s.est = 3; }

/* the pitch: a step through the sample a native sample, within the console's limit */
static void smp_frame(sv_t *v)
{
    const smp_patch_t *p = (const smp_patch_t *)v->patch;
    smp_st *e = &v->e.s;
    const bk_smp_t *s = (const bk_smp_t *)e->s;
    float note = v->pitch + v->poff, st, mx = en_con == CON_N64 ? 1.99f : 3.99f;
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (p->pdrop || v->trk < 0) {      /* a drum's pitch falling (Tone moves how far) */
        float drop = (float)p->pdrop + (v->trk < 0 ? (kv(v, KT_TONE, 0.5f) - 0.5f) * 16.0f : 0.0f);
        if (drop > 0.0f && v->frame < 10)
            note += drop * (1.0f - (float)v->frame * 0.1f) * (1.0f - (float)v->frame * 0.1f);
    }
    st = (float)s->rate * tunefac * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / eng->rate;
    while (st > mx)                    /* past the pitch register: an octave down */
        st *= 0.5f;
    e->step = st;
}

/* A voice renders in passes (the FM-1 keeps its floats in the integer registers: a loop that holds the
 * envelope, the decoder and the interpolator at once spills): the envelope's levels, then the samples the
 * position passes (in a row after the interpolator's last four), then the interpolation. The voices render
 * one at a time, so the buffers are shared. */
#define SM_N 264
static float sm_lv[SM_N] EN_POOL, sm_fr[SM_N] EN_POOL;         /* each output sample: its level (x the gain), where it is */
static uint16_t sm_i[SM_N] EN_POOL;                    /* ... the samples passed before it */
static int16_t sm_x[4 + 4 * SM_N] EN_POOL;             /* the last four, then the samples passed */

/* the envelopes: the SNES's in its own units (0..2047, its rate table at 32 kHz), the others' as levels
 * 0..1 (kept x 2048 in env); the outputs it lasts (n, or fewer when it ends: *end set) */
static uint32_t smp_env(sv_t *v, uint32_t n, float g, int *end)
{
    const smp_patch_t *p = (const smp_patch_t *)v->patch;
    smp_st *e = &v->e.s;
    uint32_t j;
    if (en_con == CON_SNES) {
        int ar = 15 - (int)(kv(v, KT_ATTACK, pct(v->d->att)) * 15.0f + 0.5f);
        int dr = 7 - (int)(kv(v, KT_DECAY, pct(v->d->dec)) * 7.0f + 0.5f);
        int sl = (int)(kv(v, KT_SUSTAIN, pct(v->d->sus)) * 7.0f + 0.5f), r2 = p->srate, r3;
        int env = e->env, est = e->est, ecnt = e->ecnt;
        float r = kv(v, KT_RELEASE, pct(v->d->rel)), k = g * (1.0f / 2048.0f);
        if (v->trk < 0)
            dr = clampi(dr - (int)((v->k[1] - 0.5f) * 6.0f), 0, 7);
        if (has_kt(v->d, KT_DECAY) && v->d->kt[0] != KT_ATTACK && p->srate)
            r2 = clampi(31 - (int)(kv(v, KT_DECAY, 0.5f) * 24.0f), 1, 31);
        r3 = r < 0.03f ? -1 : clampi(30 - (int)(r * 26.0f), 1, 31);   /* -1: the hardware's release, 8 a sample */
        for (j = 0; j < n; j++) {
            int rate, step;
            switch (est) {
            case 0:
                rate = ar * 2 + 1;
                step = ar == 15 ? 1024 : 32;
                break;
            case 1:
                rate = dr * 2 + 16;
                step = -(((env - 1) >> 8) + 1);
                break;
            case 2:
                rate = r2;
                step = -(((env - 1) >> 8) + 1);
                break;
            default:
                rate = r3;
                step = -(((env - 1) >> 8) + 1);   /* the GAIN's exponential fall */
                break;
            }
            if (rate < 0) {
                env -= 8;
            } else if (rate > 0 && ++ecnt >= SNES_RATE[rate & 31]) {
                ecnt = 0;
                env += step;
                if (est == 0 && env >= 0x7E0) {
                    env = 0x7FF;
                    est = 1;
                } else if (est == 1 && (env >> 8) <= sl) {
                    est = 2;
                }
            }
            sm_lv[j] = (float)env * k;
            if (env <= 0 && est >= 1) {
                env = 0;
                if (est == 3 || (est == 2 && p->srate)) {
                    *end = 1;
                    break;
                }
            }
        }
        e->env = env;
        e->est = (uint8_t)est;
        e->ecnt = ecnt;
        return j;
    } else {                           /* the others' rates, from the knobs (a frame at a time is enough) */
        int ps1 = en_con == CON_PS1, est = e->est;
        float at = 0.002f + 2.0f * kv(v, KT_ATTACK, pct(v->d->att)) * kv(v, KT_ATTACK, pct(v->d->att));
        float dt = 0.05f + 4.0f * kv(v, KT_DECAY, pct(v->d->dec)) * kv(v, KT_DECAY, pct(v->d->dec));
        float rt = 0.01f + 3.0f * kv(v, KT_RELEASE, pct(v->d->rel)) * kv(v, KT_RELEASE, pct(v->d->rel));
        float sus, a_k, d_k, r_k, l = e->lvl, sr = p->srate ? (float)p->srate * 0.000004f : 0.0f;
        if (v->trk < 0)
            dt *= fm_exp2f((v->k[1] - 0.5f) * 3.0f);
        sus = kv(v, KT_SUSTAIN, pct(v->d->sus));
        a_k = 1.0f / (at * eng->rate);
        d_k = ps1 ? 1.0f - fm_expf(-4.6f / (dt * eng->rate)) : 1.0f / (dt * eng->rate);
        r_k = ps1 ? 1.0f - fm_expf(-4.6f / (rt * eng->rate)) : 1.0f / (rt * eng->rate);
        for (j = 0; j < n; j++) {
            switch (est) {
            case 0:
                l += a_k;
                if (l >= 1.0f) {
                    l = 1.0f;
                    est = 1;
                }
                break;
            case 1:
                l -= ps1 ? (l - sus) * d_k : d_k;
                if (l <= sus + 0.001f) {
                    l = sus;
                    est = 2;
                }
                break;
            case 2:
                l -= l * sr;
                break;
            default:
                l -= ps1 ? l * r_k : r_k;
                if (l <= 0.0005f) {
                    l = 0.0f;
                    *end = 1;
                }
                break;
            }
            if (est == 2 && sus <= 0.0f)
                *end = 1;
            if (*end)
                break;
            sm_lv[j] = l * g;
        }
        e->lvl = l;
        e->est = (uint8_t)est;
        e->env = (int32_t)(l * 2048.0f);
        return j;
    }
}

static void smp_render(sv_t *v, uint32_t n, float *out)
{
    const smp_patch_t *p = (const smp_patch_t *)v->patch;
    smp_st *e = &v->e.s;
    const bk_smp_t *s = (const bk_smp_t *)e->s;
    float g = fm_minf(v->vel, 1.3f) * (float)(p->gain ? p->gain : 64) * (1.0f / 64.0f) * (1.0f / 32768.0f) * 0.9f;
    uint32_t j, ne, c = 0, end = smp_end(v), bn = e->bn;
    int ended = 0, out_of = 0;
    if (v->done)                       /* (ended in an earlier stretch of this chunk) */
        return;
    if (n > SM_N)
        n = SM_N;
    ne = smp_env(v, n, g, &ended);
    {                                  /* the position: whole samples in, one a time, each with its block */
        float fr = e->frac, st = e->step;
        uint32_t pos = e->pos, bi = pos % bn, blk = pos / bn;
        int loops = s->loop != BK_NOLOOP && !p->slice;
        sm_x[0] = e->hist[0];
        sm_x[1] = e->hist[1];
        sm_x[2] = e->hist[2];
        sm_x[3] = e->hist[3];
        for (j = 0; j < ne; j++) {
            fr += st;
            while (fr >= 1.0f) {
                fr -= 1.0f;
                if (++pos >= end) {
                    if (!loops) {
                        out_of = 1;
                        break;
                    }
                    pos = s->loop;
                    blk = pos / bn;
                    bi = pos % bn;
                    smp_block(e, blk);
                } else if (++bi == bn) {
                    bi = 0;
                    smp_block(e, ++blk);
                }
                sm_x[4 + c++] = e->buf[bi];
            }
            if (out_of)
                break;
            sm_i[j] = (uint16_t)c;
            sm_fr[j] = fr;
        }
        ne = j;
        if (ended || out_of)
            v->done = 1;
        e->pos = pos;
        e->frac = fr;
        e->hist[0] = sm_x[c];
        e->hist[1] = sm_x[c + 1];
        e->hist[2] = sm_x[c + 2];
        e->hist[3] = sm_x[c + 3];
    }
    if (en_con != CON_N64) {           /* the Gaussian (the S-DSP's and the SPU's) */
        for (j = 0; j < ne; j++) {
            const int16_t *h = sm_x + sm_i[j];
            const float *w = gauss[(int)(sm_fr[j] * 256.0f) & 255];
            float x = w[0] * (float)h[0] + w[1] * (float)h[1] + w[2] * (float)h[2] + w[3] * (float)h[3];
            out[j] += x * sm_lv[j];
        }
    } else {                           /* a cubic (the N64's resampler, near enough) */
        for (j = 0; j < ne; j++) {
            const int16_t *h = sm_x + sm_i[j];
            float fr = sm_fr[j], y0 = (float)h[0], y1 = (float)h[1], y2 = (float)h[2], y3 = (float)h[3];
            float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[j] += (((c3 * fr + c2) * fr + c1) * fr + y1) * sm_lv[j];
        }
    }
}

/* ------------------------------------------------------------------- the sounds --- */
#define SM(k0, k1, k2, k3, a, d, s, r, vib, vspd, vdly, fl, oct) {{k0, k1, k2, k3}, a, d, s, r, vib, vspd, vdly, 0, 0, 0, (fl) | DF_HWENV, oct}
#define BASS(smp) {SM(KT_DECAY, KT_SUSTAIN, KT_RELEASE, KT_VIB, 0, 55, 50, 10, 0, 40, 30, 0, 0), smp, 6, 0, 0, 64}
#define PAD(smp) {SM(KT_ATTACK, KT_RELEASE, KT_VIB, KT_SUSTAIN, 20, 0, 100, 40, 0, 40, 30, 0, 0), smp, 0, 0, 0, 64}
#define PLUCK(smp) {SM(KT_DECAY, KT_RELEASE, KT_VIB, KT_OCTAVE, 0, 60, 0, 30, 0, 40, 30, 0, 0), smp, 10, 0, 0, 64}
#define LEAD(smp) {SM(KT_VIB, KT_ATTACK, KT_RELEASE, KT_DECAY, 0, 70, 100, 25, 25, 40, 25, 0, 0), smp, 0, 0, 0, 64}
#define HIT(smp) {SM(KT_DECAY, KT_OCTAVE, KT_RELEASE, KT_NONE, 0, 70, 0, 20, 0, 40, 0, 0, 0), smp, 12, 0, 0, 64}
#define DRM(smp, pd) {SM(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 70, 0, 50, 0, 30, 0, 0, 0), smp, 0, 0, pd, 64}
#define SLC(k) {SM(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 100, 100, 5, 0, 30, 0, 0, 0), BK_PS_BREAK, 0, k, 0, 64}

static const smp_patch_t SNES_P[EN_NSOUND] = {
    BASS(BK_SN_SLAP), BASS(BK_SN_FINGER), BASS(BK_SN_SYNBASS), PLUCK(BK_SN_PIZZ),
    PAD(BK_SN_STRINGS), PAD(BK_SN_CHOIR), PLUCK(BK_SN_PIANO), PLUCK(BK_SN_EPIANO),
    LEAD(BK_SN_FLUTE), LEAD(BK_SN_BRASS), LEAD(BK_SN_ODGTR), LEAD(BK_SN_SQUARE),
    HIT(BK_SN_HIT), PLUCK(BK_SN_HARP), PLUCK(BK_SN_MARIMBA), PLUCK(BK_SN_STEEL),
};
static const smp_patch_t PS1_P[EN_NSOUND] = {
    BASS(BK_PS_REESE), BASS(BK_PS_SUB), BASS(BK_PS_FRETLESS), BASS(BK_PS_ACID),
    PAD(BK_PS_STRINGS), PAD(BK_PS_CHOIR), PAD(BK_PS_ORGAN), PLUCK(BK_PS_RHODES),
    LEAD(BK_PS_SYNLEAD), LEAD(BK_PS_DISTGTR), LEAD(BK_PS_BRASS), LEAD(BK_PS_PANFLUTE),
    PLUCK(BK_PS_HARPSI), PAD(BK_PS_PAD), PLUCK(BK_PS_SITAR), PAD(BK_PS_VOICE),
};
static const smp_patch_t N64_P[EN_NSOUND] = {
    BASS(BK_N6_SLAP), BASS(BK_N6_TUBA), BASS(BK_N6_UPRIGHT), BASS(BK_N6_SYNBASS),
    PLUCK(BK_N6_MARIMBA), PAD(BK_N6_ACCORD), PLUCK(BK_N6_PIANO), PAD(BK_N6_CHOIR),
    LEAD(BK_N6_SYNBRASS), LEAD(BK_N6_OCARINA), LEAD(BK_N6_FIDDLE), LEAD(BK_N6_WHISTLE2),
    PLUCK(BK_N6_STEEL), LEAD(BK_N6_KAZOO), PLUCK(BK_N6_BANJO), PLUCK(BK_N6_MUSICBOX),
};
#define BASSK {"Decay", "Sustain", "Release", "Vibrato"}, {55, 50, 10, 0}, {0, 0, 0, 0}
#define PADK {"Attack", "Release", "Vibrato", "Sustain"}, {20, 40, 0, 100}, {0, 0, 0, 0}
#define PLUCKK {"Decay", "Release", "Vibrato", "Octave"}, {60, 30, 0, 50}, {0, 0, 0, 3}
#define LEADK {"Vibrato", "Attack", "Release", "Decay"}, {25, 0, 25, 70}, {0, 0, 0, 0}
#define HITK {"Decay", "Octave", "Release", ""}, {70, 50, 20, 0}, {0, 3, 0, 0}
static const en_sound_t SNES_SND[EN_NSOUND] = {
    {"Slap bass", "BRR sample", BASSK, &SNES_P[0]},      {"Finger bass", "BRR sample", BASSK, &SNES_P[1]},
    {"Synth bass", "BRR sample", BASSK, &SNES_P[2]},     {"Pizzicato", "BRR sample", PLUCKK, &SNES_P[3]},
    {"Strings", "BRR sample", PADK, &SNES_P[4]},         {"Choir", "BRR sample", PADK, &SNES_P[5]},
    {"Piano", "BRR sample", PLUCKK, &SNES_P[6]},         {"E.Piano", "BRR sample", PLUCKK, &SNES_P[7]},
    {"Flute", "BRR sample", LEADK, &SNES_P[8]},          {"Brass", "BRR sample", LEADK, &SNES_P[9]},
    {"Overdrive", "BRR sample", LEADK, &SNES_P[10]},     {"Square lead", "BRR sample", LEADK, &SNES_P[11]},
    {"Orch hit", "BRR sample", HITK, &SNES_P[12]},       {"Harp", "BRR sample", PLUCKK, &SNES_P[13]},
    {"Marimba", "BRR sample", PLUCKK, &SNES_P[14]},      {"Steel drum", "BRR sample", PLUCKK, &SNES_P[15]},
};
static const en_sound_t PS1_SND[EN_NSOUND] = {
    {"Reese", "SPU sample", BASSK, &PS1_P[0]},           {"Sub", "SPU sample", BASSK, &PS1_P[1]},
    {"Fretless", "SPU sample", BASSK, &PS1_P[2]},        {"Acid", "SPU sample", BASSK, &PS1_P[3]},
    {"Strings", "SPU sample", PADK, &PS1_P[4]},          {"Choir", "SPU sample", PADK, &PS1_P[5]},
    {"Gothic organ", "SPU sample", PADK, &PS1_P[6]},     {"Rhodes", "SPU sample", PLUCKK, &PS1_P[7]},
    {"Synth lead", "SPU sample", LEADK, &PS1_P[8]},      {"Dist. guitar", "SPU sample", LEADK, &PS1_P[9]},
    {"Brass", "SPU sample", LEADK, &PS1_P[10]},          {"Pan flute", "SPU sample", LEADK, &PS1_P[11]},
    {"Harpsichord", "SPU sample", PLUCKK, &PS1_P[12]},   {"Space pad", "SPU sample", PADK, &PS1_P[13]},
    {"Sitar", "SPU sample", PLUCKK, &PS1_P[14]},         {"Voice", "SPU sample", PADK, &PS1_P[15]},
};
static const en_sound_t N64_SND[EN_NSOUND] = {
    {"Slap bass", "N64 ADPCM sample", BASSK, &N64_P[0]},     {"Tuba", "N64 ADPCM sample", BASSK, &N64_P[1]},
    {"Upright", "N64 ADPCM sample", BASSK, &N64_P[2]},       {"Synth bass", "N64 ADPCM sample", BASSK, &N64_P[3]},
    {"Marimba", "N64 ADPCM sample", PLUCKK, &N64_P[4]},      {"Accordion", "N64 ADPCM sample", PADK, &N64_P[5]},
    {"Piano", "N64 ADPCM sample", PLUCKK, &N64_P[6]},        {"Choir", "N64 ADPCM sample", PADK, &N64_P[7]},
    {"Synth brass", "N64 ADPCM sample", LEADK, &N64_P[8]},   {"Ocarina", "N64 ADPCM sample", LEADK, &N64_P[9]},
    {"Fiddle", "N64 ADPCM sample", LEADK, &N64_P[10]},       {"Whistle", "N64 ADPCM sample", LEADK, &N64_P[11]},
    {"Steel drum", "N64 ADPCM sample", PLUCKK, &N64_P[12]},  {"Kazoo", "N64 ADPCM sample", LEADK, &N64_P[13]},
    {"Banjo", "N64 ADPCM sample", PLUCKK, &N64_P[14]},       {"Music box", "N64 ADPCM sample", PLUCKK, &N64_P[15]},
};
#undef BASSK
#undef PADK
#undef PLUCKK
#undef LEADK
#undef HITK

static const smp_patch_t SNES_DP[2][EN_NPART] = {
    {DRM(BK_SN_KICK, 2), DRM(BK_SN_SNARE, 0), DRM(BK_SN_TOM, 2), DRM(BK_SN_TOM, 2), DRM(BK_SN_TOM, 2), DRM(BK_SN_RIM, 0),
     DRM(BK_SN_CLAP, 0), DRM(BK_SN_CHAT, 0), DRM(BK_SN_OHAT, 0), DRM(BK_SN_CRASH, 0), DRM(BK_SN_RIDE, 0)},
    {DRM(BK_SN_CBD, 0), DRM(BK_SN_CSNARE, 0), DRM(BK_SN_TIMP, 0), DRM(BK_SN_TIMP, 0), DRM(BK_SN_TIMP, 0), DRM(BK_SN_WOOD, 0),
     DRM(BK_SN_ROLL, 0), DRM(BK_SN_TRI, 0), DRM(BK_SN_TRI, 0), DRM(BK_SN_PIATTI, 0), DRM(BK_SN_TAMB, 0)},
};
static const smp_patch_t PS1_DP[2][EN_NPART] = {
    {SLC(1), SLC(2), SLC(3), SLC(4), SLC(5), SLC(6), SLC(7), SLC(8), SLC(9), SLC(10), SLC(11)},
    {DRM(BK_PS_KICK, 3), DRM(BK_PS_SNARE, 0), DRM(BK_PS_TOM, 2), DRM(BK_PS_TOM, 2), DRM(BK_PS_TOM, 2), DRM(BK_PS_RIM, 0),
     DRM(BK_PS_CLAP, 0), DRM(BK_PS_CHAT, 0), DRM(BK_PS_OHAT, 0), DRM(BK_PS_CRASH, 0), DRM(BK_PS_RIDE, 0)},
};
static const smp_patch_t N64_DP[2][EN_NPART] = {
    {DRM(BK_N6_KICK, 2), DRM(BK_N6_SNARE, 0), DRM(BK_N6_TOM, 2), DRM(BK_N6_TOM, 2), DRM(BK_N6_TOM, 2), DRM(BK_N6_RIM, 0),
     DRM(BK_N6_CLAP, 0), DRM(BK_N6_CHAT, 0), DRM(BK_N6_OHAT, 0), DRM(BK_N6_CRASH, 0), DRM(BK_N6_COW, 0)},
    {DRM(BK_N6_SURDO, 0), DRM(BK_N6_CONGA, 0), DRM(BK_N6_CONGA, 0), DRM(BK_N6_CSLAP, 0), DRM(BK_N6_BONGO, 0), DRM(BK_N6_LOG, 0),
     DRM(BK_N6_CLAVES, 0), DRM(BK_N6_SHAKER, 0), DRM(BK_N6_TAMB, 0), DRM(BK_N6_COW, 0), DRM(BK_N6_WHISTLE, 0)},
};
#undef SM
#undef BASS
#undef PAD
#undef PLUCK
#undef LEAD
#undef HIT
#undef DRM
#undef SLC
static const dkit_t SNES_KIT[EN_NKIT] = {
    {"Studio", {{"KCK", "Kick", &SNES_DP[0][0], 60, 0, 0}, {"SNR", "Snare", &SNES_DP[0][1], 60, 0, 0},
                {"LT", "Low tom", &SNES_DP[0][2], 55, -50, 0}, {"MT", "Mid tom", &SNES_DP[0][3], 60, 0, 0},
                {"HT", "High tom", &SNES_DP[0][4], 65, 50, 0}, {"RIM", "Rim", &SNES_DP[0][5], 60, 0, 0},
                {"CLP", "Clap", &SNES_DP[0][6], 60, 0, 0}, {"CH", "Closed hat", &SNES_DP[0][7], 60, 30, 1},
                {"OH", "Open hat", &SNES_DP[0][8], 60, 30, 1}, {"CRS", "Crash", &SNES_DP[0][9], 60, -30, 0},
                {"RDE", "Ride", &SNES_DP[0][10], 60, 30, 0}}},
    {"Orchestra", {{"BD", "Concert drum", &SNES_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &SNES_DP[1][1], 60, 0, 0},
                   {"TI1", "Timpani low", &SNES_DP[1][2], 55, -30, 0}, {"TI2", "Timpani mid", &SNES_DP[1][3], 60, 0, 0},
                   {"TI3", "Timpani high", &SNES_DP[1][4], 64, 30, 0}, {"WB", "Woodblock", &SNES_DP[1][5], 60, 40, 0},
                   {"ROL", "Snare roll", &SNES_DP[1][6], 60, 0, 0}, {"TR1", "Triangle", &SNES_DP[1][7], 60, 40, 2},
                   {"TR2", "Triangle open", &SNES_DP[1][8], 60, 40, 2}, {"CYM", "Cymbals", &SNES_DP[1][9], 60, -30, 0},
                   {"TMB", "Tambourine", &SNES_DP[1][10], 60, 30, 0}}},
};
static const dkit_t PS1_KIT[EN_NKIT] = {
    {"Jungle", {{"S1", "Slice 1", &PS1_DP[0][0], 60, 0, 3}, {"S2", "Slice 2", &PS1_DP[0][1], 60, 0, 3},
                {"S3", "Slice 3", &PS1_DP[0][2], 60, 0, 3}, {"S4", "Slice 4", &PS1_DP[0][3], 60, 0, 3},
                {"S5", "Slice 5", &PS1_DP[0][4], 60, 0, 3}, {"S6", "Slice 6", &PS1_DP[0][5], 60, 0, 3},
                {"S7", "Slice 7", &PS1_DP[0][6], 60, 0, 3}, {"S8", "Slice 8", &PS1_DP[0][7], 60, 0, 3},
                {"S9", "Slice 9", &PS1_DP[0][8], 60, 0, 3}, {"S10", "Slice 10", &PS1_DP[0][9], 60, 0, 3},
                {"S11", "Slice 11", &PS1_DP[0][10], 60, 0, 3}}},
    {"Studio", {{"KCK", "Kick", &PS1_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &PS1_DP[1][1], 60, 0, 0},
                {"LT", "Low tom", &PS1_DP[1][2], 55, -50, 0}, {"MT", "Mid tom", &PS1_DP[1][3], 60, 0, 0},
                {"HT", "High tom", &PS1_DP[1][4], 65, 50, 0}, {"RIM", "Rim", &PS1_DP[1][5], 60, 0, 0},
                {"CLP", "Clap", &PS1_DP[1][6], 60, 0, 0}, {"CH", "Closed hat", &PS1_DP[1][7], 60, 30, 1},
                {"OH", "Open hat", &PS1_DP[1][8], 60, 30, 1}, {"CRS", "Crash", &PS1_DP[1][9], 60, -30, 0},
                {"RDE", "Ride", &PS1_DP[1][10], 60, 30, 0}}},
};
static const dkit_t N64_KIT[EN_NKIT] = {
    {"Pop", {{"KCK", "Kick", &N64_DP[0][0], 60, 0, 0}, {"SNR", "Snare", &N64_DP[0][1], 60, 0, 0},
             {"LT", "Low tom", &N64_DP[0][2], 55, -50, 0}, {"MT", "Mid tom", &N64_DP[0][3], 60, 0, 0},
             {"HT", "High tom", &N64_DP[0][4], 65, 50, 0}, {"RIM", "Rim", &N64_DP[0][5], 60, 0, 0},
             {"CLP", "Clap", &N64_DP[0][6], 60, 0, 0}, {"CH", "Closed hat", &N64_DP[0][7], 60, 30, 1},
             {"OH", "Open hat", &N64_DP[0][8], 60, 30, 1}, {"CRS", "Crash", &N64_DP[0][9], 60, -30, 0},
             {"AGO", "Agogo", &N64_DP[0][10], 60, 20, 0}}},
    {"Tribal", {{"SRD", "Surdo", &N64_DP[1][0], 60, 0, 0}, {"CG1", "Low conga", &N64_DP[1][1], 55, -40, 0},
                {"CG2", "Conga", &N64_DP[1][2], 60, -20, 0}, {"SLP", "Conga slap", &N64_DP[1][3], 60, 20, 0},
                {"BNG", "Bongo", &N64_DP[1][4], 60, 40, 0}, {"LOG", "Log drum", &N64_DP[1][5], 60, 0, 0},
                {"CLV", "Claves", &N64_DP[1][6], 60, 30, 0}, {"SHK", "Shaker", &N64_DP[1][7], 60, -30, 0},
                {"TMB", "Tambourine", &N64_DP[1][8], 60, 30, 0}, {"AGO", "Agogo", &N64_DP[1][9], 60, -20, 0},
                {"WHS", "Whistle", &N64_DP[1][10], 84, 0, 0}}},
};
static const engine_t SNES_ENG = {32000.0f, 60.0f, smp_start, smp_frame, smp_render, smp_release};
static const engine_t PS1_ENG = {44100.0f, 60.0f, smp_start, smp_frame, smp_render, smp_release};
static const engine_t N64_ENG = {22050.0f, 60.0f, smp_start, smp_frame, smp_render, smp_release};
static const engine_t N64_ENG32 = {32000.0f, 60.0f, smp_start, smp_frame, smp_render, smp_release};
