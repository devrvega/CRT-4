/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's sample bank: the consoles' formats. Each encoder searches, block by block, for what the console's
 * own decoder will turn back into the closest sound (the decoders here are the firmware's, bit for bit):
 *   PCM8    8-bit unsigned (the Genesis's DAC)
 *   PCM4    4-bit, two a byte (a C64's digi, a Game Boy's wave kit)
 *   PCM5    5-bit, a byte each (the PC Engine's DDA)
 *   DMC1    1-bit delta (the NES): each bit moves a 7-bit counter two up or two down
 *   DPCM4   4-bit delta from a table, two a byte (a Genesis sound driver's way to halve its drums)
 *   BRR     the SNES's: 9-byte blocks of 16 samples, a shift and one of four predictors
 *   SPU     the PlayStation's: 16-byte blocks of 28 samples, a shift and one of five predictors
 *   VADPCM  the N64's kind: 9-byte frames of 16, a scale and an order-2 predictor from the sample's own book
 *   OKI     the PC Engine CD's MSM5205: 4-bit ADPCM, an adaptive step */
#pragma once
#include "dsp.h"

typedef struct {
    uint8_t *d;
    int n, cap;
} bytes_t;
static void put8(bytes_t *b, int v)
{
    if (b->n >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        b->d = (uint8_t *)realloc(b->d, (size_t)b->cap);
    }
    b->d[b->n++] = (uint8_t)v;
}
static int clamp16(int x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
static int q16(double x) { return clamp16((int)lrint(x * 32767.0)); }

static void enc_pcm8(bytes_t *o, const double *x, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int v = (int)lrint(x[i] * 127.0 + 128.0);
        put8(o, v < 0 ? 0 : v > 255 ? 255 : v);
    }
}
static void enc_pcm4(bytes_t *o, const double *x, int n)
{
    int i;
    for (i = 0; i < n; i += 2) {
        int a = (int)lrint(x[i] * 7.5 + 7.5), b = i + 1 < n ? (int)lrint(x[i + 1] * 7.5 + 7.5) : 8;
        a = a < 0 ? 0 : a > 15 ? 15 : a;
        b = b < 0 ? 0 : b > 15 ? 15 : b;
        put8(o, a | b << 4);
    }
}
static void enc_pcm5(bytes_t *o, const double *x, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int v = (int)lrint(x[i] * 15.5 + 15.5);
        put8(o, v < 0 ? 0 : v > 31 ? 31 : v);
    }
}
/* the DMC: the counter (0..127) chases the signal two at a time; start: where the counter is set first */
static void enc_dmc1(bytes_t *o, const double *x, int n, int *start)
{
    int i, c = (int)lrint(64.0 + x[0] * 60.0), byte = 0;
    c = c < 0 ? 0 : c > 127 ? 127 : c;
    *start = c;
    for (i = 0; i < n; i++) {
        double tgt = 64.0 + x[i] * 60.0;
        int bit = (double)c < tgt;
        if (bit) {
            if (c <= 125)
                c += 2;
        } else if (c >= 2) {
            c -= 2;
        }
        byte |= bit << (i & 7);
        if ((i & 7) == 7) {
            put8(o, byte);
            byte = 0;
        }
    }
    if (n & 7)
        put8(o, byte);
}
/* 4-bit deltas from a table (the Sonic games' driver's), two a byte, high nibble first */
static const int DPCM_T[16] = {0, 1, 2, 4, 8, 16, 32, 64, -128, -1, -2, -4, -8, -16, -32, -64};
static void enc_dpcm4(bytes_t *o, const double *x, int n)
{
    int i, acc = 128, hi = 0, k;
    for (i = 0; i < n; i++) {
        int tgt = (int)lrint(x[i] * 127.0 + 128.0), best = 0, be = 1 << 30;
        for (k = 0; k < 16; k++) {
            int v = (acc + DPCM_T[k]) & 255, e = abs(v - tgt);
            if (e < be) {
                be = e;
                best = k;
            }
        }
        acc = (acc + DPCM_T[best]) & 255;
        if (i & 1)
            put8(o, hi << 4 | best);
        else
            hi = best;
    }
    if (n & 1)
        put8(o, hi << 4);
}

/* ------------------------------------------------------------------- BRR --- */
/* one sample of the S-DSP's decoder: the nibble, the shift, the filter, its last two (as it keeps them) */
static int brr_one(int nib, int shift, int filter, int p1, int p2)
{
    int s = nib >= 8 ? nib - 16 : nib;
    if (shift <= 12) {
        s = (s * (1 << shift)) >> 1;
    } else {
        s = s < 0 ? -2048 : 0;
    }
    p2 >>= 1;
    switch (filter) {
    case 1: s += p1 >> 1; s += (-p1) >> 5; break;
    case 2: s += p1; s -= p2; s += p2 >> 4; s += (p1 * -3) >> 6; break;
    case 3: s += p1; s -= p2; s += (p1 * -13) >> 7; s += (p2 * 3) >> 4; break;
    default: break;
    }
    s = clamp16(s);
    s = (int16_t)(s * 2);
    return s;
}
/* x: the samples (n a multiple of 16); loop: the loop's first block (-1 none). Each block: every filter and
 * shift tried, the decoder run, the least squared error kept. The loop's first block uses filter 0, so the
 * decoder starts clean wherever it comes from. */
static void enc_brr(bytes_t *o, const double *x, int n, int loop)
{
    int nb = (n + 15) / 16, b, p1 = 0, p2 = 0;
    for (b = 0; b < nb; b++) {
        int best_f = 0, best_s = 0, f, sh, i, bn[16], bp1 = 0, bp2 = 0;
        double be = 1e300;
        for (f = 0; f < 4; f++) {
            if (b == 0 || (loop >= 0 && b == loop))
                if (f)
                    continue;
            for (sh = 0; sh <= 12; sh++) {
                int q1 = p1, q2 = p2, nn[16];
                double e = 0;
                for (i = 0; i < 16; i++) {
                    int idx = b * 16 + i, tgt = idx < n ? q16(x[idx]) : 0, k, bestk = 0, bv = 0;
                    double bk = 1e300;
                    for (k = -8; k < 8; k++) {   /* the nibble that lands nearest */
                        int v = brr_one(k & 15, sh, f, q1, q2);
                        double d = (double)v - (double)tgt;
                        if (d * d < bk) {
                            bk = d * d;
                            bestk = k;
                            bv = v;
                        }
                    }
                    nn[i] = bestk & 15;
                    e += bk;
                    q2 = q1;
                    q1 = bv;
                    if (e >= be)
                        break;
                }
                if (e < be) {
                    be = e;
                    best_f = f;
                    best_s = sh;
                    memcpy(bn, nn, sizeof bn);
                    bp1 = q1;
                    bp2 = q2;
                }
            }
        }
        put8(o, best_s << 4 | best_f << 2 | (loop >= 0 ? 2 : 0) | (b == nb - 1 ? 1 : 0));
        for (i = 0; i < 16; i += 2)
            put8(o, bn[i] << 4 | bn[i + 1]);
        p1 = bp1;
        p2 = bp2;
    }
}

/* -------------------------------------------------------- PlayStation SPU --- */
static const int SPU_POS[5] = {0, 60, 115, 98, 122}, SPU_NEG[5] = {0, 0, -52, -55, -60};
static int spu_one(int nib, int shift, int filter, int p1, int p2)
{
    int s = (int16_t)(nib << 12) >> shift;
    s += (p1 * SPU_POS[filter] + p2 * SPU_NEG[filter] + 32) >> 6;
    return clamp16(s);
}
/* 28 a block; flags: 1 end, 2 repeat (go back to the loop), 4 the loop starts here */
static void enc_spu(bytes_t *o, const double *x, int n, int loop)
{
    int nb = (n + 27) / 28, b, p1 = 0, p2 = 0;
    for (b = 0; b < nb; b++) {
        int best_f = 0, best_s = 0, f, sh, i, bn[28], bp1 = 0, bp2 = 0, fl;
        double be = 1e300;
        for (f = 0; f < 5; f++) {
            if (loop >= 0 && b == loop && f)
                continue;
            for (sh = 0; sh <= 12; sh++) {
                int q1 = p1, q2 = p2, nn[28];
                double e = 0;
                for (i = 0; i < 28; i++) {
                    int idx = b * 28 + i, tgt = idx < n ? q16(x[idx]) : 0, k, bestk = 0, bv = 0;
                    double bk = 1e300;
                    for (k = -8; k < 8; k++) {
                        int v = spu_one(k & 15, sh, f, q1, q2);
                        double d = (double)(v - tgt);
                        if (d * d < bk) {
                            bk = d * d;
                            bestk = k;
                            bv = v;
                        }
                    }
                    nn[i] = bestk & 15;
                    e += bk;
                    q2 = q1;
                    q1 = bv;
                    if (e >= be)
                        break;
                }
                if (e < be) {
                    be = e;
                    best_f = f;
                    best_s = sh;
                    memcpy(bn, nn, sizeof bn);
                    bp1 = q1;
                    bp2 = q2;
                }
            }
        }
        fl = (b == nb - 1 ? 1 : 0) | (b == nb - 1 && loop >= 0 ? 2 : 0) | (loop >= 0 && b == loop ? 4 : 0);
        put8(o, best_f << 4 | best_s);
        put8(o, fl);
        for (i = 0; i < 28; i += 2)
            put8(o, bn[i] | bn[i + 1] << 4);
        p1 = bp1;
        p2 = bp2;
    }
}

/* ----------------------------------------------------------- the N64's kind --- */
/* the book: four order-2 predictors (Q11), fitted to the sample: each frame's best (least squares), then
 * clustered into four. A frame: a byte (scale << 4 | predictor), sixteen 4-bit residuals. */
static int vad_one(int r, int scale, const int *c, int p1, int p2)
{
    int s = (r >= 8 ? r - 16 : r) * (1 << scale);
    s += (c[0] * p1 + c[1] * p2 + 1024) >> 11;
    return clamp16(s);
}
static void vad_book(const double *x, int n, int book[4][2])
{
    int nf = n / 16, f, k, it;
    double (*pc)[2] = (double (*)[2])calloc((size_t)(nf + 1), sizeof(double[2]));
    double cen[4][2] = {{0, 0}, {1.0, 0}, {1.8, -0.85}, {1.95, -0.97}};
    for (f = 0; f < nf; f++) {         /* the frame's own predictor */
        double r11 = 1e-9, r12 = 0, r22 = 1e-9, r01 = 0, r02 = 0, det;
        int i;
        for (i = f * 16; i < f * 16 + 16; i++) {
            double x1 = i >= 1 ? x[i - 1] : 0, x2 = i >= 2 ? x[i - 2] : 0;
            r11 += x1 * x1;
            r12 += x1 * x2;
            r22 += x2 * x2;
            r01 += x[i] * x1;
            r02 += x[i] * x2;
        }
        det = r11 * r22 - r12 * r12;
        if (fabs(det) < 1e-12) {
            pc[f][0] = 0;
            pc[f][1] = 0;
        } else {
            pc[f][0] = (r01 * r22 - r02 * r12) / det;
            pc[f][1] = (r02 * r11 - r01 * r12) / det;
        }
        if (pc[f][0] > 1.99)
            pc[f][0] = 1.99;
        if (pc[f][0] < -1.99)
            pc[f][0] = -1.99;
        if (pc[f][1] > 0.99)
            pc[f][1] = 0.99;
        if (pc[f][1] < -0.99)
            pc[f][1] = -0.99;
    }
    for (it = 0; it < 12; it++) {      /* four centres (k-means) */
        double s[4][3] = {{0}};
        for (f = 0; f < nf; f++) {
            int bk = 0;
            double bd = 1e300;
            for (k = 0; k < 4; k++) {
                double d = (pc[f][0] - cen[k][0]) * (pc[f][0] - cen[k][0]) + (pc[f][1] - cen[k][1]) * (pc[f][1] - cen[k][1]);
                if (d < bd) {
                    bd = d;
                    bk = k;
                }
            }
            s[bk][0] += pc[f][0];
            s[bk][1] += pc[f][1];
            s[bk][2] += 1;
        }
        for (k = 1; k < 4; k++)
            if (s[k][2] > 0) {
                cen[k][0] = s[k][0] / s[k][2];
                cen[k][1] = s[k][1] / s[k][2];
            }
    }
    for (k = 0; k < 4; k++) {
        book[k][0] = (int)lrint(cen[k][0] * 2048.0);
        book[k][1] = (int)lrint(cen[k][1] * 2048.0);
    }
    book[0][0] = book[0][1] = 0;       /* the first is none: a loop's start, a frame from silence */
    free(pc);
}
static void enc_vad(bytes_t *o, const double *x, int n, int loop, int book[4][2])
{
    int nf = (n + 15) / 16, f, p1 = 0, p2 = 0;
    for (f = 0; f < nf; f++) {
        int bp = 0, bs = 0, pr, sc, i, bn[16], bp1 = 0, bp2 = 0;
        double be = 1e300;
        for (pr = 0; pr < 4; pr++) {
            if ((f == 0 || (loop >= 0 && f == loop)) && pr)
                continue;
            for (sc = 0; sc <= 12; sc++) {
                int q1 = p1, q2 = p2, nn[16];
                double e = 0;
                for (i = 0; i < 16; i++) {
                    int idx = f * 16 + i, tgt = idx < n ? q16(x[idx]) : 0, k, bk_ = 0, bv = 0;
                    double bk = 1e300;
                    for (k = -8; k < 8; k++) {
                        int v = vad_one(k & 15, sc, book[pr], q1, q2);
                        double d = (double)(v - tgt);
                        if (d * d < bk) {
                            bk = d * d;
                            bk_ = k;
                            bv = v;
                        }
                    }
                    nn[i] = bk_ & 15;
                    e += bk;
                    q2 = q1;
                    q1 = bv;
                    if (e >= be)
                        break;
                }
                if (e < be) {
                    be = e;
                    bp = pr;
                    bs = sc;
                    memcpy(bn, nn, sizeof bn);
                    bp1 = q1;
                    bp2 = q2;
                }
            }
        }
        put8(o, bs << 4 | bp);
        for (i = 0; i < 16; i += 2)
            put8(o, bn[i] << 4 | bn[i + 1]);
        p1 = bp1;
        p2 = bp2;
    }
}

/* -------------------------------------------------------------- OKI ADPCM --- */
static const int OKI_STEP[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
                                 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
                                 796, 876, 963, 1060, 1166, 1282, 1411, 1552};
static const int OKI_ADJ[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int oki_one(int nib, int *sig, int *idx)
{
    int st = OKI_STEP[*idx], d = st >> 3;
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
    *idx += OKI_ADJ[nib & 7];
    *idx = *idx < 0 ? 0 : *idx > 48 ? 48 : *idx;
    return *sig;
}
static void enc_oki(bytes_t *o, const double *x, int n)
{
    int i, sig = 0, idx = 0, hi = 0;
    for (i = 0; i < n; i++) {
        int tgt = (int)lrint(x[i] * 2047.0), k, best = 0, bs = 0, bi = 0;
        double be = 1e300;
        for (k = 0; k < 16; k++) {
            int s = sig, ix = idx, v = oki_one(k, &s, &ix);
            double e = (double)(v - tgt) * (double)(v - tgt);
            if (e < be) {
                be = e;
                best = k;
                bs = s;
                bi = ix;
            }
        }
        sig = bs;
        idx = bi;
        if (i & 1)
            put8(o, hi << 4 | best);
        else
            hi = best;
    }
    if (n & 1)
        put8(o, hi << 4);
}
