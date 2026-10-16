/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's sample bank: the drums, synthesized. Analog machines (808, 909) the way their circuits work, the
 * acoustic ones by their modes (a membrane's, a bar's, a plate's) and their noise. The consoles' kits are
 * these, made smaller and coarser by each console's own format. */
#pragma once
#include "dsp.h"

/* damped modes: frequencies f0 x ratio, each its own amplitude and decay (s, to -60 dB) */
static void modes_add(buf_t *b, double f0, const double *ratio, const double *amp, const double *dec, int n,
                      double glide, double gtime)
{
    int k, i;
    for (k = 0; k < n; k++) {
        double ph = rnd01() * 0.2, a = amp[k], d = 6.9 / (dec[k] * SR);
        for (i = 0; i < b->n; i++) {
            double t = (double)i / SR, f = f0 * ratio[k] * (1.0 + glide * exp(-t / gtime));
            if (f >= SR * 0.45)
                break;
            ph += f / SR;
            b->x[i] += a * sin(TAU * ph) * exp(-d * i);
        }
    }
}
/* noise through a band, under an envelope (attack a, decay d to -60 dB) */
static void noise_add(buf_t *b, int type, double fc, double q, double a, double d, double g)
{
    bq_t f;
    int i;
    memset(&f, 0, sizeof f);
    bq_set(&f, type, fc, q, 0);
    for (i = 0; i < b->n; i++) {
        double t = (double)i / SR, e = (t < a ? t / a : exp(-6.9 * (t - a) / d));
        b->x[i] += bq(&f, rndn()) * e * g;
    }
}

/* the kick: a sine falling from f0 to f1 (time constant tp), its body decaying, a click; drive saturates */
static buf_t d_kick(double f0, double f1, double tp, double dec, double click, double drive)
{
    buf_t b = buf_new(dec * 1.2 + 0.05);
    double ph = 0;
    int i;
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, f = f1 + (f0 - f1) * exp(-t / tp), e = exp(-6.9 * t / dec);
        ph += f / SR;
        b.x[i] = sin(TAU * ph) * e * (t < 0.0015 ? t / 0.0015 : 1.0);
    }
    if (click > 0)
        noise_add(&b, BQ_HP, 1500, 0.7, 0.0003, 0.008, click);
    if (drive > 0)
        for (i = 0; i < b.n; i++)
            b.x[i] = tanh(b.x[i] * (1.0 + drive * 3.0)) / tanh(1.0 + drive * 3.0);
    return b;
}
/* an acoustic kick: the beater, the batter head's modes, the shell's thump */
static buf_t d_kick_ac(double f0, double dec, double beater)
{
    static const double R[4] = {1.0, 1.59, 2.14, 2.65}, A[4] = {1.0, 0.35, 0.18, 0.1}, D[4] = {1.0, 0.5, 0.3, 0.2};
    double dd[4];
    int k;
    buf_t b = buf_new(dec + 0.05);
    for (k = 0; k < 4; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f0, R, A, dd, 4, 0.5, 0.012);
    noise_add(&b, BQ_BP, 2500, 0.8, 0.0002, 0.01, beater);
    noise_add(&b, BQ_LP, 300, 0.7, 0.001, 0.05, 0.4);
    bq_run(&b, BQ_PEAK, 3000, 1.2, 4);
    return b;
}
/* the snare: two body modes, noise (the snares) through a band */
static buf_t d_snare(double f1, double f2, double bdec, double ndec, double snappy, double nfc)
{
    buf_t b = buf_new(ndec + 0.05);
    double r[2] = {1.0, f2 / f1}, a[2] = {0.8, 0.45}, d[2] = {bdec, bdec * 0.7};
    modes_add(&b, f1, r, a, d, 2, 0.15, 0.01);
    noise_add(&b, BQ_HP, nfc, 0.7, 0.0005, ndec, snappy);
    noise_add(&b, BQ_BP, nfc * 1.8, 1.0, 0.0005, ndec * 0.6, snappy * 0.5);
    return b;
}
/* an acoustic snare: the head, the wires' buzz (noise, low-passed, decaying more slowly), the stick */
static buf_t d_snare_ac(double f, double dec, double wires)
{
    static const double R[5] = {1.0, 1.59, 2.14, 2.3, 2.65}, A[5] = {0.7, 0.4, 0.3, 0.2, 0.15}, D[5] = {0.18, 0.12, 0.1, 0.08, 0.07};
    double dd[5];
    int k;
    buf_t b = buf_new(dec + 0.08);
    for (k = 0; k < 5; k++)
        dd[k] = D[k] * dec / 0.25;
    modes_add(&b, f, R, A, dd, 5, 0.08, 0.008);
    noise_add(&b, BQ_BP, 4200, 0.6, 0.0008, dec, wires);
    noise_add(&b, BQ_HP, 1200, 0.7, 0.0004, dec * 0.5, wires * 0.6);
    noise_add(&b, BQ_BP, 1800, 1.5, 0.0001, 0.006, 0.6);
    return b;
}
/* a tom: a membrane's modes, the pitch settling as the head relaxes, the stick */
static buf_t d_tom(double f, double dec, double glide, double stick)
{
    static const double R[4] = {1.0, 1.5, 1.98, 2.44}, A[4] = {1.0, 0.3, 0.15, 0.08}, D[4] = {1.0, 0.45, 0.3, 0.2};
    double dd[4];
    int k;
    buf_t b = buf_new(dec + 0.05);
    for (k = 0; k < 4; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f, R, A, dd, 4, glide, 0.04);
    noise_add(&b, BQ_BP, 3000, 1.0, 0.0002, 0.012, stick);
    return b;
}
/* the 808's metal: six square oscillators at its frequencies, through high band-passes */
static void metal_add(buf_t *b, double mul, double fc, double dec, double g)
{
    static const double F[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    double ph[6] = {0};
    bq_t f1, f2;
    int i, k;
    memset(&f1, 0, sizeof f1);
    memset(&f2, 0, sizeof f2);
    bq_set(&f1, BQ_BP, fc, 1.2, 0);
    bq_set(&f2, BQ_HP, fc * 0.8, 0.7, 0);
    for (i = 0; i < b->n; i++) {
        double s = 0, t = (double)i / SR;
        for (k = 0; k < 6; k++) {
            ph[k] += F[k] * mul / SR;
            s += (ph[k] - floor(ph[k]) < 0.5) ? 1.0 : -1.0;
        }
        b->x[i] += bq(&f2, bq(&f1, s / 6.0)) * exp(-6.9 * t / dec) * g;
    }
}
static buf_t d_hat(double dec, double fc, double noise)
{
    buf_t b = buf_new(dec + 0.03);
    metal_add(&b, 1.0, fc, dec, 1.0 - noise);
    noise_add(&b, BQ_HP, fc, 0.7, 0.0003, dec, noise * 0.8);
    return b;
}
/* a cymbal: many inharmonic partials (a plate's), a wash of noise; a ride has its bell */
static buf_t d_cymbal(double dec, double bright, int ride)
{
    buf_t b = buf_new(dec + 0.1);
    int k, i;
    for (k = 0; k < 48; k++) {
        double f = 320.0 * pow(30.0, rnd01()) * (0.6 + 0.6 * bright), a = 0.05 + 0.05 * rnd01(), d = dec * (0.4 + 0.6 * rnd01());
        double ph = rnd01(), dk = 6.9 / (d * SR);
        if (f > SR * 0.45)
            continue;
        for (i = 0; i < b.n; i++) {
            ph += f / SR;
            b.x[i] += a * sin(TAU * ph) * exp(-dk * i);
        }
    }
    noise_add(&b, BQ_HP, 5000 + 3000 * bright, 0.7, 0.001, dec * 0.8, ride ? 0.2 : 0.6);
    if (ride) {
        static const double R[3] = {1.0, 2.41, 3.7}, A[3] = {0.5, 0.3, 0.2}, D[3] = {1.5, 1.0, 0.7};
        modes_add(&b, 760, R, A, D, 3, 0, 1);
    }
    bq_run(&b, BQ_HP, 400, 0.7, 0);
    return b;
}
/* the clap: bursts of noise a few ms apart, then its tail, through a band */
static buf_t d_clap(double dec, int bursts)
{
    buf_t b = buf_new(dec + 0.06);
    bq_t f;
    int i, k;
    memset(&f, 0, sizeof f);
    bq_set(&f, BQ_BP, 1150, 1.4, 0);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, e = 0;
        for (k = 0; k < bursts; k++) {
            double t0 = k * 0.0105;
            if (t >= t0)
                e = fmax(e, exp(-(t - t0) / 0.0035) * (k < bursts - 1 ? 1.0 : 0.0));
        }
        if (t >= (bursts - 1) * 0.0105)
            e = fmax(e, exp(-6.9 * (t - (bursts - 1) * 0.0105) / dec));
        b.x[i] = bq(&f, rndn()) * e;
    }
    return b;
}
static buf_t d_rim(double f)
{
    buf_t b = buf_new(0.07);
    double r[2] = {1.0, 455.0 / f * 0 + 0.27}, a[2] = {1.0, 0.6}, d[2] = {0.03, 0.045};
    modes_add(&b, f, r, a, d, 2, 0, 1);
    noise_add(&b, BQ_BP, 3500, 1.0, 0.0001, 0.004, 0.5);
    return b;
}
static buf_t d_cowbell(double f1, double f2, double dec)
{
    buf_t b = buf_new(dec + 0.03);
    double p1 = 0, p2 = 0;
    bq_t f;
    int i;
    memset(&f, 0, sizeof f);
    bq_set(&f, BQ_BP, (f1 + f2) * 1.1, 1.8, 0);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, s;
        p1 += f1 / SR;
        p2 += f2 / SR;
        s = ((p1 - floor(p1)) < 0.5 ? 1.0 : -1.0) + ((p2 - floor(p2)) < 0.5 ? 1.0 : -1.0);
        b.x[i] = bq(&f, s) * (t < 0.02 ? exp(-t / 0.012) * 0.6 + 0.4 : 0.4 * exp(-6.9 * (t - 0.02) / dec));
    }
    return b;
}
/* the timpani: a kettle's membrane, its modes near harmonic (the air in the bowl tunes them), the mallet */
static buf_t d_timpani(double f, double dec)
{
    static const double R[5] = {1.0, 1.5, 1.99, 2.44, 2.89}, A[5] = {1.0, 0.5, 0.25, 0.15, 0.08}, D[5] = {1.0, 0.75, 0.5, 0.4, 0.3};
    double dd[5];
    int k;
    buf_t b = buf_new(dec + 0.1);
    for (k = 0; k < 5; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f, R, A, dd, 5, 0.02, 0.05);
    noise_add(&b, BQ_LP, 600, 0.7, 0.001, 0.08, 0.5);
    return b;
}
/* hand drums: a conga or a bongo (a slap: brighter, shorter); a log drum (a slit of wood) */
static buf_t d_conga(double f, double dec, double slap)
{
    static const double R[4] = {1.0, 1.52, 2.1, 2.9}, A[4] = {1.0, 0.4, 0.2, 0.1}, D[4] = {1.0, 0.5, 0.3, 0.2};
    double dd[4], aa[4];
    int k;
    buf_t b = buf_new(dec + 0.05);
    for (k = 0; k < 4; k++) {
        dd[k] = D[k] * dec * (1.0 - 0.5 * slap);
        aa[k] = A[k] * (k ? 1.0 + 2.0 * slap : 1.0);
    }
    modes_add(&b, f, R, aa, dd, 4, 0.04, 0.02);
    noise_add(&b, BQ_BP, 2200 + 2000 * slap, 1.0, 0.0002, 0.015 + 0.02 * slap, 0.3 + slap);
    return b;
}
static buf_t d_log(double f, double dec)
{
    static const double R[3] = {1.0, 2.76, 5.4}, A[3] = {1.0, 0.35, 0.12}, D[3] = {1.0, 0.4, 0.2};
    double dd[3];
    int k;
    buf_t b = buf_new(dec + 0.03);
    for (k = 0; k < 3; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f, R, A, dd, 3, 0, 1);
    noise_add(&b, BQ_BP, 1500, 1.5, 0.0002, 0.006, 0.4);
    return b;
}
static buf_t d_claves(double f, double dec)
{
    double r[2] = {1.0, 2.8}, a[2] = {1.0, 0.2}, d[2] = {dec, dec * 0.4};
    buf_t b = buf_new(dec + 0.02);
    modes_add(&b, f, r, a, d, 2, 0, 1);
    return b;
}
static buf_t d_shaker(double dec, double len)
{
    buf_t b = buf_new(len + dec);
    int i;
    bq_t f;
    memset(&f, 0, sizeof f);
    bq_set(&f, BQ_BP, 6500, 1.0, 0);
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, e = t < len ? sin(M_PI * t / len) : exp(-6.9 * (t - len) / dec) * 0.0;
        b.x[i] = bq(&f, rndn()) * e * (0.6 + 0.4 * (rnd01() < 0.3));
    }
    return b;
}
static buf_t d_tamb(double dec)
{
    buf_t b = buf_new(dec + 0.05);
    int k, i;
    for (k = 0; k < 10; k++) {                       /* the jingles: little cymbals, a few ms apart */
        double f = 6000 + 4000 * rnd01(), at = rnd01() * 0.012, ph = 0, d = dec * (0.5 + 0.5 * rnd01());
        for (i = (int)(at * SR); i < b.n; i++) {
            double t = (double)i / SR - at;
            ph += f / SR;
            b.x[i] += 0.12 * sin(TAU * ph) * exp(-6.9 * t / d);
        }
    }
    noise_add(&b, BQ_HP, 7000, 0.7, 0.0005, dec * 0.6, 0.5);
    noise_add(&b, BQ_BP, 400, 1.0, 0.0001, 0.01, 0.3);
    return b;
}
static buf_t d_triangle(double f, double dec)
{
    static const double R[4] = {1.0, 2.76, 5.4, 8.9}, A[4] = {0.5, 0.4, 0.3, 0.2}, D[4] = {1.0, 0.9, 0.8, 0.6};
    double dd[4];
    int k;
    buf_t b = buf_new(dec + 0.05);
    for (k = 0; k < 4; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f, R, A, dd, 4, 0, 1);
    return b;
}
/* the concert bass drum: big, low, long */
static buf_t d_concert_bd(double f, double dec)
{
    static const double R[4] = {1.0, 1.43, 1.85, 2.3}, A[4] = {1.0, 0.6, 0.3, 0.2}, D[4] = {1.0, 0.7, 0.5, 0.4};
    double dd[4];
    int k;
    buf_t b = buf_new(dec + 0.1);
    for (k = 0; k < 4; k++)
        dd[k] = D[k] * dec;
    modes_add(&b, f, R, A, dd, 4, 0.05, 0.08);
    noise_add(&b, BQ_LP, 250, 0.7, 0.003, 0.15, 0.6);
    return b;
}
/* a snare roll: hits a 32nd apart, louder as it goes */
static buf_t d_roll(double secs)
{
    buf_t b = buf_new(secs + 0.2), h;
    int k, n = (int)(secs / 0.045);
    for (k = 0; k < n; k++) {
        h = d_snare_ac(200 + 10 * rndn(), 0.16, 0.9);
        buf_add(&b, &h, (int)(k * 0.045 * SR + rndn() * 0.002 * SR), 0.35 + 0.65 * k / n);
        buf_free(&h);
    }
    return b;
}
/* a burst of noise through a low-pass: a 'chip' crash or explosion's source */
static buf_t d_burst(double dec, double fc)
{
    buf_t b = buf_new(dec + 0.02);
    noise_add(&b, BQ_LP, fc, 0.7, 0.0005, dec, 1.0);
    return b;
}

/* a little room: early reflections and a short, dark tail (for the break and the 'studio' kits) */
static void room(buf_t *b, double wet, double size)
{
    static const int TAP[8] = {431, 617, 797, 1009, 1213, 1427, 1621, 1847};
    int len = b->n, i, k;
    double *y = (double *)calloc((size_t)len, sizeof(double)), lp = 0;
    double comb[4][2400] = {{0}};
    int cl[4] = {1116, 1356, 1557, 1781}, cw[4] = {0};
    for (i = 0; i < len; i++) {
        double s = 0, in = b->x[i];
        for (k = 0; k < 8; k++) {
            int d = (int)(TAP[k] * size);
            if (i >= d)
                s += b->x[i - d] * (0.5 - k * 0.05);
        }
        for (k = 0; k < 4; k++) {
            int l = (int)(cl[k] * size);
            double o = comb[k][cw[k]];
            if (l > 2399)
                l = 2399;
            lp += 0.3 * (o - lp);
            comb[k][cw[k]] = in + lp * 0.55;
            cw[k] = (cw[k] + 1) % l;
            s += o * 0.25;
        }
        y[i] = s;
    }
    for (i = 0; i < len; i++)
        b->x[i] += y[i] * wet;
    free(y);
}
