/* SPDX-License-Identifier: GPL-3.0-only */
/* One sound rendered to a WAV: console, sound (or -1-part for a drum), note, seconds held, seconds total,
 * the file (default build/host/note.wav); its peak, RMS and tail printed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "crt4.h"
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static void wav(const char *fn, const int32_t *x, int n)
{
    FILE *f = fopen(fn, "wb");
    int i; uint32_t d = (uint32_t)n * 4u, r = d + 36u; uint16_t s;
    fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    r = 16; fwrite(&r, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    r = 44100; fwrite(&r, 4, 1, f); r = 44100 * 4; fwrite(&r, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    for (i = 0; i < 2 * n; i++) { int16_t v = (int16_t)(x[i] >> 8); fwrite(&v, 2, 1, f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    int con = atoi(argv[1]), snd = atoi(argv[2]), note = argc > 3 ? atoi(argv[3]) : 60;
    double hold = argc > 4 ? atof(argv[4]) : 0.6, tot = argc > 5 ? atof(argv[5]) : 1.5;
    static int32_t buf[44100 * 2 * 20];
    int n = (int)(tot * 44100), k, off = (int)(hold * 44100), done = 0;
    en_glob_defaults();
    en_cfg_defaults(con);
    en_init();
    en_set_console(con);
    en_render(buf, 256, 2048);
    if (snd >= 0) {
        en_cfg.tp[1][TP_SOUND] = (uint8_t)snd;
        en_cfg.mute = 0;
        en_render(buf, 256, 2048);
        en_note(1, note, 1, 0);
    } else {
        en_cfg.tp[0][TP_SOUND] = (uint8_t)((-snd - 1) / 16);
        en_hit((-snd - 1) % 16, 0);
    }
    for (k = 0; k < n; k += 256) {
        if (!done && k >= off && snd >= 0) { en_note(1, note, 0, 0); done = 1; }
        en_render(buf + 2 * k, 256, 2048);
    }
    {
        double pk = 0, rms = 0, tail = 0; int i;
        for (i = 0; i < 2 * n; i++) { double v = buf[i] / 8388608.0; if (fabs(v) > pk) pk = fabs(v); rms += v * v; }
        for (i = 2 * (n - 4410); i < 2 * n; i++) { double v = buf[i] / 8388608.0; tail = fmax(tail, fabs(v)); }
        printf("peak %.3f rms %.4f tail %.4f\n", pk, sqrt(rms / (2.0 * n)), tail);
    }
    wav(argc > 6 ? argv[6] : "build/host/note.wav", buf, n);
    return 0;
}
