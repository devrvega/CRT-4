/* SPDX-License-Identifier: GPL-3.0-only */
/* The Genesis (Mega Drive): the YM2612 and the SN76489.
 *   FM      four operators a voice, in one of eight algorithms (who modulates whom), the first one feeding
 *           back into itself; each a sine read at the chip's 10 bits of phase, with its multiple, detune, level
 *           and envelope (attack, two decays, sustain, release, its rates scaled by the key); an LFO for
 *           vibrato and tremolo. The envelope works in the chip's own units (1024 steps of 0.09375 dB) at
 *           its rates. The output goes through a 9-bit DAC with a gap at zero (the "ladder": quiet notes
 *           grow gritty), the Model 1's; the Model 2's is clean. Channel 3 can give each operator its own
 *           frequency (bells).
 *   PSG     three squares (10-bit periods) and a noise (white or periodic), sixteen volumes 2 dB apart.
 *   DAC     channel 6 as 8-bit PCM: the drums and voices, unfiltered, at the sound driver's rate.
 * The driver: 60 frames a second. Included once. */

enum { GN_FM, GN_PSG, GN_NOISE, GN_DAC };
typedef struct {
    uint8_t mul, dt, tl, ar, d1r, d2r, d1l, rr, ks;
    uint8_t r16;                       /* channel 3's own ratio x 1/16 (0: the multiple's) */
} fm_op_t;
typedef struct {
    drv_t d;
    uint8_t type;                      /* GN_* */
    uint8_t alg, fb;
    uint8_t fms, ams;                  /* the LFO's depth on the pitch (0..7) and the level (0..3) */
    fm_op_t op[4];
    uint8_t smp;                       /* a DAC sample */
} gen_patch_t;

static int gen_model2;

/* which operators are carriers, in each algorithm */
static const uint8_t GN_CARRIER[8] = {1, 1, 1, 1, 5, 7, 7, 15};   /* bit 3 op 1 .. bit 0 op 4 */
/* an envelope rate (0..63) as attenuation steps a sample at 44.1 kHz (the chip's EG ticks at 53267 / 3 Hz);
 * a table, as every voice asks for sixteen of them a frame */
static float gn_rt[64];
static void gen_init(void)
{
    int r;
    for (r = 2; r < 64; r++) {
        float inc = (1.0f + (float)(r & 3) * 0.25f) * fm_exp2f((float)((r >> 2) - 11));
        gn_rt[r] = (inc > 8.0f ? 8.0f : inc) * (17755.7f / 44100.0f);
    }
}
static inline float gn_rate(int r) { return r <= 1 ? 0.0f : gn_rt[r > 63 ? 63 : r]; }
static int gn_keycode(float note)
{
    int k = (int)((note - 12.0f) * (1.0f / 3.0f));
    return k < 0 ? 0 : k > 31 ? 31 : k;
}

static void gen_setup(sv_t *v)                     /* the operators' rates and levels, from the patch and the knobs */
{
    const gen_patch_t *p = (const gen_patch_t *)v->patch;
    fm_st *f = &v->e.f;
    float note = v->pitch + v->poff;
    int kc = gn_keycode(note), k, car = GN_CARRIER[p->alg & 7];
    int dmod = has_kt(v->d, KT_MOD) ? (int)((0.5f - kv(v, KT_MOD, 0.5f)) * 48.0f) : 0;
    int ddec = has_kt(v->d, KT_DECAY) ? (int)((kv(v, KT_DECAY, 0.5f) - 0.5f) * 20.0f) : 0;
    int drel = has_kt(v->d, KT_RELEASE) ? (int)((kv(v, KT_RELEASE, 0.5f) - 0.5f) * 12.0f) : 0;
    int datt = has_kt(v->d, KT_ATTACK) ? (int)((kv(v, KT_ATTACK, 0.0f)) * 24.0f) : 0;
    if (v->trk < 0)
        ddec = (int)((v->k[1] - 0.5f) * 16.0f);
    for (k = 0; k < 4; k++) {
        const fm_op_t *o = &p->op[k];
        int ksr = kc >> (3 - (o->ks & 3)), is_car = (car >> (3 - k)) & 1, tl = o->tl, ar, d1, d2, rr;
        if (!is_car)
            tl = clampi(tl + dmod, 0, 127);
        if (v->trk < 0 && !is_car)
            tl = clampi(tl + (int)((0.5f - kv(v, KT_TONE, 0.5f)) * 30.0f), 0, 127);
        ar = clampi(o->ar - datt, 0, 31);
        d1 = clampi(o->d1r - (is_car ? ddec : 0), 0, 31);
        d2 = clampi(o->d2r - (is_car ? ddec : 0), 0, 31);
        rr = clampi(o->rr - (is_car ? drel : 0), 0, 15);
        f->rate[k][0] = ar >= 31 ? -1.0f : gn_rate(ar ? 2 * ar + ksr : 0);
        f->rate[k][1] = gn_rate(d1 ? 2 * d1 + ksr : 0);
        f->rate[k][2] = gn_rate(d2 ? 2 * d2 + ksr : 0);
        f->rate[k][3] = gn_rate(4 * rr + 2 + ksr);
        f->sl[k] = o->d1l >= 15 ? 992.0f : (float)o->d1l * 32.0f;
        f->tl[k] = (float)tl * 8.0f;
    }
}

