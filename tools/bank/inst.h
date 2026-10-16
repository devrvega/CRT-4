/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's sample bank: the instruments, synthesized, one note each (the consoles pitch them). Plucked and
 * struck strings by Karplus-Strong's delay line (its loss, its pick, its stiffness); bars, bells, tines and
 * pans by their modes; bowed strings and sections, voices and choirs, winds and brass by their sources and
 * their resonances (bodies, formants, bores); organs by their ranks; synths as the circuits would. Each
 * returns its note at SR; the bank cuts it to the console's size, rate and format. */
#pragma once
#include "dsp.h"
#include "drums.h"

/* a band-limited saw (or square, sq) at phase ph, frequency f: polyBLEP, fine at 48 kHz for what follows */
static double pblep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}
static double bl_saw(double ph, double f, int sq)
{
    double dt = f / SR, t = ph - floor(ph), s, t2;
    if (dt > 0.45)
        dt = 0.45;
    if (!sq)
        return 2.0 * t - 1.0 - pblep(t, dt);
    s = (t < 0.5 ? 1.0 : -1.0) + pblep(t, dt);
    t2 = t + 0.5;
    t2 -= floor(t2);
    return s - pblep(t2, dt);
}

/* ----------------------------------------------------------- plucked strings --- */
/* the string: a delay of one period (the fraction by an allpass), a loss filter (bright: its high end
 * kept), the pluck's position (a comb on the excitation), stiffness (allpasses: the partials sharpen) */
