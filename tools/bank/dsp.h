/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's sample bank, the tools: buffers, noise, filters, envelopes, a resampler, a loop finder. Double
 * precision, no hurry: this runs once, on the computer, when the firmware is built. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000.0                     /* the rate every source is made at */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TAU (2.0 * M_PI)

typedef struct {
    double *x;
    int n;
} buf_t;

static buf_t buf_new(double secs)
{
    buf_t b;
    b.n = (int)(secs * SR + 0.5);
    if (b.n < 1)
        b.n = 1;
    b.x = (double *)calloc((size_t)b.n, sizeof(double));
    return b;
}
static void buf_free(buf_t *b)
{
    free(b->x);
    b->x = 0;
    b->n = 0;
}
static void buf_add(buf_t *d, const buf_t *s, int at, double g)
{
    int i;
    for (i = 0; i < s->n && at + i < d->n; i++)
        if (at + i >= 0)
            d->x[at + i] += s->x[i] * g;
}

/* noise: a xorshift, the same every build */
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static void rseed(uint64_t s) { rs = s ? s : 1; }
static double rnd01(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return (double)(rs >> 11) * (1.0 / 9007199254740992.0);
}
static double rndn(void) { return rnd01() * 2.0 - 1.0; }
static double rgauss(void)
{
    double u = rnd01() + 1e-12, v = rnd01();
    return sqrt(-2.0 * log(u)) * cos(TAU * v);
}

/* biquads (RBJ's cookbook) */
typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} bq_t;
enum { BQ_LP, BQ_HP, BQ_BP, BQ_PEAK, BQ_NOTCH, BQ_LSHELF, BQ_HSHELF };
static void bq_set(bq_t *f, int type, double fc, double q, double gain_db)
{
    double w = TAU * fc / SR, c = cos(w), s = sin(w), al = s / (2.0 * q), A = pow(10.0, gain_db / 40.0), a0;
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    if (fc >= SR * 0.49)
        fc = SR * 0.49;
    switch (type) {
    case BQ_LP: b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_HP: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_BP: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_NOTCH: b0 = 1; b1 = -2 * c; b2 = 1; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_PEAK: b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;
    case BQ_LSHELF: {
        double sq = 2 * sqrt(A) * al;
        b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
        a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq;
        break;
    }
    default: {
        double sq = 2 * sqrt(A) * al;
        b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
        a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq;
        break;
    }
    }
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}
static double bq(bq_t *f, double x)
{
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}
static void bq_run(buf_t *b, int type, double fc, double q, double g)
{
    bq_t f;
    int i;
    memset(&f, 0, sizeof f);
    bq_set(&f, type, fc, q, g);
    for (i = 0; i < b->n; i++)
        b->x[i] = bq(&f, b->x[i]);
}
/* a one-pole low-pass coefficient */
static double op_c(double fc) { return 1.0 - exp(-TAU * fc / SR); }

static double peak(const buf_t *b)
{
    double p = 0;
    int i;
    for (i = 0; i < b->n; i++)
        if (fabs(b->x[i]) > p)
            p = fabs(b->x[i]);
    return p;
}
static void normalize(buf_t *b, double to)
{
    double p = peak(b);
    int i;
    if (p <= 0)
        return;
    for (i = 0; i < b->n; i++)
        b->x[i] *= to / p;
}
static void dc_remove(buf_t *b)
{
    double z = 0, k = op_c(15.0);
    int i;
    for (i = 0; i < b->n; i++) {
        z += k * (b->x[i] - z);
        b->x[i] -= z;
    }
}
static void fade_out(buf_t *b, double secs)
{
    int n = (int)(secs * SR), i;
    if (n > b->n)
        n = b->n;
    for (i = 0; i < n; i++)
        b->x[b->n - 1 - i] *= (double)i / n;
}
/* cut the tail where it has gone below thr (relative to the peak) for good */
static void trim(buf_t *b, double thr)
{
    double p = peak(b) * thr;
    int i = b->n - 1;
    while (i > 0 && fabs(b->x[i]) < p)
        i--;
    b->n = i + 1 + (int)(0.004 * SR) < b->n ? i + 1 + (int)(0.004 * SR) : b->n;
}
static double softclip(double x) { return x > 1.5 ? 1.0 : x < -1.5 ? -1.0 : x - (4.0 / 27.0) * x * x * x; }
static double mtof(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }

/* a resampler: windowed sinc (Blackman), the band cut at 0.45 of the lower rate */
static double *resample(const double *x, int n, double from, double to, int *out_n)
{
    double r = to / from, fc = 0.45 * (r < 1.0 ? r : 1.0);
    int m = (int)((double)n * r) + 1, i, half = 24;
    double *y = (double *)calloc((size_t)m, sizeof(double));
    for (i = 0; i < m; i++) {
        double t = (double)i / r, s = 0, w = 0;
        int k, c = (int)floor(t);
        for (k = c - half + 1; k <= c + half; k++) {
            double d = t - (double)k, sx = d == 0.0 ? 2.0 * fc : sin(TAU * fc * d) / (M_PI * d);
            double win = 0.42 + 0.5 * cos(M_PI * d / half) + 0.08 * cos(2.0 * M_PI * d / half);
            if (fabs(d) >= half)
                continue;
            if (k >= 0 && k < n)
                s += x[k] * sx * win;
            w += sx * win;
        }
        y[i] = s / (w != 0.0 ? w : 1.0);   /* the kernel's sum: unity gain at DC */
    }
    *out_n = m;
    return y;
}

/* a loop: the best place, near [want0, end), whose start matches its end (the waveform and its slope), then a
 * crossfade over xf samples so the seam is smooth; returns the loop's start */
static int loop_find(const double *x, int n, int lo, int hi, int len_min, int *len_out)
{
    int best = lo, bestl = len_min, s, l;
    double bscore = 1e30;
    for (s = lo; s < hi; s++)
        for (l = len_min; s + l < n && l < len_min * 3; l++) {
            int e = s + l, k;
            double sc = 0;
            for (k = -6; k <= 6; k++)
                if (s + k >= 0 && e + k < n)
                    sc += (x[s + k] - x[e + k]) * (x[s + k] - x[e + k]);
            if (sc < bscore) {
                bscore = sc;
                best = s;
                bestl = l;
            }
        }
    *len_out = bestl;
    return best;
}
static void loop_xfade(double *x, int ls, int le, int xf)
{
    int i;
    if (xf > ls)
        xf = ls;
    for (i = 0; i < xf; i++) {
        double a = (double)i / xf;
        x[le - xf + i] = x[le - xf + i] * (1.0 - a) + x[ls - xf + i] * a;
    }
}