static void gen_start(sv_t *v, int legato)
{
    const gen_patch_t *p = (const gen_patch_t *)v->patch;
    fm_st *f = &v->e.f;
    int k;
    if (p->type == GN_FM) {
        gen_setup(v);
        for (k = 0; k < 4; k++) {      /* key on: every operator to its attack (from where it is) */
            if (!legato) {
                f->stg[k] = 0;
                if (f->att[k] <= 0.0f && !v->t)
                    f->att[k] = 1023.0f;
                if (f->rate[k][0] < 0.0f)
                    f->att[k] = 0.0f;
            }
        }
        if (!legato && v->t == 0)
            f->ph[0] = f->ph[1] = f->ph[2] = f->ph[3] = 0.0f;
    } else if (p->type == GN_DAC) {
        const bk_smp_t *s = bank_smp(p->smp);
        f->smp = s->data;
        f->slen = s->nsamp;
        f->spos = 0;
        f->sfrac = 0.0f;
        f->dac = 0.0f;
        f->lfsr = 128;                 /* the DPCM accumulator */
    }
    if (!f->lfsr)
        f->lfsr = 0x8000u;
}
static void gen_release(sv_t *v)
{
    fm_st *f = &v->e.f;
    int k;
    for (k = 0; k < 4; k++)
        if (f->stg[k] < 3)
            f->stg[k] = 3;
}

static void gen_frame(sv_t *v)
{
    const gen_patch_t *p = (const gen_patch_t *)v->patch;
    fm_st *f = &v->e.f;
    float note = v->pitch + v->poff, sr = eng->rate;
    int k;
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (has_kt(v->d, KT_BEND) && v->frame < 8)
        note += kv(v, KT_BEND, 0.0f) * 12.0f * (1.0f - (float)v->frame * 0.125f);
    if (v->trk < 0 && p->type != GN_DAC && p->type != GN_FM)
        note += (kv(v, KT_TONE, 0.5f) - 0.5f) * 24.0f;
    switch (p->type) {
    case GN_FM: {
        float base = mtof(note) / sr;
        int kc = gn_keycode(note);
        float det = has_kt(v->d, KT_DETUNE) ? kv(v, KT_DETUNE, 0.0f) : 0.0f;
        gen_setup(v);
        if (has_kt(v->d, KT_VIB) || p->fms)     /* the LFO's vibrato comes through the driver's (a frame) */
            ;
        for (k = 0; k < 4; k++) {
            const fm_op_t *o = &p->op[k];
            float r = o->r16 ? (float)o->r16 * (1.0f / 16.0f) : (o->mul ? (float)o->mul : 0.5f);
            static const float DT[4] = {0.0f, 1.0f, 2.2f, 3.8f};
            float cents = DT[o->dt & 3] * (0.5f + (float)kc * (1.0f / 24.0f)) * ((o->dt & 4) ? -1.0f : 1.0f);
            cents += det * 9.0f * (float)((k & 1) ? 1 : -1);
            f->inc[k] = base * r * fm_exp2f(cents * (1.0f / 1200.0f));
        }
        f->dac = kv(v, KT_FB, (float)p->fb / 7.0f);   /* (the feedback, 0..1) */
        break;
    }
    case GN_PSG: {
        float fr = chip_quant(mtof(note), 3579545.0f, 32.0f, 0.0f, 1.0f, 1023.0f);
        f->inc[0] = fr / sr;
        f->att[0] = v->elev * fm_minf(v->vel, 1.0f);
        break;
    }
    case GN_NOISE: {
        static const float NR[3] = {13983.0f, 6991.0f, 3496.0f};
        int i = clampi((int)((84.0f - note) * 0.12f), 0, 2);
        f->inc[0] = NR[i] / sr;
        f->att[0] = v->elev * fm_minf(v->vel, 1.0f);
        break;
    }
    default: {
        const bk_smp_t *s = bank_smp(p->smp);
        f->srate = (float)s->rate * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / sr;
        break;
    }
    }
}