typedef struct {
    double bright;                     /* 0..1: the loss filter (how fast the high partials die) */
    double pick;                       /* 0..0.5: where it is plucked (0.5 the middle) */
    double decay;                      /* seconds to -60 dB (the fundamental) */
    double stiff;                      /* 0..1: dispersion */
    double exc_lp;                     /* the excitation's brightness (Hz) */
    double exc_amp;
} str_t;
static buf_t ks_string(double f, double dur, str_t p)
{
    buf_t b = buf_new(dur);
    int L = (int)floor(SR / f - 0.5), i, w = 0;
    double frac, ap_c, ap_z = 0, ap_x = 0, lp_z = 0, g, *d, ex_lp = 0, st_z[2] = {0, 0}, st_x[2] = {0, 0};
    double sc = p.stiff * -0.7, s = 0.5 + 0.49 * p.bright;
    /* the allpasses' delay at f (two of them, coefficient sc), and the loss filter's half sample, come off */
    double stdel = p.stiff > 0 ? 2.0 * (1.0 - sc) / (1.0 + sc) : 0.0;
    frac = SR / f - (double)L - 0.5 * (1.0 - s) / 1.0 - stdel;
    while (frac < 0.1) {
        L--;
        frac += 1.0;
    }
    ap_c = (1.0 - frac) / (1.0 + frac);
    g = pow(10.0, -3.0 / (f * p.decay));
    d = (double *)calloc((size_t)L + 2, sizeof(double));
    for (i = 0; i < L; i++) {          /* the pluck: noise, low-passed, the pick's comb */
        double e = rndn();
        ex_lp += op_c(p.exc_lp) * (e - ex_lp);
        d[i] = ex_lp;
    }
    {
        int pk = (int)(p.pick * L), k;
        double *t = (double *)calloc((size_t)L, sizeof(double)), m = 0;
        for (k = 0; k < L; k++)
            t[k] = d[k] - (k >= pk ? d[k - pk] : 0.0);
        for (k = 0; k < L; k++)
            m += t[k];
        for (k = 0; k < L; k++)
            d[k] = (t[k] - m / L) * p.exc_amp;
        free(t);
    }
    for (i = 0; i < b.n; i++) {
        double x = d[w], y;
        y = s * x + (1.0 - s) * lp_z;  /* the loss: one zero */
        lp_z = x;
        y *= g;
        if (p.stiff > 0) {             /* the stiffness: two allpasses */
            int k;
            for (k = 0; k < 2; k++) {
                double o = sc * y + st_x[k] - sc * st_z[k];
                st_x[k] = y;
                st_z[k] = o;
                y = o;
            }
        }
        {                              /* the tuning: the fractional delay */
            double o = ap_c * y + ap_x - ap_c * ap_z;
            ap_x = y;
            ap_z = o;
            y = o;
        }
        d[w] = y;
        w = (w + 1) % L;
        b.x[i] = x;
    }
    free(d);
    return b;
}
static buf_t slap_bass(double f)
{
    str_t p = {0.85, 0.12, 2.2, 0.15, 6000, 1.6};
    buf_t b = ks_string(f, 1.6, p);
    int i;
    noise_add(&b, BQ_BP, 2200, 1.2, 0.0002, 0.03, 0.35);    /* the slap: the string on the frets */
    for (i = 0; i < b.n; i++)
        b.x[i] = softclip(b.x[i] * 1.3);
    bq_run(&b, BQ_PEAK, 500, 0.8, -6);
    bq_run(&b, BQ_PEAK, 2500, 1.0, 5);
    bq_run(&b, BQ_LSHELF, 100, 0.7, 4);
    return b;
}
static buf_t finger_bass(double f)
{
    str_t p = {0.35, 0.22, 2.6, 0.1, 1500, 1.0};
    buf_t b = ks_string(f, 1.6, p);
    noise_add(&b, BQ_LP, 300, 0.7, 0.001, 0.03, 0.3);       /* the finger */
    bq_run(&b, BQ_LP, 1800, 0.7, 0);
    bq_run(&b, BQ_PEAK, 700, 1.0, 3);
    return b;
}
static buf_t upright_bass(double f)
{
    str_t p = {0.3, 0.2, 1.3, 0.05, 1200, 1.0};
    buf_t b = ks_string(f, 1.4, p);
    noise_add(&b, BQ_LP, 250, 0.7, 0.001, 0.05, 0.5);
    bq_run(&b, BQ_PEAK, 110, 2.0, 6);                       /* the body */
    bq_run(&b, BQ_PEAK, 230, 2.0, 4);
    bq_run(&b, BQ_LP, 2200, 0.7, 0);
    return b;
}
/* the violin family's body: its main resonances (the air's, the wood's, the bridge's) */
static void violin_body(buf_t *b, double scale)
{
    bq_run(b, BQ_PEAK, 280 * scale, 3, 6);
    bq_run(b, BQ_PEAK, 460 * scale, 3, 5);
    bq_run(b, BQ_PEAK, 1100 * scale, 2, 3);
    bq_run(b, BQ_PEAK, 2900 * scale, 2, 6);
    bq_run(b, BQ_HSHELF, 6000, 0.7, -6);
}
static buf_t pizzicato(double f)
{
    buf_t b = buf_new(0.9);
    int k;
    for (k = 0; k < 3; k++) {          /* a section: three, a little apart in time and tune */
        str_t p = {0.45, 0.3, 0.5, 0.02, 2500, 1.0};
        buf_t s = ks_string(f * (1.0 + 0.003 * rndn()), 0.85, p);
        buf_add(&b, &s, (int)(rnd01() * 0.012 * SR), 0.5);
        buf_free(&s);
    }
    violin_body(&b, f < 200 ? 0.6 : 1.0);
    return b;
}
static buf_t harp(double f)
{
    str_t p = {0.55, 0.14, 3.0, 0.08, 3500, 1.0};
    buf_t b = ks_string(f, 1.8, p);
    bq_run(&b, BQ_PEAK, 300, 1.0, 3);
    bq_run(&b, BQ_HSHELF, 5000, 0.7, -4);
    return b;
}
static buf_t banjo(double f)
{
    str_t p = {0.92, 0.09, 0.9, 0.1, 9000, 1.2};
    buf_t b = ks_string(f, 1.0, p);
    bq_run(&b, BQ_PEAK, 380, 2.5, 8);                       /* the head */
    bq_run(&b, BQ_PEAK, 2600, 1.5, 4);
    bq_run(&b, BQ_HP, 120, 0.7, 0);
    return b;
}
static buf_t harpsichord(double f)
{
    buf_t b = buf_new(1.5);
    str_t p8 = {0.95, 0.07, 1.6, 0.1, 12000, 1.0}, p4 = {0.95, 0.06, 1.2, 0.1, 12000, 0.5};
    buf_t a = ks_string(f, 1.5, p8), c = ks_string(f * 2.003, 1.5, p4);
    buf_add(&b, &a, 0, 1.0);
    buf_add(&b, &c, 0, 0.6);
    noise_add(&b, BQ_BP, 1800, 1.0, 0.0001, 0.01, 0.3);     /* the quill */
    bq_run(&b, BQ_PEAK, 220, 1.5, 4);
    buf_free(&a);
    buf_free(&c);
    return b;
}
/* the sitar: bright, the bridge's buzz sweeping down (the jawari), its sympathetic strings */
static buf_t sitar(double f)
{
    str_t p = {0.97, 0.05, 2.5, 0.2, 12000, 1.0};
    buf_t b = ks_string(f, 2.0, p);
    int i, k;
    bq_t bp;
    memset(&bp, 0, sizeof bp);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, fc = 900 + 2600 * exp(-t / 0.35);
        if (i % 32 == 0)
            bq_set(&bp, BQ_BP, fc, 4.0, 0);
        b.x[i] = b.x[i] * 0.6 + bq(&bp, b.x[i]) * 1.8;
    }
    for (k = 0; k < 3; k++) {          /* the sympathetic strings: an octave, a fifth, two octaves */
        static const double R[3] = {2.0, 1.5, 4.0};
        str_t q = {0.8, 0.3, 3.0, 0.1, 6000, 0.12};
        buf_t s = ks_string(f * R[k], 2.0, q);
        buf_add(&b, &s, (int)(0.03 * SR), 1.0);
        buf_free(&s);
    }
    return b;
}
/* guitars: the string, its pickup, the amplifier's clipping, the cabinet */
static buf_t guitar(double f, double drive, double vib)
{
    str_t p = {0.7, 0.18, 4.0, 0.1, 5000, 1.0};
    buf_t b = ks_string(f, 1.6, p);
    int i;
    double ph = 0;
    bq_run(&b, BQ_HP, 120, 0.7, 0);
    bq_run(&b, BQ_PEAK, 800, 0.8, 4);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, x = b.x[i] * (1.0 + 0.03 * vib * sin(TAU * 5.5 * t) * (t > 0.3));
        ph += 1;
        x = tanh(x * drive) + 0.15 * tanh(x * drive * 0.5) * tanh(x * drive * 0.5);   /* even harmonics too */
        b.x[i] = x;
    }
    bq_run(&b, BQ_LP, 4200, 0.9, 0);                         /* the cabinet: 12 dB at 4 kHz, a bump at 1.2 */
    bq_run(&b, BQ_LP, 5200, 0.7, 0);
    bq_run(&b, BQ_PEAK, 1200, 1.2, 4);
    bq_run(&b, BQ_HP, 90, 0.7, 0);
    return b;
}
static buf_t fretless(double f)
{
    str_t p = {0.4, 0.25, 3.0, 0.05, 1200, 1.0};
    buf_t b = ks_string(f, 1.8, p);
    int i;
    bq_t bp;
    memset(&bp, 0, sizeof bp);
    for (i = 0; i < b.n; i++) {        /* the mwah: a formant rising as the note blooms */
        double t = (double)i / SR, fc = 350 + 650 * (1.0 - exp(-t / 0.25));
        if (i % 32 == 0)
            bq_set(&bp, BQ_BP, fc, 2.5, 0);
        b.x[i] = b.x[i] * (t < 0.03 ? t / 0.03 : 1.0) + bq(&bp, b.x[i]) * 1.2;
    }
    bq_run(&b, BQ_LP, 2000, 0.7, 0);
    return b;
}

