/* SPDX-License-Identifier: GPL-3.0-only */
/* The consoles' outputs and effects. Included once, by crt4_cons.c.
 *   the TV         a CRT set's little speaker (TV, on the chip consoles' FX page): the low end and the top
 *                  gone, a honk in the middle, a little crunch
 *   NES            the Famicom's output: a high-pass near 37 Hz, a low-pass at 14 kHz
 *   Game Boy       its high-pass (the capacitor on its output), the model's low-pass; its own speaker
 *   Genesis        the Model 1's low-pass (warm), or the Model 2's
 *   SNES           the echo: a delay of up to 240 ms in the sound memory, its feedback, its 8-tap FIR filter
 *   PlayStation    the SPU's reverb: the hardware's network (reflections on the same side and across, four
 *                  combs, two allpasses) at 22 kHz, with presets of our own after its kinds
 *   N64            the audio library's kinds: a small and a big room, an echo, a chorus, a flange
 * The SNES's echo, the PlayStation's and the N64's effects share one block of memory (one console at a time). */

#define FXN 32768u
static int16_t fxmem[FXN] EN_POOL;    /* the effects' memory: the SNES's echo, the SPU's reverb, the N64's */
static int fx_pos(int k);

static inline int16_t f2s(float x)
{
    x *= 16384.0f;
    if (x > 32767.0f)
        x = 32767.0f;
    if (x < -32768.0f)
        x = -32768.0f;
    return (int16_t)x;
}
static inline float s2f(int16_t s) { return (float)s * (1.0f / 16384.0f); }

/* ---------------------------------------------------------------- the TV --- */
static float tv_s[2][4], tv_c[4], tv_h[4], tv_amt = -1.0f;
static void tv_run(float *l, float *r, uint32_t n, float amt)
{
    uint32_t j;
    float bp;
    if (amt <= 0.01f)
        return;
    if (fm_fabsf(amt - tv_amt) > 0.001f) {
        svf_coefs(tv_c, 4200.0f, 1.2f);         /* the speaker's top */
        svf_coefs(tv_h, 170.0f, 1.0f);          /* and its bottom */
        tv_amt = amt;
    }
    for (j = 0; j < n; j++) {
        float a = svf_hp(&tv_s[0][2], tv_h, svf(&tv_s[0][0], tv_c, l[j], &bp) + bp * 0.35f, &bp);
        float b = svf_hp(&tv_s[1][2], tv_h, svf(&tv_s[1][0], tv_c, r[j], &bp) + bp * 0.35f, &bp);
        a = sat(a * 1.4f) * 0.85f;
        b = sat(b * 1.4f) * 0.85f;
        l[j] += (a - l[j]) * amt;
        r[j] += (b - r[j]) * amt;
    }
}

/* ------------------------------------------------- the chip consoles' outputs --- */
static float op_l[4], op_r[4];          /* one-pole states: high-pass, low-pass, (a second pair) */
static inline void hp1(float *z, float k, float *x) { *z += k * (*x - *z); *x -= *z; }
static inline void lp1(float *z, float k, float *x) { *z += k * (*x - *z); *x = *z; }