/* the DAC: 9 bits, and on the Model 1 a step at zero (the ladder) */
static inline float gn_dac(float y)
{
    float q;
    if (y > 1.0f)
        y = 1.0f;
    if (y < -1.0f)
        y = -1.0f;
    q = (float)(int)(y * 255.0f) * (1.0f / 255.0f);
    if (!gen_model2)
        q += y >= 0.0f ? (1.5f / 255.0f) : -(1.5f / 255.0f);
    return q;
}

/* FM in passes, an operator at a time over a stretch of samples (the FM-1 keeps its floats in the integer
 * registers: a loop that holds all four operators at once spills; one that holds one does not). The
 * voices render one at a time, so the stretch's buffers are shared. */
#define GN_CH 64
static float gn_m[3][GN_CH] EN_POOL;                   /* operators 1-3's outputs (or their sums) */
static float gn_a[4][GN_CH / 4] EN_POOL;               /* each operator's level, four samples at a time */

static inline float gn_sin(float x)            /* x in turns, >= 0: the chip's 10-bit phase, its sine looked up */
{
    return sint[(int)(x * (float)SN) & (SN - 1)];
}

/* one operator: dst = (dst if acc) + its sine, modulated by mod (if any), at its levels */
static void gn_pass(float *php, float inc, const float *mod, const float *amp, float *dst, uint32_t n, int acc)
{
    float ph = *php;
    uint32_t j;
    if (mod) {
        for (j = 0; j < n; j++) {
            float s;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            s = gn_sin(ph + 8.0f + mod[j] * 4.0f) * amp[j >> 2];
            dst[j] = acc ? dst[j] + s : s;
        }
    } else {
        for (j = 0; j < n; j++) {
            float s;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            s = gn_sin(ph + 8.0f) * amp[j >> 2];
            dst[j] = acc ? dst[j] + s : s;
        }
    }
    *php = ph;
}

static void gn_add(float *dst, const float *x, uint32_t n)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        dst[j] += x[j];
}

/* the envelopes for a stretch: the levels four samples at a time; whether a carrier is still heard */
static int gn_env(fm_st *f, int car, uint32_t groups)
{
    uint32_t q;
    int k, live = 0;
    for (k = 0; k < 4; k++) {
        float at = f->att[k], tl = f->tl[k];
        int stg = f->stg[k];
        for (q = 0; q < groups; q++) {
            switch (stg) {
            case 0:
                at -= (at + 1.0f) * f->rate[k][0] * 4.0f * (1.0f / 16.0f);
                if (at <= 0.5f) {
                    at = 0.0f;
                    stg = 1;
                }
                break;
            case 1:
                at += f->rate[k][1] * 4.0f;
                if (at >= f->sl[k]) {
                    at = f->sl[k];
                    stg = 2;
                }
                break;
            case 2: at += f->rate[k][2] * 4.0f; break;
            case 3: at += f->rate[k][3] * 4.0f; break;
            default: break;
            }
            if (at > 1023.0f) {
                at = 1023.0f;
                if (stg == 3)
                    stg = 4;
            }
            gn_a[k][q] = at + tl >= 1000.0f ? 0.0f : fm_exp2f(-(at + tl) * (1.0f / 64.22f));
        }
        f->att[k] = at;
        f->stg[k] = (uint8_t)stg;
        if ((car >> (3 - k)) & 1)
            live |= stg < 4 && at + tl < 1000.0f;   /* a carrier still heard */
    }
    return live;
}