/* --------------------------------------------------------- struck and plucked --- */
static buf_t piano(double f)
{
    buf_t b = buf_new(2.4);
    double B = 0.00035 * pow(f / 261.6, 0.6);
    int s, n, i;
    for (s = 0; s < 3; s++) {          /* three strings, a few cents apart: the beating */
        double fs = f * (1.0 + (s - 1) * 0.0006);
        for (n = 1; n <= 40; n++) {
            double fn = n * fs * sqrt(1.0 + B * n * n), a, d1, d2, ph = rnd01();
            if (fn > SR * 0.45)
                break;
            a = pow(n, -1.1) * (1.0 - 0.9 * fabs(sin(M_PI * n / 7.5))) / 3.0;   /* the hammer at 1/7.5 */
            d1 = 0.35 / (1.0 + 0.25 * n);
            d2 = 6.0 / (1.0 + 0.15 * n) * pow(261.6 / f, 0.4);
            for (i = 0; i < b.n; i++) {
                double t = (double)i / SR, e = 0.65 * exp(-6.9 * t / d1) + 0.35 * exp(-6.9 * t / d2);
                b.x[i] += a * sin(TAU * (fn * t + ph)) * e;
            }
        }
    }
    noise_add(&b, BQ_LP, 1500, 0.7, 0.0005, 0.03, 0.15);    /* the hammer's thump */
    bq_run(&b, BQ_PEAK, 160, 1.0, 2);                        /* the soundboard */
    return b;
}
/* the DX7's electric piano: two FM pairs (the body, and the tine's ping) */
static buf_t fm_epiano(double f)
{
    buf_t b = buf_new(2.0);
    int i;
    double p1 = 0, p2 = 0, p3 = 0, p4 = 0;
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, i1 = 1.4 * exp(-t / 0.6), i2 = 2.2 * exp(-t / 0.05);
        double m1, m2;
        p1 += f / SR;
        p2 += f / SR;
        p3 += 14.0 * f / SR;
        p4 += f / SR;
        m1 = sin(TAU * p1) * i1;
        m2 = sin(TAU * p3) * i2;
        b.x[i] = sin(TAU * p2 + m1) * exp(-t / 1.4) * 0.7 + sin(TAU * p4 + m2) * exp(-t / 0.4) * 0.35;
    }
    return b;
}
/* a Rhodes: the tine (a near sine, a bell partial at first), the pickup's lopsided pull (its bark) */
static buf_t rhodes(double f)
{
    buf_t b = buf_new(2.2);
    int i;
    double ph = 0, pb = 0;
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, x, e = exp(-t / 1.6);
        ph += f / SR;
        pb += 7.1 * f / SR;
        x = sin(TAU * ph) * e + 0.25 * sin(TAU * pb) * exp(-t / 0.08);
        x = x + 0.35 * x * x - 0.1 * x * x * x;             /* the pickup */
        b.x[i] = x;
    }
    dc_remove(&b);
    bq_run(&b, BQ_LP, 5000, 0.7, 0);
    return b;
}
static buf_t marimba(double f)
{
    static const double R[3] = {1.0, 3.99, 10.65}, A[3] = {1.0, 0.35, 0.08}, D[3] = {1.1, 0.3, 0.07};
    double dd[3];
    int k;
    buf_t b = buf_new(1.3);
    for (k = 0; k < 3; k++)
        dd[k] = D[k] * pow(261.6 / f, 0.3);
    modes_add(&b, f, R, A, dd, 3, 0, 1);
    noise_add(&b, BQ_LP, 800, 0.7, 0.0005, 0.01, 0.15);
    return b;
}
static buf_t steel_drum(double f)
{
    static const double R[5] = {1.0, 2.0, 3.0, 2.01, 4.02}, A[5] = {1.0, 0.6, 0.25, 0.3, 0.12}, D[5] = {1.4, 1.0, 0.6, 0.9, 0.4};
    buf_t b = buf_new(1.5);
    modes_add(&b, f, R, A, D, 5, 0.004, 0.02);
    noise_add(&b, BQ_BP, 2000, 1.5, 0.0002, 0.01, 0.1);
    bq_run(&b, BQ_PEAK, 1300, 1.0, 3);
    return b;
}
static buf_t music_box(double f)
{
    static const double R[4] = {1.0, 5.4, 13.6, 2.0}, A[4] = {1.0, 0.25, 0.08, 0.05}, D[4] = {2.2, 0.25, 0.06, 1.0};
    buf_t b = buf_new(2.0);
    modes_add(&b, f, R, A, D, 4, 0, 1);
    noise_add(&b, BQ_HP, 4000, 0.7, 0.0001, 0.004, 0.2);    /* the pin */
    return b;
}