static void nes_out(float *l, float *r, uint32_t n)
{
    const float kh = onepole(37.0f), kl = onepole(14000.0f);
    uint32_t j;
    for (j = 0; j < n; j++) {
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
}
static void tia_out(float *l, float *r, uint32_t n)   /* the 2600: a DC blocker and its RF's softness */
{
    const float kh = onepole(30.0f), kl = onepole(9000.0f);
    uint32_t j;
    for (j = 0; j < n; j++) {
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
}
static void sid_out(float *l, float *r, uint32_t n)   /* the C64's output stage: a low-pass, its DC blocked */
{
    const float kh = onepole(16.0f), kl = onepole(12000.0f);
    uint32_t j;
    for (j = 0; j < n; j++) {
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
}
static void pce_out(float *l, float *r, uint32_t n)
{
    const float kh = onepole(20.0f), kl = onepole(12500.0f);
    int st = fx_pos(2);
    uint32_t j;
    for (j = 0; j < n; j++) {
        if (!st) {
            float m = 0.5f * (l[j] + r[j]);
            l[j] = r[j] = m;
        }
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
}
static void gen_out(float *l, float *r, uint32_t n)   /* Model 1: a first-order low-pass near 3.4 kHz */
{
    const float kl = onepole(fx_pos(2) ? 15000.0f : 3390.0f), kh = onepole(15.0f);
    uint32_t j;
    for (j = 0; j < n; j++) {
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
}
static void gb_out(float *l, float *r, uint32_t n)
{
    static const float HPF[3] = {28.0f, 28.0f, 60.0f}, LPF[3] = {9500.0f, 13000.0f, 16000.0f};
    int m = fx_pos(2);
    float kh = onepole(HPF[m % 3]), kl = onepole(LPF[m % 3]), spk = (float)fx_pos(3) * 0.01f;
    uint32_t j;
    for (j = 0; j < n; j++) {
        hp1(&op_l[0], kh, &l[j]);
        hp1(&op_r[0], kh, &r[j]);
        lp1(&op_l[1], kl, &l[j]);
        lp1(&op_r[1], kl, &r[j]);
    }
    if (spk > 0.01f) {                  /* its own speaker: mono, small, a little rough */
        const float sh = onepole(420.0f), sl = onepole(3800.0f);
        for (j = 0; j < n; j++) {
            float m2 = 0.5f * (l[j] + r[j]);
            hp1(&op_l[2], sh, &m2);
            lp1(&op_l[3], sl, &m2);
            m2 = sat(m2 * 1.8f) * 0.7f;
            l[j] += (m2 - l[j]) * spk;
            r[j] += (m2 - r[j]) * spk;
        }
    }
}

/* ------------------------------------------------------------- SNES: echo --- */
/* the echo buffer: EDL x 512 frames (16 ms at 32 kHz), stereo, in the sound memory; each frame read is
 * filtered by the FIR (its last eight), fed back (EFB) with the input, and heard (EVOL) */
static const int8_t SNES_FIR[4][8] = {
    {127, 0, 0, 0, 0, 0, 0, 0},       /* flat */
    {56, 40, 20, 8, 2, 0, 0, 0},      /* soft */
    {24, 28, 26, 20, 14, 8, 4, 2},    /* dark */
    {112, -22, 12, -4, 2, 0, 0, 0},   /* bright */
};
static uint32_t ec_w, ec_len;
static float fir_h[2][8];
static int fir_i;
static void snes_out(float *l, float *r, float *sl, float *sr, uint32_t n)
{
    uint32_t j, len = 512u * (uint32_t)(1 + clampi(en_cfg.par[P_FX1], 0, 100) * 14 / 100);
    float efb = (float)clampi(en_cfg.par[P_FX2], 0, 100) * 0.0085f, evol = (float)clampi(en_cfg.par[P_FX4], 0, 100) * 0.012f;
    const int8_t *c = SNES_FIR[fx_pos(2) & 3];
    float cf[8];
    int k;
    if (en_fx_now == FX_ECHO && en_playing)
        efb = fm_maxf(efb, 0.7f);
    for (k = 0; k < 8; k++)
        cf[k] = (float)c[k] * (1.0f / 128.0f);
    if (len != ec_len) {               /* a new delay: the memory is the console's, so whatever was there */
        ec_len = len;
        if (ec_w >= len)
            ec_w = 0;
    }
    for (j = 0; j < n; j++) {
        float el = s2f(fxmem[2 * ec_w]), er = s2f(fxmem[2 * ec_w + 1]), fl = 0.0f, fr = 0.0f;
        int i;
        fir_h[0][fir_i] = el;
        fir_h[1][fir_i] = er;
        for (k = 0; k < 8; k++) {
            i = (fir_i - k) & 7;
            fl += fir_h[0][i] * cf[k];
            fr += fir_h[1][i] * cf[k];
        }
        fir_i = (fir_i + 1) & 7;
        fxmem[2 * ec_w] = f2s(sl[j] + fl * efb);
        fxmem[2 * ec_w + 1] = f2s(sr[j] + fr * efb);
        if (++ec_w >= ec_len)
            ec_w = 0;
        l[j] += fl * evol;
        r[j] += fr * evol;
    }
}

/* ---------------------------------------------------------- PlayStation: reverb --- */
/* the SPU's network, at 22.05 kHz: same-side and cross reflections (each the input and a wall's echo of
 * itself, smoothed by an IIR), four combs reading them, two allpasses, a side. Every line lives in one ring,
 * written at the top of its own stretch and read below it, as the hardware's presets lay them out. The
 * presets are ours, in its kinds: their delays in samples. */
typedef struct {
    uint16_t same, diff;                    /* the reflections' delays: on the same side, across */
    uint16_t comb[4];                       /* the combs: 0, 2 on the same side's line, 1, 3 across */
    uint16_t ap1, ap2;                      /* the allpasses' delays */
    float wall, iir, cg[4], g1, g2, out;
} rvb_t;
static const rvb_t RVB[9] = {
    {520, 640, {470, 410, 360, 300}, 260, 180, 0.6f, 0.6f, {0.6f, -0.5f, 0.45f, -0.4f}, 0.6f, 0.5f, 0.9f},
    {700, 840, {650, 560, 500, 430}, 300, 210, 0.55f, 0.65f, {0.6f, -0.5f, 0.45f, -0.4f}, 0.6f, 0.5f, 0.9f},
    {1300, 1550, {1200, 1050, 920, 800}, 420, 300, 0.62f, 0.7f, {0.6f, -0.5f, 0.45f, -0.4f}, 0.62f, 0.5f, 0.85f},
    {2200, 2600, {2000, 1780, 1560, 1360}, 560, 390, 0.7f, 0.75f, {0.6f, -0.5f, 0.45f, -0.4f}, 0.65f, 0.55f, 0.8f},
    {3600, 4200, {3300, 2900, 2550, 2200}, 760, 520, 0.78f, 0.8f, {0.6f, -0.5f, 0.45f, -0.4f}, 0.68f, 0.6f, 0.75f},
    {4400, 4400, {4400, 0, 0, 0}, 100, 60, 0.55f, 0.85f, {0.8f, 0.0f, 0.0f, 0.0f}, 0.4f, 0.3f, 0.8f},
    {6000, 6800, {5800, 5100, 4400, 3800}, 900, 640, 0.82f, 0.82f, {0.6f, -0.55f, 0.5f, -0.45f}, 0.7f, 0.6f, 0.75f},
    {6400, 6400, {6200, 5600, 4800, 4200}, 1600, 1200, 0.86f, 0.9f, {0.6f, 0.55f, 0.5f, 0.45f}, 0.7f, 0.65f, 0.65f},
    {7600, 7600, {7600, 0, 0, 0}, 2, 1, 0.75f, 1.0f, {0.95f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.75f},
};
static uint32_t rv_p;                   /* the ring's place (the buffer address): two a sample */
static float rv_in[2], rv_out[2][2], rv_lp[2];
static int rv_ph;
#define RL(o) fxmem[(rv_p + 2u * (uint32_t)(o)) & (FXN - 1u)]
#define RR(o) fxmem[(rv_p + 2u * (uint32_t)(o) + 1u) & (FXN - 1u)]
static void ps1_reverb(float li, float ri, float *lo, float *ro)
{
    const rvb_t *p = &RVB[fx_pos(0) % 9];
    float fb = 0.4f + 1.2f * (float)clampi(en_cfg.par[P_FX3], 0, 100) * 0.01f, wall = fm_minf(p->wall * fb, 0.97f), iir = p->iir;
    uint32_t a2 = 1u + p->ap2, a1 = a2 + 1u + p->ap1, dl = p->diff > p->comb[1] ? p->diff : p->comb[1], sm;
    float L, R, a;
    if (p->comb[3] > dl)
        dl = p->comb[3];
    dl += a1 + 1u;                     /* the cross line's top */
    sm = p->same > p->comb[0] ? p->same : p->comb[0];
    if (p->comb[2] > sm)
        sm = p->comb[2];
    sm += dl + 1u;                     /* the same side's top */
    /* the reflections */
    RL(sm) = f2s((li + s2f(RL(sm - p->same)) * wall - s2f(RL(sm - 1u))) * iir + s2f(RL(sm - 1u)));
    RR(sm) = f2s((ri + s2f(RR(sm - p->same)) * wall - s2f(RR(sm - 1u))) * iir + s2f(RR(sm - 1u)));
    RL(dl) = f2s((li + s2f(RR(dl - p->diff)) * wall - s2f(RL(dl - 1u))) * iir + s2f(RL(dl - 1u)));
    RR(dl) = f2s((ri + s2f(RL(dl - p->diff)) * wall - s2f(RR(dl - 1u))) * iir + s2f(RR(dl - 1u)));
    /* the combs */
    L = p->cg[0] * s2f(RL(sm - p->comb[0])) + p->cg[1] * s2f(RL(dl - p->comb[1])) + p->cg[2] * s2f(RL(sm - p->comb[2])) +
        p->cg[3] * s2f(RL(dl - p->comb[3]));
    R = p->cg[0] * s2f(RR(sm - p->comb[0])) + p->cg[1] * s2f(RR(dl - p->comb[1])) + p->cg[2] * s2f(RR(sm - p->comb[2])) +
        p->cg[3] * s2f(RR(dl - p->comb[3]));
    /* the allpasses */
    a = s2f(RL(a1 - p->ap1));
    L -= p->g1 * a;
    RL(a1) = f2s(L);
    L = L * p->g1 + a;
    a = s2f(RR(a1 - p->ap1));
    R -= p->g1 * a;
    RR(a1) = f2s(R);
    R = R * p->g1 + a;
    a = s2f(RL(a2 - p->ap2));
    L -= p->g2 * a;
    RL(a2) = f2s(L);
    L = L * p->g2 + a;
    a = s2f(RR(a2 - p->ap2));
    R -= p->g2 * a;
    RR(a2) = f2s(R);
    R = R * p->g2 + a;
    *lo = L * p->out;
    *ro = R * p->out;
    rv_p = (rv_p + 2u) & (FXN - 1u);   /* the base moves up: what was written at k is at k - 1 next sample */
}
#undef RL
#undef RR
static void ps1_out(float *l, float *r, float *sl, float *sr, uint32_t n)
{
    uint32_t j;
    float depth = (float)clampi(en_cfg.par[P_FX2], 0, 100) * 0.016f, tone = onepole(1500.0f + 120.0f * (float)fx_pos(3) * 100.0f * 0.01f);
    if (en_fx_now == FX_ECHO && en_playing)
        depth = fm_maxf(depth, 1.1f);
    for (j = 0; j < n; j++) {          /* at half rate: two in, one through the network, two out (slid) */
        float f;
        rv_in[0] += sl[j];
        rv_in[1] += sr[j];
        if (++rv_ph >= 2) {
            float a, b;
            rv_ph = 0;
            rv_lp[0] += tone * (rv_in[0] * 0.5f - rv_lp[0]);
            rv_lp[1] += tone * (rv_in[1] * 0.5f - rv_lp[1]);
            ps1_reverb(rv_lp[0], rv_lp[1], &a, &b);
            rv_out[0][0] = rv_out[0][1];
            rv_out[1][0] = rv_out[1][1];
            rv_out[0][1] = a;
            rv_out[1][1] = b;
            rv_in[0] = rv_in[1] = 0.0f;
        }
        f = (float)rv_ph * 0.5f;
        l[j] += (rv_out[0][0] + (rv_out[0][1] - rv_out[0][0]) * f) * depth;
        r[j] += (rv_out[1][0] + (rv_out[1][1] - rv_out[1][0]) * f) * depth;
    }
}

/* ------------------------------------------------------------------ N64: FX --- */
/* at the mixing rate: rooms (four combs damped, two allpasses: a little Schroeder), an echo, a chorus, a
 * flange; all in the shared memory */
static const uint16_t N64_COMB[2][4] = {{830, 940, 1060, 1170}, {1650, 1810, 1990, 2140}};
static const uint16_t N64_AP[2] = {225, 341};
static uint32_t n6_w;
static float n6_lp[4], n6_lfo;
#define N6(o) fxmem[(n6_w + FXN - (uint32_t)(o)) & (FXN - 1u)]
static void n64_out(float *l, float *r, float *sl, float *sr, uint32_t n)
{
    int type = fx_pos(0) % 5, k;
    float amt = (float)clampi(en_cfg.par[P_FX2], 0, 100) * 0.014f, fb = (float)clampi(en_cfg.par[P_FX3], 0, 100) * 0.01f;
    float rate = eng->rate;
    uint32_t j;
    if (en_fx_now == FX_ECHO && en_playing) {
        amt = fm_maxf(amt, 1.0f);
        fb = fm_maxf(fb, 0.75f);
    }
    for (j = 0; j < n; j++) {
        float in = 0.5f * (sl[j] + sr[j]), yl = 0.0f, yr = 0.0f;
        switch (type) {
        case 0:
        case 1: {                      /* a room: four combs into two allpasses (the comb lines 0..4 * 2400) */
            float s = 0.0f, g = (type ? 0.8f : 0.7f) + 0.15f * fb;
            int sz = type ? 1 : 0;
            for (k = 0; k < 4; k++) {
                float o = s2f(fxmem[(k * 2400u + ((n6_w + 2400u - N64_COMB[sz][k]) % 2400u))]);
                n6_lp[k] += 0.35f * (o - n6_lp[k]);
                fxmem[k * 2400u + n6_w % 2400u] = f2s(in + n6_lp[k] * fm_minf(g, 0.93f));
                s += o;
            }
            s *= 0.25f;
            for (k = 0; k < 2; k++) {  /* the allpasses: 9600.. 9600 + 1024 */
                uint32_t base = 9600u + 512u * (uint32_t)k, w = n6_w % 512u;
                float d = s2f(fxmem[base + (w + 512u - N64_AP[k]) % 512u]), v = s + 0.5f * d;
                fxmem[base + w] = f2s(v);
                s = d - 0.5f * v;
            }
            yl = s;
            yr = s2f(fxmem[(3u * 2400u + ((n6_w + 2400u - 1100u) % 2400u))]) * 0.25f + s * 0.75f;
            break;
        }
        case 2: {                      /* an echo: up to 0.6 s, its repeats darkened */
            uint32_t d = (uint32_t)(rate * 0.32f);
            float o = s2f(N6(d));
            n6_lp[0] += 0.4f * (o - n6_lp[0]);
            N6(0) = f2s(in + n6_lp[0] * fm_minf(fb, 0.9f));
            yl = o;
            yr = s2f(N6(d / 2u)) * 0.6f;
            break;
        }
        default: {                     /* a chorus (12 ms, slow) or a flange (2 ms, fed back) */
            float lfo, dd, o1, o2, base = type == 3 ? 0.012f : 0.002f, dep = type == 3 ? 0.004f : 0.0016f;
            uint32_t i1, i2;
            n6_lfo = wrap1(n6_lfo + (type == 3 ? 0.8f : 0.25f) / rate);
            lfo = sine(n6_lfo);
            dd = (base + dep * lfo) * rate;
            i1 = (uint32_t)dd;
            o1 = s2f(N6(i1));
            dd = (base - dep * lfo) * rate;
            i2 = (uint32_t)dd;
            o2 = s2f(N6(i2));
            N6(0) = f2s(in + (type == 4 ? o1 * fm_minf(fb, 0.85f) : 0.0f));
            yl = o1;
            yr = o2;
            break;
        }
        }
        n6_w = (n6_w + 1u) & (FXN - 1u);
        l[j] += yl * amt;
        r[j] += yr * amt;
    }
}
#undef N6

static void fx_reset(void)
{
    uint32_t i;
    for (i = 0; i < FXN; i++)
        fxmem[i] = 0;
    for (i = 0; i < 4; i++) {
        op_l[i] = op_r[i] = 0.0f;
        n6_lp[i] = 0.0f;
    }
    ec_w = 0;
    ec_len = 0;
    rv_p = 0;
    n6_w = 0;
    for (i = 0; i < 8; i++)
        fir_h[0][i] = fir_h[1][i] = 0.0f;
    rv_in[0] = rv_in[1] = 0.0f;
    tv_amt = -1.0f;
    {
        int a, b;
        for (a = 0; a < 2; a++)
            for (b = 0; b < 4; b++)
                tv_s[a][b] = 0.0f;
    }
    drv_hz = eng->frame_hz;
}