static void gen_fm(sv_t *v, const gen_patch_t *p, uint32_t n, float *out, float g)
{
    fm_st *f = &v->e.f;
    int alg = p->alg & 7, car = GN_CARRIER[alg], live = 0;
    float fbk = f->dac > 0.01f ? fm_exp2f(f->dac * 7.0f - 7.0f) : 0.0f, o1 = f->o1, o2 = f->o2;
    float *A = gn_m[0], *B = gn_m[1], *C = gn_m[2];
    while (n) {
        uint32_t m = n > GN_CH ? GN_CH : n, j;
        live = gn_env(f, car, (m + 3u) >> 2);
        {                                       /* operator 1, fed back on itself */
            float ph = f->ph[0], inc = f->inc[0];
            const float *a0 = gn_a[0];
            for (j = 0; j < m; j++) {
                float a;
                ph += inc;
                if (ph >= 1.0f)
                    ph -= 1.0f;
                a = gn_sin(ph + 8.0f + (o1 + o2) * fbk) * a0[j >> 2];
                o2 = o1;
                o1 = a;
                A[j] = a;
            }
            f->ph[0] = ph;
        }
        switch (alg) {                          /* 4 turns a full-scale modulator */
        case 0:
            gn_pass(&f->ph[1], f->inc[1], A, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], B, gn_a[2], C, m, 0);
            gn_pass(&f->ph[3], f->inc[3], C, gn_a[3], A, m, 0);
            break;
        case 1:
            gn_pass(&f->ph[1], f->inc[1], 0, gn_a[1], B, m, 0);
            gn_add(B, A, m);
            gn_pass(&f->ph[2], f->inc[2], B, gn_a[2], C, m, 0);
            gn_pass(&f->ph[3], f->inc[3], C, gn_a[3], A, m, 0);
            break;
        case 2:
            gn_pass(&f->ph[1], f->inc[1], 0, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], B, gn_a[2], C, m, 0);
            gn_add(C, A, m);
            gn_pass(&f->ph[3], f->inc[3], C, gn_a[3], A, m, 0);
            break;
        case 3:
            gn_pass(&f->ph[1], f->inc[1], A, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], 0, gn_a[2], B, m, 1);
            gn_pass(&f->ph[3], f->inc[3], B, gn_a[3], A, m, 0);
            break;
        case 4:
            gn_pass(&f->ph[1], f->inc[1], A, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], 0, gn_a[2], C, m, 0);
            gn_pass(&f->ph[3], f->inc[3], C, gn_a[3], B, m, 1);
            A = B;
            break;
        case 5:
            gn_pass(&f->ph[1], f->inc[1], A, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], A, gn_a[2], B, m, 1);
            gn_pass(&f->ph[3], f->inc[3], A, gn_a[3], B, m, 1);
            A = B;
            break;
        case 6:
            gn_pass(&f->ph[1], f->inc[1], A, gn_a[1], B, m, 0);
            gn_pass(&f->ph[2], f->inc[2], 0, gn_a[2], B, m, 1);
            gn_pass(&f->ph[3], f->inc[3], 0, gn_a[3], B, m, 1);
            A = B;
            break;
        default:
            gn_pass(&f->ph[1], f->inc[1], 0, gn_a[1], A, m, 1);
            gn_pass(&f->ph[2], f->inc[2], 0, gn_a[2], A, m, 1);
            gn_pass(&f->ph[3], f->inc[3], 0, gn_a[3], A, m, 1);
            break;
        }
        {                                       /* A holds the voice: through the DAC */
            float k = g * 0.85f;
            for (j = 0; j < m; j++)
                out[j] += gn_dac(A[j] * 0.6f) * k;
        }
        A = gn_m[0];
        out += m;
        n -= m;
    }
    f->o1 = o1;
    f->o2 = o2;
    if (!live && v->t > 64)
        v->done = 1;
}

static void gen_render(sv_t *v, uint32_t n, float *out)
{
    const gen_patch_t *p = (const gen_patch_t *)v->patch;
    fm_st *f = &v->e.f;
    uint32_t j;
    float g = fm_minf(v->vel, 1.2f);
    switch (p->type) {
    case GN_FM:
        gen_fm(v, p, n, out, g);
        break;
    case GN_PSG: {                     /* a square, 2 dB steps */
        float gg = vol_db(f->att[0], 2.0f, 15), ph = f->ph[0], inc = f->inc[0];
        for (j = 0; j < n; j++) {
            ph = wrap1(ph + inc);
            out[j] += chip_pulse(ph, inc, 0.5f) * gg * g * 0.7f;
        }
        f->ph[0] = ph;
        if (v->elev <= 0.0f && v->est >= 3)
            v->done = 1;
        break;
    }
    case GN_NOISE: {
        float gg = vol_db(f->att[0], 2.0f, 15);
        chip_st c;
        c.nph = f->ph5;
        c.nout = f->dac;
        c.lfsr = f->lfsr;
        for (j = 0; j < n; j++)
            out[j] += noise_run(&c, f->inc[0], lfsr_sn) * gg * g * 0.5f;
        f->ph5 = c.nph;
        f->dac = c.nout;
        f->lfsr = c.lfsr;
        if (v->elev <= 0.0f && v->est >= 3)
            v->done = 1;
        break;
    }
    default: {                         /* the DAC: 4-bit deltas decoded to 8 bits, held */
        const bk_smp_t *s = bank_smp(p->smp);
        static const int8_t DT[16] = {0, 1, 2, 4, 8, 16, 32, 64, -128, -1, -2, -4, -8, -16, -32, -64};
        float y = f->dac;
        for (j = 0; j < n; j++) {
            f->sfrac += f->srate;
            while (f->sfrac >= 1.0f) {
                uint32_t b;
                f->sfrac -= 1.0f;
                if (f->spos >= f->slen) {
                    v->done = 1;
                    break;
                }
                b = s->data[f->spos >> 1];
                f->lfsr = (f->lfsr + (uint32_t)(int32_t)DT[(f->spos & 1u) ? b & 15u : b >> 4]) & 255u;
                f->spos++;
                y = ((float)f->lfsr - 128.0f) * (1.0f / 128.0f);
            }
            out[j] += y * g * 0.8f * (v->est >= 3 && !(p->d.flags & DF_HOLD) ? 0.0f : 1.0f);
        }
        f->dac = y;
        if (v->est >= 3 && !(p->d.flags & DF_HOLD))
            v->done = 1;
        break;
    }
    }
}