/* --------------------------------------------------------------- bowed, sung --- */
/* a bowed string: a saw's partials, the bow's noise, a vibrato after the start; its body */
static void bowed_add(buf_t *b, double f, double att, double vib, double vrate, double gain, double det)
{
    double ph = rnd01(), vph = rnd01();
    int i;
    bq_t bn;
    memset(&bn, 0, sizeof bn);
    bq_set(&bn, BQ_BP, f * 3.0, 2.0, 0);
    for (i = 0; i < b->n; i++) {
        double t = (double)i / SR, ff = f * (1.0 + det) * (1.0 + vib * sin(TAU * vph) * fmin(t / 0.3, 1.0)), e;
        vph += vrate / SR;
        ph += ff / SR;
        e = t < att ? t / att : 1.0;
        b->x[i] += (bl_saw(ph, ff, 0) * 0.8 + bq(&bn, rndn()) * 0.15) * e * gain;
    }
}
static buf_t fiddle(double f)
{
    buf_t b = buf_new(0.9);
    bowed_add(&b, f, 0.06, 0.006, 5.6, 1.0, 0);
    violin_body(&b, 1.0);
    bq_run(&b, BQ_HSHELF, 4000, 0.7, -3);
    return b;
}
static buf_t strings(double f, double att, int n)
{
    buf_t b = buf_new(1.0);
    int k;
    for (k = 0; k < n; k++)            /* a section: detuned, each its own vibrato */
        bowed_add(&b, f, att * (0.8 + 0.4 * rnd01()), 0.004 + 0.002 * rnd01(), 4.8 + 1.2 * rnd01(), 1.0 / n,
                  0.004 * rndn());
    violin_body(&b, f < 200 ? 0.6 : 1.0);
    bq_run(&b, BQ_LP, 7000, 0.7, 0);
    room(&b, 0.25, 1.0);
    return b;
}
/* a voice: a glottal pulse (a saw's spectrum, tilted), breath, through a vowel's five formants */
typedef struct {
    double f[5], bw[5], g[5];
} vowel_t;
static const vowel_t V_AH = {{800, 1150, 2900, 3900, 4950}, {80, 90, 120, 130, 140}, {1.0, 0.5, 0.25, 0.25, 0.1}};
static const vowel_t V_OO = {{350, 600, 2700, 3800, 4950}, {40, 60, 100, 120, 120}, {1.0, 0.3, 0.05, 0.05, 0.02}};
static const vowel_t V_EH = {{450, 1900, 2600, 3500, 4700}, {60, 90, 110, 120, 130}, {1.0, 0.4, 0.3, 0.2, 0.1}};
static const vowel_t V_EE = {{280, 2250, 2900, 3700, 4900}, {50, 90, 110, 120, 130}, {1.0, 0.3, 0.25, 0.2, 0.1}};
static const vowel_t V_UH = {{600, 1040, 2250, 2450, 2750}, {60, 70, 110, 120, 130}, {1.0, 0.45, 0.35, 0.3, 0.1}};
static void voice_add(buf_t *b, double f, const vowel_t *v0, const vowel_t *v1, double morph_t, double vib,
                      double breath, double gain, double att, double scoop)
{
    bq_t fm[5];
    double ph = rnd01(), vph = rnd01(), lp = 0;
    int i, k;
    memset(fm, 0, sizeof fm);
    for (i = 0; i < b->n; i++) {
        double t = (double)i / SR, m = morph_t > 0 ? fmin(t / morph_t, 1.0) : 0.0, x = 0, src, e;
        double ff = f * (1.0 + vib * sin(TAU * vph) * fmin(t / 0.25, 1.0)) * (1.0 - scoop * exp(-t / 0.06));
        vph += (5.2 + 0.3 * sin(t)) / SR;
        ph += ff / SR;
        if (i % 16 == 0)
            for (k = 0; k < 5; k++) {
                double fc = v0->f[k] + (v1->f[k] - v0->f[k]) * m, bw = v0->bw[k] + (v1->bw[k] - v0->bw[k]) * m;
                bq_set(&fm[k], BQ_BP, fc, fc / bw, 0);
            }
        src = bl_saw(ph, ff, 0);
        lp += 0.25 * (src - lp);
        src = lp + breath * rndn();
        for (k = 0; k < 5; k++)
            x += bq(&fm[k], src) * (v0->g[k] + (v1->g[k] - v0->g[k]) * m);
        e = t < att ? t / att : 1.0;
        b->x[i] += x * e * gain;
    }
}
static buf_t choir(double f, int n, const vowel_t *v)
{
    buf_t b = buf_new(1.0);
    int k;
    for (k = 0; k < n; k++)
        voice_add(&b, f * (1.0 + 0.005 * rndn()), v, v, 0, 0.007 + 0.004 * rnd01(), 0.05, 1.0 / n, 0.12, 0.0);
    room(&b, 0.35, 1.2);
    return b;
}
static buf_t solo_voice(double f)
{
    buf_t b = buf_new(1.0);
    voice_add(&b, f, &V_AH, &V_AH, 0, 0.012, 0.04, 1.0, 0.08, 0.02);
    room(&b, 0.2, 1.0);
    return b;
}
/* a shout: a breath, then the vowel gliding, the pitch falling */
static buf_t shout(double f, const vowel_t *a, const vowel_t *z, double len)
{
    buf_t b = buf_new(len + 0.05);
    int i;
    noise_add(&b, BQ_BP, 1800, 1.0, 0.002, 0.05, 0.5);
    {
        buf_t v = buf_new(len);
        voice_add(&v, f, a, z, len * 0.6, 0.0, 0.08, 1.0, 0.03, -0.12);
        for (i = 0; i < v.n; i++) {
            double t = (double)i / SR;
            v.x[i] *= exp(-2.5 * t / len) * (1.0 - exp(-t / 0.01));
        }
        buf_add(&b, &v, (int)(0.03 * SR), 1.0);
        buf_free(&v);
    }
    return b;
}
static buf_t kazoo(double f)
{
    buf_t b = buf_new(0.8);
    double ph = 0, vph = 0;
    int i;
    for (i = 0; i < b.n; i++) {        /* a buzzing membrane: a narrow pulse, through "oo" */
        double t = (double)i / SR, ff = f * (1.0 + 0.012 * sin(TAU * vph));
        vph += 5.0 / SR;
        ph += ff / SR;
        b.x[i] = (ph - floor(ph) < 0.12 ? 1.0 : -0.14) * (t < 0.03 ? t / 0.03 : 1.0);
    }
    bq_run(&b, BQ_PEAK, 350, 3, 10);
    bq_run(&b, BQ_PEAK, 900, 3, 8);
    bq_run(&b, BQ_PEAK, 2600, 4, 6);
    bq_run(&b, BQ_LP, 5000, 0.7, 0);
    return b;
}

