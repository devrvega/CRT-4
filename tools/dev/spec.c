/* SPDX-License-Identifier: GPL-3.0-only */
/* A spectrogram of a WAV (16-bit stereo or mono, 44.1 kHz) as a PPM: time across, log frequency up
 * (30 Hz .. 20 kHz), dB as brightness. For looking at the sounds when they can't be heard.
 *   spec IN.wav OUT.ppm [width] [height] [start_s] [len_s] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 4096
static void fft(double *re, double *im, int n)
{
    int i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2.0 * M_PI / m;
        for (k = 0; k < n; k += m)
            for (j = 0; j < m / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j), xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi;
                double xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
                re[k + j + m / 2] = re[k + j] - xr; im[k + j + m / 2] = im[k + j] - xi;
                re[k + j] += xr; im[k + j] += xi;
            }
    }
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int W = argc > 3 ? atoi(argv[3]) : 900, H = argc > 4 ? atoi(argv[4]) : 360, ch, n, x, y, i;
    double t0 = argc > 5 ? atof(argv[5]) : 0, tl = argc > 6 ? atof(argv[6]) : 0;
    unsigned char hdr[44];
    short *d;
    double *mono, re[N], im[N];
    unsigned char *img;
    long sz;
    if (!f) return 1;
    fread(hdr, 1, 44, f);
    ch = hdr[22];
    fseek(f, 0, SEEK_END); sz = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    d = malloc(sz); fread(d, 1, sz, f); fclose(f);
    n = (int)(sz / 2 / ch);
    mono = malloc(sizeof(double) * n);
    for (i = 0; i < n; i++) mono[i] = ch == 2 ? (d[2 * i] + d[2 * i + 1]) / 65536.0 : d[i] / 32768.0;
    {
        int s0 = (int)(t0 * 44100), s1 = tl > 0 ? s0 + (int)(tl * 44100) : n;
        if (s1 > n) s1 = n;
        mono += s0; n = s1 - s0;
    }
    img = calloc(W * H * 3, 1);
    for (x = 0; x < W; x++) {
        int c = (int)((double)x / W * (n - N));
        for (i = 0; i < N; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
            re[i] = c + i < n && c + i >= 0 ? mono[c + i] * w : 0; im[i] = 0;
        }
        fft(re, im, N);
        for (y = 0; y < H; y++) {
            double fr = 30.0 * pow(20000.0 / 30.0, (double)y / (H - 1)), bin = fr / 44100.0 * N, m = 0, db;
            int b0 = (int)bin, b1 = (int)(30.0 * pow(20000.0 / 30.0, (double)(y + 1) / (H - 1)) / 44100.0 * N), b;
            if (b1 < b0) b1 = b0;
            for (b = b0; b <= b1 && b < N / 2; b++) { double mm = re[b] * re[b] + im[b] * im[b]; if (mm > m) m = mm; }
            db = 10 * log10(m + 1e-12) - 20 * log10(N / 4.0);   /* a full-scale sine: 0 dB */
            {
                double v = (db + 84) / 84; int r, g, bl;
                if (v < 0) v = 0; if (v > 1) v = 1;
                r = (int)(255 * fmin(1, v * 1.8)); g = (int)(255 * fmax(0, fmin(1, v * 1.8 - 0.6))); bl = (int)(255 * fmax(0, v * 2.5 - 1.5));
                img[((H - 1 - y) * W + x) * 3] = r; img[((H - 1 - y) * W + x) * 3 + 1] = g; img[((H - 1 - y) * W + x) * 3 + 2] = bl;
            }
        }
    }
    f = fopen(argv[2], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, W * H * 3, f);
    fclose(f);
    return 0;
}