/* ------------------------------------------------------------------- the sounds --- */
#define GD(k0, k1, k2, k3, vib, vspd, vdly, sw, swt, fl, oct) {{k0, k1, k2, k3}, 0, 0, 100, 0, vib, vspd, vdly, sw, swt, 0, (fl) | DF_HWENV, oct}
#define GP(k0, k1, k2, k3, a, d, s, r, vib, vspd, vdly, fl, oct) {{k0, k1, k2, k3}, a, d, s, r, vib, vspd, vdly, 0, 0, 0, fl, oct}
#define OP(mul, dt, tl, ar, d1, d2, sl, rr, ks) {mul, dt, tl, ar, d1, d2, sl, rr, ks, 0}
#define OPR(r16, tl, ar, d1, d2, sl, rr) {1, 0, tl, ar, d1, d2, sl, rr, 0, r16}
static const gen_patch_t GEN_P[EN_NSOUND] = {
    /* Slap: a struck modulator at x3 (the pop), the body under it */
    {GD(KT_MOD, KT_FB, KT_DECAY, KT_RELEASE, 0, 30, 0, 0, 0, 0, -1), GN_FM, 2, 5, 0, 0,
     {OP(1, 0, 30, 31, 12, 5, 2, 8, 1), OP(3, 3, 38, 31, 18, 8, 5, 8, 1), OP(1, 7, 30, 31, 14, 6, 3, 8, 1), OP(1, 0, 0, 31, 7, 3, 2, 9, 1)}, 0},
    /* House: two pairs, a soft sub and a bright one */
    {GD(KT_MOD, KT_DECAY, KT_FB, KT_RELEASE, 0, 30, 0, 0, 0, 0, -1), GN_FM, 4, 4, 0, 0,
     {OP(1, 0, 26, 31, 10, 4, 3, 8, 1), OP(1, 0, 2, 31, 6, 2, 2, 9, 1), OP(2, 3, 36, 31, 14, 6, 4, 8, 1), OP(1, 7, 6, 31, 6, 2, 2, 9, 1)}, 0},
    /* Rock: all four in a chain, fed back hard */
    {GD(KT_FB, KT_MOD, KT_DECAY, KT_RELEASE, 0, 30, 0, 0, 0, 0, -1), GN_FM, 0, 7, 0, 0,
     {OP(1, 0, 34, 31, 8, 3, 3, 8, 1), OP(1, 3, 28, 31, 9, 3, 3, 8, 1), OP(2, 0, 34, 31, 10, 4, 4, 8, 1), OP(1, 0, 0, 31, 5, 2, 2, 9, 1)}, 0},
    /* Wood: a fretless's soft bloom */
    {GD(KT_MOD, KT_ATTACK, KT_DECAY, KT_VIB, 0, 30, 20, 0, 0, 0, -1), GN_FM, 4, 2, 0, 0,
     {OP(1, 0, 40, 22, 6, 3, 4, 8, 1), OP(1, 0, 6, 28, 4, 2, 2, 8, 1), OP(1, 3, 46, 24, 6, 3, 4, 8, 1), OP(2, 7, 18, 26, 6, 3, 3, 8, 1)}, 0},
    /* E.Piano: the tine (x14, gone at once) over the body */
    {GD(KT_MOD, KT_DECAY, KT_RELEASE, KT_DETUNE, 0, 30, 0, 0, 0, 0, 0), GN_FM, 4, 0, 0, 0,
     {OP(14, 0, 44, 31, 20, 10, 15, 8, 2), OP(1, 0, 10, 31, 8, 4, 3, 7, 1), OP(1, 3, 30, 31, 9, 4, 5, 7, 1), OP(1, 7, 6, 31, 7, 3, 3, 7, 1)}, 0},
    /* Organ: four carriers, drawbars at 16', 8', 4', 2' */
    {GD(KT_FB, KT_ATTACK, KT_RELEASE, KT_VIB, 0, 40, 20, 0, 0, 0, 0), GN_FM, 7, 3, 0, 0,
     {OP(0, 0, 14, 31, 0, 0, 0, 9, 0), OP(1, 0, 8, 31, 0, 0, 0, 9, 0), OP(2, 0, 16, 31, 0, 0, 0, 9, 0), OP(4, 0, 22, 31, 0, 0, 0, 9, 0)}, 0},
    /* Brass: the modulators slower to open (the blat) */
    {GD(KT_MOD, KT_ATTACK, KT_RELEASE, KT_VIB, 0, 40, 25, 0, 0, 0, 0), GN_FM, 4, 4, 0, 0,
     {OP(1, 0, 24, 18, 4, 2, 2, 7, 1), OP(1, 0, 4, 22, 3, 1, 1, 7, 1), OP(1, 3, 28, 17, 4, 2, 2, 7, 1), OP(1, 7, 8, 21, 3, 1, 1, 7, 1)}, 0},
    /* Strings: one light modulator, three carriers detuned apart, slow */
    {GD(KT_ATTACK, KT_RELEASE, KT_DETUNE, KT_VIB, 25, 35, 30, 0, 0, 0, 0), GN_FM, 5, 2, 0, 0,
     {OP(1, 0, 40, 14, 2, 1, 2, 5, 0), OP(1, 1, 14, 13, 1, 0, 1, 5, 0), OP(1, 5, 16, 12, 1, 0, 1, 5, 0), OP(2, 2, 22, 12, 1, 0, 1, 5, 0)}, 0},
    /* Scream: a chain fed back to a saw, screaming (the shooters' leads) */
    {GD(KT_FB, KT_MOD, KT_VIB, KT_RELEASE, 30, 50, 15, 0, 0, 0, 0), GN_FM, 0, 6, 0, 0,
     {OP(1, 0, 22, 31, 2, 1, 1, 7, 1), OP(1, 3, 26, 31, 2, 1, 1, 7, 1), OP(1, 7, 30, 31, 3, 1, 2, 7, 1), OP(1, 0, 0, 31, 2, 1, 1, 7, 1)}, 0},
    /* FM guitar: overdriven, its modulators dying into the sustain */
    {GD(KT_MOD, KT_FB, KT_DECAY, KT_VIB, 20, 45, 30, 0, 0, 0, -1), GN_FM, 1, 6, 0, 0,
     {OP(1, 0, 20, 31, 6, 2, 3, 7, 1), OP(3, 0, 24, 31, 9, 3, 4, 7, 1), OP(1, 0, 18, 31, 4, 2, 2, 7, 1), OP(1, 0, 0, 31, 4, 2, 2, 7, 1)}, 0},
    /* Marimba: a struck pair, short */
    {GD(KT_MOD, KT_DECAY, KT_RELEASE, KT_NONE, 0, 30, 0, 0, 0, 0, 0), GN_FM, 4, 0, 0, 0,
     {OP(4, 0, 34, 31, 20, 14, 15, 8, 2), OP(1, 0, 6, 31, 12, 8, 15, 8, 2), OP(10, 0, 44, 31, 24, 16, 15, 8, 2), OP(1, 0, 12, 31, 13, 9, 15, 8, 2)}, 0},
    /* PSG square */
    {GP(KT_VIB, KT_DECAY, KT_SUSTAIN, KT_RELEASE, 0, 40, 70, 15, 30, 40, 25, 0, 0), GN_PSG, 0, 0, 0, 0, {{0}}, 0},
    /* the DAC's */
    {GP(KT_DECAY, KT_OCTAVE, KT_NONE, KT_NONE, 0, 0, 100, 0, 0, 30, 0, DF_HOLD, 0), GN_DAC, 0, 0, 0, 0, {{0}}, BK_GEN_HIT},
    {GP(KT_OCTAVE, KT_NONE, KT_NONE, KT_NONE, 0, 0, 100, 0, 0, 30, 0, DF_HOLD, 0), GN_DAC, 0, 0, 0, 0, {{0}}, BK_GEN_VOICE},
    /* Ch3 bells: each operator its own frequency (1, 2.76, 5.4, 8.9: a bell's) */
    {GD(KT_DECAY, KT_RELEASE, KT_DETUNE, KT_FB, 0, 30, 0, 0, 0, 0, 0), GN_FM, 7, 0, 0, 0,
     {OPR(16, 8, 31, 6, 4, 15, 6), OPR(44, 16, 31, 8, 5, 15, 6), OPR(86, 22, 31, 10, 7, 15, 6), OPR(142, 28, 31, 12, 8, 15, 6)}, 0},
    /* Laser: fed back, falling */
    {GD(KT_FB, KT_SWEEP, KT_DECAY, KT_MOD, 0, 30, 0, -48, 40, 0, 1), GN_FM, 0, 6, 0, 0,
     {OP(1, 0, 18, 31, 6, 4, 4, 9, 0), OP(2, 0, 22, 31, 8, 4, 5, 9, 0), OP(1, 0, 26, 31, 6, 4, 4, 9, 0), OP(1, 0, 0, 31, 6, 4, 3, 9, 0)}, 0},
};
static const en_sound_t GEN_SND[EN_NSOUND] = {
    {"Slap", "YM2612 FM", {"Bright", "Feedback", "Decay", "Release"}, {50, 71, 50, 50}, {0, 0, 0, 0}, &GEN_P[0]},
    {"House", "YM2612 FM", {"Bright", "Decay", "Feedback", "Release"}, {50, 50, 57, 50}, {0, 0, 0, 0}, &GEN_P[1]},
    {"Rock", "YM2612 FM", {"Feedback", "Bright", "Decay", "Release"}, {100, 50, 50, 50}, {0, 0, 0, 0}, &GEN_P[2]},
    {"Wood", "YM2612 FM", {"Bright", "Attack", "Decay", "Vibrato"}, {50, 0, 50, 0}, {0, 0, 0, 0}, &GEN_P[3]},
    {"E.Piano", "YM2612 FM", {"Tine", "Decay", "Release", "Detune"}, {50, 50, 50, 20}, {0, 0, 0, 0}, &GEN_P[4]},
    {"Organ", "YM2612 FM", {"Feedback", "Attack", "Release", "Vibrato"}, {43, 0, 50, 0}, {0, 0, 0, 0}, &GEN_P[5]},
    {"Brass", "YM2612 FM", {"Bright", "Attack", "Release", "Vibrato"}, {50, 0, 50, 25}, {0, 0, 0, 0}, &GEN_P[6]},
    {"Strings", "YM2612 FM", {"Attack", "Release", "Detune", "Vibrato"}, {0, 50, 30, 25}, {0, 0, 0, 0}, &GEN_P[7]},
    {"Scream", "YM2612 FM", {"Feedback", "Bright", "Vibrato", "Release"}, {86, 50, 30, 50}, {0, 0, 0, 0}, &GEN_P[8]},
    {"FM guitar", "YM2612 FM", {"Bright", "Feedback", "Decay", "Vibrato"}, {50, 86, 50, 20}, {0, 0, 0, 0}, &GEN_P[9]},
    {"Marimba", "YM2612 FM", {"Bright", "Decay", "Release", ""}, {50, 50, 50, 0}, {0, 0, 0, 0}, &GEN_P[10]},
    {"PSG square", "SN76489", {"Vibrato", "Decay", "Sustain", "Release"}, {30, 40, 70, 15}, {0, 0, 0, 0}, &GEN_P[11]},
    {"Orch hit", "8-bit DAC", {"Decay", "Octave", "", ""}, {100, 50, 0, 0}, {0, 3, 0, 0}, &GEN_P[12]},
    {"DAC voice", "8-bit DAC", {"Octave", "", "", ""}, {50, 0, 0, 0}, {3, 0, 0, 0}, &GEN_P[13]},
    {"Ch3 bells", "YM2612 channel 3", {"Decay", "Release", "Detune", "Feedback"}, {50, 50, 0, 0}, {0, 0, 0, 0}, &GEN_P[14]},
    {"Laser", "YM2612 FM", {"Feedback", "Sweep", "Decay", "Bright"}, {86, 15, 50, 50}, {0, 0, 0, 0}, &GEN_P[15]},
};