/* ------------------------------------------------------------------- winds --- */
/* a flute's kind: its partials (the few it has), breath (a band around the note), a chiff at the start */
static buf_t wind(double f, const double *harm, int nh, double breath, double chiff, double vib, double dur)
{
    buf_t b = buf_new(dur);
    double ph = 0, vph = 0;
    int i, k;
    bq_t bn;
    memset(&bn, 0, sizeof bn);
    bq_set(&bn, BQ_BP, f * 2.0, 3.0, 0);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, ff = f * (1.0 + vib * sin(TAU * vph) * fmin(fmax(t - 0.15, 0.0) / 0.3, 1.0)), s = 0;
        vph += 5.0 / SR;
        ph += ff / SR;
        for (k = 0; k < nh; k++)
            s += harm[k] * sin(TAU * ph * (k + 1));
        s += bq(&bn, rndn()) * breath;
        b.x[i] = s * (t < 0.04 ? t / 0.04 : 1.0);
    }
    if (chiff > 0)
        noise_add(&b, BQ_BP, f * 4.0, 1.5, 0.002, 0.04, chiff);
    return b;
}
static buf_t flute(double f)
{
    static const double H[4] = {1.0, 0.22, 0.12, 0.04};
    return wind(f, H, 4, 0.12, 0.25, 0.004, 0.9);
}
static buf_t pan_flute(double f)
{
    static const double H[3] = {1.0, 0.08, 0.05};
    buf_t b = wind(f, H, 3, 0.35, 0.6, 0.002, 0.9);
    room(&b, 0.25, 0.8);
    return b;
}
static buf_t ocarina(double f)
{
    static const double H[3] = {1.0, 0.03, 0.06};
    return wind(f, H, 3, 0.05, 0.12, 0.006, 0.9);
}
static buf_t whistle(double f)
{
    static const double H[1] = {1.0};
    buf_t b = wind(f, H, 1, 0.03, 0.05, 0.009, 0.8);
    noise_add(&b, BQ_HP, 3000, 0.7, 0.01, 0.5, 0.02);
    return b;
}

