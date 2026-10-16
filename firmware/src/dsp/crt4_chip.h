/* SPDX-License-Identifier: GPL-3.0-only */
/* The chip consoles' parts: what their sound chips were made of, at the output's rate. A square or a pulse
 * band-limited (polyBLEP: the chip's edges, without the folding the FM-1's rate would add), a stepped
 * waveform read from a table (left stepped: the steps are the sound), noise from a shift register clocked
 * at its own rate (averaged over each output sample), a volume in so many steps. Included once, by crt4.c. */

/* the pulse, high for the fraction w of a turn: mean 0, height 1 (peak to peak); band-limited */
static inline float chip_pulse(float ph, float inc, float w)
{
    return 0.5f * blep_pulse(ph, inc, w) - 0.0f;
}

/* a volume in steps (16: a 4-bit register) */
static inline float vol_q(float x, float steps)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;
    return (float)(int)(x * steps + 0.5f) * (1.0f / steps);
}

/* a volume register in decibel steps (the PSG's 2 dB, the PC Engine's and the 5B's 1.5): the level's
 * nearest step, as the drivers wrote it (0: off, past the last step) */
static inline float vol_db(float x, float step_db, int steps)
{
    float d;
    int k;
    if (x <= 0.001f)
        return 0.0f;
    d = -20.0f * fm_log2f(fm_minf(x, 1.0f)) * 0.30103f;
    k = (int)(d / step_db + 0.5f);
    if (k >= steps)
        return 0.0f;
    return fm_exp2f(-(float)k * step_db * 0.166096f);
}

/* the NES's triangle (and the Game Boy's wave channel): 32 steps of 16 levels, 15 down to 0 and up again */
static inline float tri32(float ph)
{
    int s = (int)(ph * 32.0f) & 31;
    int lv = s < 16 ? 15 - s : s - 16;
    return (float)lv * (2.0f / 15.0f) - 1.0f;
}

/* a 32-step table of 4-bit values (0..15), stepped */
static inline float wave4(const uint8_t *w, float ph)
{
    return (float)w[(int)(ph * 32.0f) & 31] * (2.0f / 15.0f) - 1.0f;
}

/* the noise: the register clocked inc times a sample, its bit averaged over the sample (a box filter, as the
 * analog side would do much the same); step() makes the next bit (0 or 1) */
typedef uint32_t (*lfsr_fn)(uint32_t *r);
static inline uint32_t lfsr_nes(uint32_t *r)            /* 15 bits, bit 0 ^ bit 1 (long) */
{
    uint32_t b = (*r ^ (*r >> 1)) & 1u;
    *r = (*r >> 1) | (b << 14);
    return ~*r & 1u;
}
static inline uint32_t lfsr_nes_short(uint32_t *r)      /* bit 0 ^ bit 6: 93 steps, metallic */
{
    uint32_t b = (*r ^ (*r >> 6)) & 1u;
    *r = (*r >> 1) | (b << 14);
    return ~*r & 1u;
}
static inline uint32_t lfsr_gb7(uint32_t *r)            /* the Game Boy's 7-bit mode */
{
    uint32_t b = (*r ^ (*r >> 1)) & 1u;
    *r = ((*r >> 1) & ~(1u << 6)) | (b << 14) | (b << 6);
    return ~*r & 1u;
}
static inline uint32_t lfsr_sn(uint32_t *r)             /* the SN76489 (Sega's): 16 bits, taps 0 and 3 */
{
    uint32_t b = (*r ^ (*r >> 3)) & 1u;
    *r = (*r >> 1) | (b << 15);
    return *r & 1u;
}
static inline uint32_t lfsr_17(uint32_t *r)             /* 17 bits (the PC Engine's, the 5B's) */
{
    uint32_t b = (*r ^ (*r >> 3)) & 1u;
    *r = (*r >> 1) | (b << 16);
    return *r & 1u;
}
static float noise_run(chip_st *c, float inc, lfsr_fn step)
{
    float s = 0.0f;
    int k = 0;
    c->nph += inc;
    if (c->nph < 1.0f)
        return c->nout;
    while (c->nph >= 1.0f && k < 24) {
        s += (float)step(&c->lfsr);
        c->nph -= 1.0f;
        k++;
    }
    if (c->nph >= 1.0f)
        c->nph -= fm_floorf(c->nph);
    c->nout = (s / (float)k) * 2.0f - 1.0f;
    return c->nout;
}

/* a sample of 8-bit (unsigned) PCM, a step a rate tick, held between (a DAC with no filter) */
static inline float pcm8_at(const uint8_t *p, uint32_t i) { return ((float)p[i] - 128.0f) * (1.0f / 128.0f); }
/* 4-bit samples, two a byte (low nibble first) */
static inline float pcm4_at(const uint8_t *p, uint32_t i)
{
    uint32_t b = p[i >> 1];
    return ((float)((i & 1u) ? b >> 4 : b & 15u) - 7.5f) * (1.0f / 7.5f);
}

/* the frequency a chip with a divider makes for a note, after its register's rounding: clock / (mul x
 * (period + add)), the period an integer within [lo, hi] (a note out of range moves by octaves) */
static float chip_quant(float f, float clock, float mul, float add, float lo, float hi)
{
    float p = clock / (mul * f) - add;
    int g = 0;
    while (p > hi && g++ < 8) {
        f *= 2.0f;
        p = clock / (mul * f) - add;
    }
    while (p < lo && g++ < 16) {
        f *= 0.5f;
        p = clock / (mul * f) - add;
    }
    p = (float)(int)(p + 0.5f);
    if (p < lo)
        p = lo;
    return clock / (mul * (p + add));
}