/* the kits: the DAC's 4-bit drums (a house kit), and the FM's own with the PSG's noise */
#define GDG(smp) {GP(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 0, 100, 0, 0, 30, 0, DF_HOLD, 0), GN_DAC, 0, 0, 0, 0, {{0}}, smp}
#define GFM(alg, fb, sw, swt, o1, o2, o3, o4) {{{KT_NONE, KT_DECAY, KT_TONE, KT_NONE}, 0, 0, 100, 0, 0, 30, 0, sw, swt, 0, DF_HWENV, 0}, GN_FM, alg, fb, 0, 0, {o1, o2, o3, o4}, 0}
#define GNZ(dec) {GP(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, dec, 0, 0, 0, 30, 0, DF_NOREL, 0), GN_NOISE, 0, 0, 0, 0, {{0}}, 0}
#define OFF OP(1, 0, 127, 31, 0, 0, 0, 15, 0)
static const gen_patch_t GEN_DP[2][EN_NPART] = {
    {GDG(BK_GEN_KICK), GDG(BK_GEN_SNARE), GDG(BK_GEN_TOM), GDG(BK_GEN_TOM), GDG(BK_GEN_TOM), GDG(BK_GEN_RIM),
     GDG(BK_GEN_CLAP), GDG(BK_GEN_CHAT), GDG(BK_GEN_OHAT), GDG(BK_GEN_CRASH), GDG(BK_GEN_COW)},
    {GFM(4, 0, -40, 6, OP(1, 0, 22, 31, 31, 20, 15, 15, 0), OP(1, 0, 0, 31, 17, 12, 15, 12, 0), OFF, OFF),
     GFM(7, 7, -8, 4, OP(13, 0, 6, 31, 18, 14, 15, 14, 0), OP(1, 0, 4, 31, 20, 16, 15, 14, 0), OFF, OFF),
     GFM(4, 0, -12, 10, OP(1, 0, 34, 31, 24, 16, 15, 12, 0), OP(1, 0, 0, 31, 18, 12, 15, 11, 0), OFF, OFF),
     GFM(4, 0, -12, 10, OP(1, 0, 34, 31, 24, 16, 15, 12, 0), OP(1, 0, 0, 31, 18, 12, 15, 11, 0), OFF, OFF),
     GFM(4, 0, -12, 10, OP(1, 0, 34, 31, 24, 16, 15, 12, 0), OP(1, 0, 0, 31, 19, 12, 15, 11, 0), OFF, OFF),
     GFM(4, 3, 0, 0, OP(7, 0, 20, 31, 28, 24, 15, 15, 0), OP(3, 0, 2, 31, 24, 20, 15, 15, 0), OFF, OFF),
     GNZ(24), GNZ(10), GNZ(36), GNZ(55),
     GFM(7, 0, 0, 0, OP(3, 0, 8, 31, 14, 10, 15, 12, 0), OPR(36, 10, 31, 14, 10, 15, 12), OFF, OFF)},
};
#undef GDG
#undef GFM
#undef GNZ
#undef OFF
#undef OP
#undef OPR
#undef GD
#undef GP
static const dkit_t GEN_KIT[EN_NKIT] = {
    {"DAC", {{"KCK", "Kick", &GEN_DP[0][0], 60, 0, 0}, {"SNR", "Snare", &GEN_DP[0][1], 60, 0, 0},
             {"LT", "Low tom", &GEN_DP[0][2], 55, -100, 0}, {"MT", "Mid tom", &GEN_DP[0][3], 60, 0, 0},
             {"HT", "High tom", &GEN_DP[0][4], 65, 100, 0}, {"RIM", "Rim", &GEN_DP[0][5], 60, 0, 0},
             {"CLP", "Clap", &GEN_DP[0][6], 60, 0, 0}, {"CH", "Closed hat", &GEN_DP[0][7], 60, 0, 1},
             {"OH", "Open hat", &GEN_DP[0][8], 60, 0, 1}, {"CRS", "Crash", &GEN_DP[0][9], 60, 0, 0},
             {"CB", "Cowbell", &GEN_DP[0][10], 60, 0, 0}}},
    {"FM", {{"KCK", "Kick", &GEN_DP[1][0], 40, 0, 0}, {"SNR", "Snare", &GEN_DP[1][1], 55, 0, 0},
            {"LT", "Low tom", &GEN_DP[1][2], 45, -100, 0}, {"MT", "Mid tom", &GEN_DP[1][3], 52, 0, 0},
            {"HT", "High tom", &GEN_DP[1][4], 59, 100, 0}, {"RIM", "Rim", &GEN_DP[1][5], 76, 0, 0},
            {"CLP", "Clap", &GEN_DP[1][6], 60, 0, 0}, {"CH", "Closed hat", &GEN_DP[1][7], 84, 0, 1},
            {"OH", "Open hat", &GEN_DP[1][8], 84, 0, 1}, {"CRS", "Crash", &GEN_DP[1][9], 72, 0, 0},
            {"CB", "Cowbell", &GEN_DP[1][10], 79, 0, 0}}},
};
static const engine_t GEN_ENG = {44100.0f, 60.0f, gen_start, gen_frame, gen_render, gen_release};