/* ------------------------------------------------------------------- brass --- */
/* a horn: a saw whose brightness follows how hard it is blown (the bore's), the lips' scoop at the start */
static void horn_add(buf_t *b, double f, double bright, double att, double scoop, double gain, double det)
{
    double ph = rnd01(), lp1 = 0, lp2 = 0;
    int i;
    for (i = 0; i < b->n; i++) {
        double t = (double)i / SR, e = t < att ? t / att : 1.0 - 0.15 * fmin((t - att) / 0.3, 1.0);
        double ff = f * (1.0 + det) * (1.0 - scoop * exp(-t / 0.04)), fc = f * (1.5 + bright * 9.0 * e), c = op_c(fc);
        ph += ff / SR;
        lp1 += c * (bl_saw(ph, ff, 0) - lp1);
        lp2 += c * (lp1 - lp2);
        b->x[i] += lp2 * e * gain;
    }
}
static buf_t brass(double f, int n, double bright)
{
    buf_t b = buf_new(0.9);
    int k;
    for (k = 0; k < n; k++)
        horn_add(&b, f, bright, 0.05 + 0.02 * rnd01(), 0.03, 1.0 / n, 0.003 * rndn());
    bq_run(&b, BQ_PEAK, 1200, 1.2, 4);
    bq_run(&b, BQ_PEAK, 2500, 1.5, 3);
    if (n > 1)
        room(&b, 0.2, 1.0);
    return b;
}
static buf_t tuba(double f)
{
    buf_t b = buf_new(0.9);
    horn_add(&b, f, 0.35, 0.06, 0.05, 1.0, 0);
    bq_run(&b, BQ_PEAK, 250, 1.5, 4);
    return b;
}
/* the accordion: two reeds a little apart (the musette), a reedy spectrum, the bellows */
static buf_t accordion(double f)
{
    buf_t b = buf_new(0.9);
    int k, i;
    for (k = 0; k < 2; k++) {
        double ph = rnd01(), ff = f * (k ? 1.004 : 0.998);
        for (i = 0; i < b.n; i++) {
            double t = (double)i / SR;
            ph += ff / SR;
            b.x[i] += ((ph - floor(ph) < 0.22 ? 1.0 : -0.28) - 0.0) * 0.5 * (t < 0.03 ? t / 0.03 : 1.0) *
                      (1.0 + 0.04 * sin(TAU * 3.0 * t));
        }
    }
    dc_remove(&b);
    bq_run(&b, BQ_PEAK, 1000, 1.2, 6);
    bq_run(&b, BQ_LP, 4500, 0.7, 0);
    return b;
}
/* a pipe organ: its ranks (16, 8, 4, 2 2/3, 2, 1 3/5, 1), a breath of chiff as the pipes speak */
static buf_t organ(double f)
{
    static const double R[7] = {0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 8.0}, A[7] = {0.6, 1.0, 0.6, 0.35, 0.4, 0.2, 0.25};
    buf_t b = buf_new(1.0);
    int k, i;
    for (k = 0; k < 7; k++) {
        double ph = rnd01(), ff = f * R[k] * (1.0 + 0.0008 * rndn());
        if (ff > SR * 0.45)
            continue;
        for (i = 0; i < b.n; i++) {
            double t = (double)i / SR, s = sin(TAU * ph) + 0.12 * sin(TAU * 3.0 * ph);
            ph += ff / SR;
            b.x[i] += s * A[k] * 0.25 * (t < 0.06 ? t / 0.06 : 1.0);
        }
    }
    noise_add(&b, BQ_BP, f * 6.0, 2.0, 0.005, 0.06, 0.15);
    room(&b, 0.35, 1.4);
    return b;
}

/* ------------------------------------------------------------------- synths --- */
/* saws (n of them, spread), through a 4-pole low-pass with an envelope; a square instead with sq */
static buf_t va(double f, int n, double spread, int sq, double cut, double env, double edec, double res, double dur, double att)
{
    buf_t b = buf_new(dur);
    double s[4] = {0}, ph[8];
    int i, k;
    for (k = 0; k < n; k++)
        ph[k] = rnd01();
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, x = 0, fc, g, fb;
        for (k = 0; k < n; k++) {
            double ff = f * (1.0 + spread * (n > 1 ? (2.0 * k / (n - 1) - 1.0) : 0.0));
            ph[k] += ff / SR;
            x += sq ? bl_saw(ph[k], ff, 1) * 1.0 : bl_saw(ph[k], ff, 0);
        }
        x /= sqrt((double)n);
        fc = fmin(cut * (1.0 + env * exp(-t / edec)), SR * 0.4);
        g = op_c(fc);
        fb = res * 3.8;
        x = tanh(x - fb * s[3]);
        s[0] += g * (x - s[0]);
        s[1] += g * (s[0] - s[1]);
        s[2] += g * (s[1] - s[2]);
        s[3] += g * (s[2] - s[3]);
        b.x[i] = s[3] * (1.0 + fb * 0.5) * (t < att ? t / att : 1.0);
    }
    return b;
}
static buf_t sub_bass(double f)
{
    buf_t b = buf_new(0.6);
    double ph = 0;
    int i;
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR;
        ph += f / SR;
        b.x[i] = (sin(TAU * ph) + 0.12 * sin(2 * TAU * ph)) * (t < 0.005 ? t / 0.005 : 1.0);
    }
    return b;
}

/* the orchestra hit: brass and strings at the note and its octave, a timpani under, a cymbal over, all at
 * once and gone */
static buf_t orch_hit(double f)
{
    buf_t b = buf_new(0.7), x;
    int i;
    x = brass(f, 3, 0.9);
    buf_add(&b, &x, 0, 0.8);
    buf_free(&x);
    x = brass(f * 2, 2, 0.9);
    buf_add(&b, &x, 0, 0.5);
    buf_free(&x);
    x = strings(f, 0.01, 4);
    buf_add(&b, &x, 0, 0.6);
    buf_free(&x);
    x = strings(f * 2, 0.01, 4);
    buf_add(&b, &x, 0, 0.4);
    buf_free(&x);
    x = d_timpani(f / 2, 0.6);
    buf_add(&b, &x, 0, 0.7);
    buf_free(&x);
    x = d_cymbal(0.5, 0.7, 0);
    buf_add(&b, &x, 0, 0.25);
    buf_free(&x);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR;
        b.x[i] *= exp(-t / 0.18);
    }
    room(&b, 0.3, 1.3);
    return b;
}
