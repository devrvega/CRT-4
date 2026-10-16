/* SPDX-License-Identifier: GPL-3.0-only */
/* The Commodore 64: the SID. A voice is an oscillator of four waveforms (triangle, saw, pulse of any width,
 * noise from a 23-bit register clocked by the oscillator) that can be combined (they AND together, thin and
 * odd), ring-modulated or hard-synced by another oscillator, under the SID's ADSR (a linear attack, decay
 * and release that slow down as they fall, sixteen sustain levels), through its multimode filter (the 6581's,
 * with its curve and its grit, or the 8580's, cleaner). Its music drivers ran at 50 frames a second (PAL):
 * arpeggios a note a frame, pulse widths and filters swept a frame at a time, drums made by changing the
 * waveform from one frame to the next. 4-bit samples played through the volume register (the "digis"). */

enum { SW_TRI = 1, SW_SAW = 2, SW_PUL = 4, SW_NOI = 8 };
enum { SF_OFF, SF_LP, SF_BP, SF_HP, SF_LBP };
typedef struct {
    drv_t d;
    uint8_t wave;                      /* SW_* (several: combined) */
    uint8_t waves[4];                  /* what Wave picks from */
    uint8_t a, dc, s, r;               /* the SID's ADSR (0..15 each) */
    uint8_t pw;                        /* pulse width, 0..100 (50: a square) */
    uint8_t pwm;                       /* pulse width motion a frame (0: none) */
    uint8_t filt, cut, res;            /* the filter: its mode, cutoff 0..100, resonance 0..15 */
    int8_t fenv;                       /* the cutoff's sweep a frame (down if < 0) */
    uint8_t ring, sync;                /* by a second oscillator, at ratio x 1/8 */
    uint8_t ratio;
    uint8_t wseq[4];                   /* the first frames' waveforms (a drum's: 0 the patch's) */
    int8_t pseq[4];                    /* ... and their pitches (semitones) */
    uint8_t smp;                       /* a digi (the bank's), when wave is 0 */
} sid_patch_t;

static const uint16_t SID_ATT_MS[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
static int sid_8580;
static float sid_clock = 985248.0f;

static void sid_init(void) {}
static void sid_control(void)
{
    sid_8580 = fx_pos(2);
    drv_hz = 50.125f;
}

/* the filter's cutoff for 0..1: the 6581's curve (a slow start, a steep middle), or the 8580's (even) */
static float sid_fc(float x)
{
    x = fm_clampf(x, 0.0f, 1.0f);
    if (sid_8580)
        return 30.0f + 12000.0f * x;
    return 200.0f * fm_exp2f(x * x * 2.3f + x * 3.6f);
}

/* the ADSR's step a sample at rate 0..15 (attack: linear); decay and release: the same, slowed below 93,
 * 54, 26, 14 and 6 (of 255), as the SID's counter does */
static inline float sid_step(int rate) { return 1.0f / ((float)SID_ATT_MS[rate & 15] * 0.001f * eng->rate); }
static inline float sid_expdiv(float lv)
{
    return lv > 0.365f ? 1.0f : lv > 0.212f ? 0.5f : lv > 0.102f ? 0.25f : lv > 0.055f ? 0.125f : lv > 0.0235f ? 0.0625f : 0.0333f;
}

static int sid_wave(const sv_t *v)
{
    const sid_patch_t *p = (const sid_patch_t *)v->patch;
    int n = 0;
    if (!has_kt(v->d, KT_WAVE))
        return p->wave;
    while (n < 4 && p->waves[n])
        n++;
    return n ? p->waves[(int)(kv(v, KT_WAVE, 0.0f) * ((float)n - 0.01f))] : p->wave;
}

static void sid_start(sv_t *v, int legato)
{
    const sid_patch_t *p = (const sid_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    c->cnt = 0;                        /* the envelope from its attack (a legato note too: the SID's gate) */
    if (legato)
        return;
    if (!c->lfsr)
        c->lfsr = 0x7FFFF8u;
    c->sv[0] = c->sv[1] = 0.0f;
    c->hold = 0.0f;
    c->pw = (float)p->pw * 0.01f;
    c->pwd = p->pwm ? 1.0f : 0.0f;
    c->lp = 0.0f;                      /* the cutoff's sweep so far */
    if (!p->wave && p->smp) {
        const bk_smp_t *s = bank_smp(p->smp);
        c->smp = s->data;
        c->slen = s->nsamp;
        c->spos = 0;
        c->sfrac = 0.0f;
    }
}
static void sid_release(sv_t *v) { v->e.c.cnt = 3; }

static void sid_frame(sv_t *v)
{
    const sid_patch_t *p = (const sid_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    float note = v->pitch + v->poff, sr = eng->rate, cut, res;
    int w = sid_wave(v);
    if (v->frame < 4 && p->wseq[v->frame]) {   /* a drum's first frames: their own waveforms and pitches */
        w = p->wseq[v->frame];
        note += (float)p->pseq[v->frame];
    }
    if (has_kt(v->d, KT_BEND) && v->frame < 8)
        note += kv(v, KT_BEND, 0.0f) * 12.0f * (1.0f - (float)v->frame * 0.125f);
    if (has_kt(v->d, KT_OCTAVE))
        note += 12.0f * (float)((int)(kv(v, KT_OCTAVE, 0.5f) * 2.99f) - 1);
    if (v->trk < 0 && has_kt(v->d, KT_TONE))
        note += (kv(v, KT_TONE, 0.5f) - 0.5f) * 24.0f;
    c->cnt2 = (uint32_t)w;
    c->inc = fm_minf(mtof(note) / sr, 0.45f);
    {   /* the second oscillator (ring, sync): Ratio's knob moves it */
        float r = (float)p->ratio * 0.125f;
        if (has_kt(v->d, KT_RING))
            r = 1.0f + 3.0f * kv(v, KT_RING, 0.5f);
        if (has_kt(v->d, KT_SPEED) && p->sync)   /* a sync sweep: the ratio climbing frame by frame */
            r += (float)v->frame * 0.05f * kv(v, KT_SPEED, 0.5f) * 4.0f;
        c->mod = c->inc * r;
        c->mod = fm_minf(c->mod, 0.45f);
    }
    {   /* the pulse width: its knob, then its motion (back and forth) */
        float base = kv(v, KT_PW, (float)p->pw * 0.01f), spd = (float)p->pwm * 0.002f;
        if (has_kt(v->d, KT_PWM))
            spd = kv(v, KT_PWM, 0.3f) * 0.03f;
        if (spd > 0.0f) {
            c->pw += c->pwd > 0.0f ? spd : -spd;
            if (c->pw > 0.92f) {
                c->pw = 0.92f;
                c->pwd = -1.0f;
            } else if (c->pw < base * 0.4f + 0.04f) {
                c->pw = base * 0.4f + 0.04f;
                c->pwd = 1.0f;
            }
        } else {
            c->pw = base;
        }
    }
    {   /* the filter: the cutoff, its sweep a frame (Env), the resonance */
        float fe = has_kt(v->d, KT_FENV) ? (kv(v, KT_FENV, 0.5f) - 0.3f) * 0.08f : (float)p->fenv * 0.004f;
        if (has_kt(v->d, KT_SPEED) && !p->sync) {   /* a wobble: the cutoff on a frame-stepped triangle */
            float ph = (float)v->frame * (0.02f + 0.12f * kv(v, KT_SPEED, 0.5f));
            ph -= fm_floorf(ph);
            c->lp = (ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph) * 0.3f * kv(v, KT_FENV, 0.5f);
        } else {
            c->lp = v->frame ? fm_clampf(c->lp + fe, -1.0f, 1.0f) : (fe > 0.0f ? 0.0f : 0.25f);
        }
        cut = kv(v, KT_CUTOFF, (float)p->cut * 0.01f) + c->lp;
        res = kv(v, KT_RES, (float)p->res / 15.0f);
        if (v->trk < 0)
            cut += (kv(v, KT_TONE, 0.5f) - 0.5f) * 0.5f;
        if (p->filt)
            svf_coefs(c->wt, sid_fc(cut), 2.0f - 1.6f * res * (sid_8580 ? 1.1f : 0.9f));
    }
    if (!p->wave && p->smp) {
        const bk_smp_t *s = bank_smp(p->smp);
        c->srate = (float)s->rate * fm_exp2f((note - (float)s->root) * (1.0f / 12.0f)) / sr;
    }
}

/* the waveforms, 0..1 at phase t (the SID's 12-bit values scaled) */
static inline float sid_osc(chip_st *c, int w, float t, float inc, float m)
{
    float y = 0.0f, n = 0.0f;
    if (w & SW_NOI) {
        n = (c->nout + 1.0f) * 0.5f;
        if (w == SW_NOI)
            return n;
    }
    if (w == SW_TRI) {
        float tr = t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t;
        if (m < 0.0f)
            tr = 1.0f - tr;                     /* ring: the other oscillator's top bit turns it over */
        return tr;
    }
    if (w == SW_SAW)
        return 0.5f * (blep_saw(t, inc) + 1.0f);
    if (w == SW_PUL)
        return (1.0f - c->pw) + 0.5f * blep_pulse(t, inc, 1.0f - c->pw);   /* high from the width on */
    {   /* combined: the bits ANDed (as 8-bit values) */
        uint32_t a = 255u;
        if (w & SW_TRI)
            a &= (uint32_t)((t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t) * 255.0f);
        if (w & SW_SAW)
            a &= (uint32_t)(t * 255.0f);
        if (w & SW_PUL)
            a &= t >= c->pw ? 255u : 0u;
        if (w & SW_NOI)
            a &= (uint32_t)(n * 255.0f);
        y = (float)a * (1.0f / 255.0f);
    }
    return y;
}

static void sid_render(sv_t *v, uint32_t n, float *out)
{
    const sid_patch_t *p = (const sid_patch_t *)v->patch;
    chip_st *c = &v->e.c;
    int w = (int)c->cnt2, st = (int)c->cnt, f = p->filt;
    float env = c->hold, g = fm_minf(v->vel, 1.2f) * 0.9f, inc = c->inc, mi = c->mod, bp;
    float at = sid_step(clampi((int)(kv(v, KT_ATTACK, (float)p->a / 15.0f) * 15.0f + 0.5f), 0, 15));
    int dr = (int)(kv(v, KT_DECAY, (float)p->dc / 15.0f) * 15.0f + 0.5f), rr = (int)(kv(v, KT_RELEASE, (float)p->r / 15.0f) * 15.0f + 0.5f);
    float sus = (float)(int)(kv(v, KT_SUSTAIN, (float)p->s / 15.0f) * 15.0f + 0.5f) * (1.0f / 15.0f);
    float dk, rk;
    uint32_t j;
    if (v->trk < 0) {                  /* a drum: its Decay knob moves the part's own rates, a few either way */
        int off = (int)((v->k[1] - 0.5f) * 6.0f);
        dr = clampi((int)p->dc + off, 0, 15);
        rr = clampi((int)p->r + off, 0, 15);
    }
    dk = sid_step(dr);                 /* (the slowing below makes them the SID's three times the attack's) */
    rk = sid_step(rr);
    if (!p->wave && p->smp) {          /* a digi: 4 bits through the volume register */
        const bk_smp_t *s = bank_smp(p->smp);
        float y = c->acc, lvl = sid_8580 ? 0.35f : 1.0f;
        for (j = 0; j < n; j++) {
            c->sfrac += c->srate;
            while (c->sfrac >= 1.0f) {
                c->sfrac -= 1.0f;
                if (++c->spos >= c->slen) {
                    v->done = 1;
                    break;
                }
                y = pcm4_at(s->data, c->spos);
            }
            out[j] += y * g * lvl * (st >= 3 ? 0.0f : 1.0f);
        }
        c->acc = y;
        if (st >= 3 && !(p->d.flags & DF_HOLD))
            v->done = 1;
        return;
    }
    for (j = 0; j < n; j++) {
        float y, m = 1.0f;
        /* the envelope */
        if (st == 0) {
            env += at;
            if (env >= 1.0f) {
                env = 1.0f;
                st = 1;
            }
        } else if (st == 1) {
            env -= dk * sid_expdiv(env);
            if (env <= sus) {
                env = sus;
                st = 2;
            }
        } else if (st == 3) {
            env -= rk * sid_expdiv(env);
            if (env <= 0.0f) {
                env = 0.0f;
                v->done = 1;
            }
        }
        /* the oscillators */
        if (p->ring || p->sync) {
            c->ph2 += mi;
            if (c->ph2 >= 1.0f) {
                c->ph2 -= 1.0f;
                if (p->sync)
                    c->ph = 0.0f;               /* hard sync: the first starts over */
            }
            m = c->ph2 < 0.5f ? 1.0f : -1.0f;
        }
        c->ph += inc;
        if (c->ph >= 1.0f)
            c->ph -= 1.0f;
        if (w & SW_NOI) {                       /* the noise: clocked sixteen times a turn */
            c->nph += inc * 16.0f;
            while (c->nph >= 1.0f) {
                uint32_t b = ((c->lfsr >> 22) ^ (c->lfsr >> 17)) & 1u;
                c->nph -= 1.0f;
                c->lfsr = ((c->lfsr << 1) | b) & 0x7FFFFFu;
                c->nout = (float)(((c->lfsr >> 20) & 1u) << 7 | ((c->lfsr >> 18) & 1u) << 6 | ((c->lfsr >> 14) & 1u) << 5 |
                                  ((c->lfsr >> 11) & 1u) << 4 | ((c->lfsr >> 9) & 1u) << 3 | ((c->lfsr >> 5) & 1u) << 2 |
                                  ((c->lfsr >> 2) & 1u) << 1 | (c->lfsr & 1u)) * (2.0f / 255.0f) - 1.0f;
            }
        }
        y = sid_osc(c, w, c->ph, inc, p->ring ? m : 1.0f) * 2.0f - 1.0f;
        y *= env;
        if (f) {                                /* the filter (the 6581's a little rough) */
            float lp = sid_8580 ? svf(c->sv, c->wt, y, &bp) : svf_hot(c->sv, c->wt, y, &bp), o;
            switch (f) {
            case SF_LP: o = lp; break;
            case SF_BP: o = bp * 1.4f; break;
            case SF_HP: o = y - c->wt[3] * bp - lp; break;
            default: o = lp + bp; break;
            }
            y = o;
        }
        out[j] += y * g * 0.55f;
    }
    c->hold = env;
    c->cnt = (uint32_t)st;
}

static int sid_wave_text(int snd, int pos, char *b);

/* ------------------------------------------------------------------- the sounds --- */
#define SD(k0, k1, k2, k3, vib, vspd, vdly, sw, swt, arp, fl, oct) {{k0, k1, k2, k3}, 0, 0, 100, 0, vib, vspd, vdly, sw, swt, arp, (fl) | DF_HWENV, oct}
static const sid_patch_t SID_P[EN_NSOUND] = {
    /* basses */
    {SD(KT_PW, KT_PWM, KT_CUTOFF, KT_DECAY, 0, 30, 0, 0, 0, 0, 0, -1), SW_PUL, {0}, 0, 6, 9, 3, 30, 4, SF_LP, 45, 6, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_CUTOFF, KT_RES, KT_FENV, KT_DECAY, 0, 30, 0, 0, 0, 0, 0, -1), SW_SAW, {0}, 0, 7, 6, 3, 50, 0, SF_LP, 30, 10, -4, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_RING, KT_CUTOFF, KT_DECAY, KT_RES, 0, 30, 0, 0, 0, 0, 0, -1), SW_SAW, {0}, 0, 6, 8, 3, 50, 0, SF_LP, 55, 5, 0, 0, 1, 20, {0}, {0}, 0},
    {SD(KT_PW, KT_DECAY, KT_CUTOFF, KT_BEND, 0, 30, 0, 0, 0, 0, 0, -1), SW_PUL, {0}, 0, 5, 4, 2, 25, 0, SF_LP, 60, 4, 0, 0, 0, 8, {SW_NOI, 0, 0, 0}, {12, 0, 0, 0}, 0},
    /* keys */
    {SD(KT_PW, KT_ARPSPD, KT_ARPMODE, KT_DECAY, 0, 30, 0, 0, 0, 1, DF_AUTOARP, 0), SW_PUL, {0}, 0, 8, 6, 4, 35, 3, SF_OFF, 50, 0, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_PWM, KT_CUTOFF, KT_ATTACK, KT_RELEASE, 0, 30, 0, 0, 0, 1, 0, 0), SW_PUL, {0}, 8, 9, 12, 9, 40, 3, SF_LP, 55, 4, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_RING, KT_DECAY, KT_RELEASE, KT_CUTOFF, 0, 30, 0, 0, 0, 1, 0, 0), SW_TRI, {0}, 0, 9, 0, 9, 50, 0, SF_OFF, 80, 0, 0, 1, 0, 22, {0}, {0}, 0},
    {SD(KT_WAVE, KT_ATTACK, KT_RELEASE, KT_VIB, 0, 30, 20, 0, 0, 1, 0, 0), SW_TRI | SW_SAW, {SW_TRI | SW_SAW, SW_PUL | SW_TRI, SW_PUL | SW_SAW, SW_TRI}, 2, 9, 13, 6, 45, 0, SF_OFF, 50, 0, 0, 0, 0, 8, {0}, {0}, 0},
    /* leads */
    {SD(KT_VIB, KT_PWM, KT_DECAY, KT_SUSTAIN, 30, 45, 20, 0, 0, 1, 0, 0), SW_PUL, {0}, 0, 8, 11, 5, 40, 4, SF_OFF, 50, 0, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_RING, KT_SPEED, KT_VIB, KT_RELEASE, 25, 45, 25, 0, 0, 1, 0, 0), SW_SAW, {0}, 0, 8, 11, 5, 50, 0, SF_OFF, 50, 0, 0, 0, 1, 12, {0}, {0}, 0},
    {SD(KT_CUTOFF, KT_RES, KT_VIB, KT_RELEASE, 25, 45, 25, 0, 0, 1, 0, 0), SW_SAW, {0}, 0, 8, 11, 5, 50, 0, SF_BP, 45, 11, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_PW, KT_CUTOFF, KT_VIB, KT_RELEASE, 25, 45, 25, 0, 0, 1, 0, 0), SW_PUL, {0}, 0, 8, 11, 5, 45, 2, SF_LP, 80, 3, 0, 0, 0, 8, {SW_NOI, SW_NOI, 0, 0}, {0, 0, 0, 0}, 0},
    /* its own */
    {SD(KT_OCTAVE, KT_RELEASE, KT_NONE, KT_NONE, 0, 30, 0, 0, 0, 1, DF_HOLD, 0), 0, {0}, 0, 0, 15, 1, 50, 0, SF_OFF, 50, 0, 0, 0, 0, 8, {0}, {0}, BK_C64_VOICE},
    {SD(KT_SPEED, KT_FENV, KT_CUTOFF, KT_RES, 0, 30, 0, 0, 0, 1, 0, -1), SW_SAW, {0}, 0, 9, 12, 4, 50, 0, SF_LP, 40, 11, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_SWEEP, KT_DECAY, KT_CUTOFF, KT_WAVE, 0, 30, 0, 40, 0, 1, 0, 1), SW_NOI, {SW_NOI, SW_PUL | SW_NOI, SW_SAW | SW_NOI, 0}, 0, 9, 4, 3, 50, 0, SF_BP, 70, 6, 0, 0, 0, 8, {0}, {0}, 0},
    {SD(KT_SPEED, KT_RING, KT_DECAY, KT_CUTOFF, 0, 30, 0, 0, 0, 1, 0, 0), SW_SAW, {0}, 0, 10, 10, 5, 50, 0, SF_LP, 85, 4, 0, 0, 1, 8, {0}, {0}, 0},
};
static const en_sound_t SID_SND[EN_NSOUND] = {
    {"Pulse", "SID pulse", {"Width", "PWM", "Cutoff", "Decay"}, {30, 30, 45, 40}, {0, 0, 0, 0}, &SID_P[0]},
    {"Filter", "SID saw, low-pass", {"Cutoff", "Reso", "Env", "Decay"}, {30, 65, 20, 45}, {0, 0, 0, 0}, &SID_P[1]},
    {"Sync", "SID hard sync", {"Ratio", "Cutoff", "Decay", "Reso"}, {45, 55, 50, 30}, {0, 0, 0, 0}, &SID_P[2]},
    {"Slap", "SID noise, pulse", {"Width", "Decay", "Cutoff", "Thump"}, {25, 30, 60, 0}, {0, 0, 0, 0}, &SID_P[3]},
    {"Arp chords", "SID arpeggios", {"Width", "Speed", "Mode", "Decay"}, {35, 0, 0, 40}, {0, 6, 4, 0}, &SID_P[4]},
    {"PWM pad", "SID pulse", {"PWM", "Cutoff", "Attack", "Release"}, {25, 55, 55, 60}, {0, 0, 0, 0}, &SID_P[5]},
    {"Ring bells", "SID ring mod", {"Ring", "Decay", "Release", "Cutoff"}, {55, 60, 60, 80}, {0, 0, 0, 0}, &SID_P[6]},
    {"Combo organ", "SID combined waves", {"Wave", "Attack", "Release", "Vibrato"}, {0, 15, 40, 0}, {4, 0, 0, 0}, &SID_P[7]},
    {"PWM lead", "SID pulse", {"Vibrato", "PWM", "Decay", "Sustain"}, {30, 30, 55, 75}, {0, 0, 0, 0}, &SID_P[8]},
    {"Sync lead", "SID hard sync", {"Ratio", "Sweep", "Vibrato", "Release"}, {40, 30, 25, 35}, {0, 0, 0, 0}, &SID_P[9]},
    {"Filter lead", "SID band-pass", {"Cutoff", "Reso", "Vibrato", "Release"}, {45, 70, 25, 35}, {0, 0, 0, 0}, &SID_P[10]},
    {"Noise lead", "SID noise, pulse", {"Width", "Cutoff", "Vibrato", "Release"}, {45, 80, 25, 35}, {0, 0, 0, 0}, &SID_P[11]},
    {"Digi voice", "4-bit digi", {"Octave", "Release", "", ""}, {50, 6, 0, 0}, {3, 0, 0, 0}, &SID_P[12]},
    {"Wobble", "SID filter sweep", {"Speed", "Depth", "Cutoff", "Reso"}, {40, 60, 40, 75}, {0, 0, 0, 0}, &SID_P[13]},
    {"Tape screech", "SID noise sweep", {"Sweep", "Decay", "Cutoff", "Wave"}, {80, 60, 70, 0}, {0, 0, 0, 3}, &SID_P[14]},
    {"Sync sweep", "SID hard sync", {"Speed", "Ratio", "Decay", "Cutoff"}, {50, 20, 65, 85}, {0, 0, 0, 0}, &SID_P[15]},
};
static int sid_wave_text(int snd, int pos, char *b)
{
    static const char *const N[16] = {"Digi", "Tri", "Saw", "Tri+Saw", "Pulse", "Pulse+Tri", "Pulse+Saw", "Pul+S+T",
                                      "Noise", "N+Tri", "N+Saw", "N+S+T", "N+Pulse", "N+P+T", "N+P+S", "All"};
    const sid_patch_t *p = (const sid_patch_t *)SID_SND[(unsigned)snd % EN_NSOUND].patch;
    int w = p->waves[pos & 3];
    if (!w)
        return 0;
    scopy(b, N[w & 15], 10);
    return 1;
}

/* the kits: the drivers' drums (a frame of noise, then a triangle or a pulse falling), and the digis */
#define SDR(w0, w1, w2, w3, p0, p1, p2, p3, wave, a, d, pw, filt, cut, res) \
    {SD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 30, 0, 0, 0, 0, 0, 0), wave, {0}, a, d, 0, d, pw, 0, filt, cut, res, 0, 0, 0, 8, {w0, w1, w2, w3}, {p0, p1, p2, p3}, 0}
#define SDG(smp) {SD(KT_NONE, KT_DECAY, KT_TONE, KT_NONE, 0, 30, 0, 0, 0, 0, DF_HOLD, 0), 0, {0}, 0, 0, 15, 0, 50, 0, SF_OFF, 50, 0, 0, 0, 0, 8, {0}, {0}, smp}
static const sid_patch_t SID_DP[2][EN_NPART] = {
    {SDR(SW_NOI, SW_TRI, SW_TRI, SW_TRI, 24, 7, 2, 0, SW_TRI, 0, 6, 50, SF_OFF, 50, 0),
     SDR(SW_NOI, SW_PUL, SW_NOI, SW_NOI, 0, -5, 0, 0, SW_NOI, 0, 5, 50, SF_OFF, 50, 0),
     SDR(SW_NOI, SW_TRI, SW_TRI, SW_TRI, 12, 5, 2, 0, SW_TRI, 0, 6, 50, SF_OFF, 50, 0),
     SDR(SW_NOI, SW_TRI, SW_TRI, SW_TRI, 12, 5, 2, 0, SW_TRI, 0, 6, 50, SF_OFF, 50, 0),
     SDR(SW_NOI, SW_TRI, SW_TRI, SW_TRI, 12, 5, 2, 0, SW_TRI, 0, 5, 50, SF_OFF, 50, 0),
     SDR(SW_NOI, 0, 0, 0, 24, 0, 0, 0, SW_PUL, 0, 2, 50, SF_HP, 60, 4),
     SDR(SW_NOI, SW_NOI, SW_NOI, 0, 0, 0, 0, 0, SW_NOI, 0, 5, 50, SF_BP, 55, 6),
     SDR(0, 0, 0, 0, 0, 0, 0, 0, SW_NOI, 0, 2, 50, SF_HP, 85, 2),
     SDR(0, 0, 0, 0, 0, 0, 0, 0, SW_NOI, 0, 7, 50, SF_HP, 85, 2),
     SDR(0, 0, 0, 0, 0, 0, 0, 0, SW_NOI, 0, 9, 50, SF_HP, 60, 2),
     SDR(SW_PUL, SW_PUL, 0, 0, 12, 0, 0, 0, SW_PUL, 0, 4, 25, SF_OFF, 50, 0)},
    {SDG(BK_C64_KICK), SDG(BK_C64_SNARE), SDG(BK_C64_TOM), SDG(BK_C64_TOM), SDG(BK_C64_TOM), SDG(BK_C64_RIM),
     SDG(BK_C64_CLAP), SDG(BK_C64_CHAT), SDG(BK_C64_OHAT), SDG(BK_C64_CRASH), SDG(BK_C64_VOICE)},
};
#undef SDR
#undef SDG
#undef SD
static const dkit_t SID_KIT[EN_NKIT] = {
    {"SID", {{"KCK", "Kick", &SID_DP[0][0], 40, 0, 0}, {"SNR", "Snare", &SID_DP[0][1], 62, 0, 0},
             {"LT", "Low tom", &SID_DP[0][2], 48, 0, 0}, {"MT", "Mid tom", &SID_DP[0][3], 54, 0, 0},
             {"HT", "High tom", &SID_DP[0][4], 60, 0, 0}, {"RIM", "Rim", &SID_DP[0][5], 84, 0, 0},
             {"CLP", "Clap", &SID_DP[0][6], 72, 0, 0}, {"CH", "Closed hat", &SID_DP[0][7], 96, 0, 1},
             {"OH", "Open hat", &SID_DP[0][8], 96, 0, 1}, {"CRS", "Crash", &SID_DP[0][9], 90, 0, 0},
             {"BLP", "Blip", &SID_DP[0][10], 84, 0, 0}}},
    {"Digi", {{"KCK", "Kick", &SID_DP[1][0], 60, 0, 0}, {"SNR", "Snare", &SID_DP[1][1], 60, 0, 0},
              {"LT", "Low tom", &SID_DP[1][2], 55, 0, 0}, {"MT", "Mid tom", &SID_DP[1][3], 60, 0, 0},
              {"HT", "High tom", &SID_DP[1][4], 65, 0, 0}, {"RIM", "Rim", &SID_DP[1][5], 60, 0, 0},
              {"CLP", "Clap", &SID_DP[1][6], 60, 0, 0}, {"CH", "Closed hat", &SID_DP[1][7], 60, 0, 1},
              {"OH", "Open hat", &SID_DP[1][8], 60, 0, 1}, {"CRS", "Crash", &SID_DP[1][9], 60, 0, 0},
              {"YEA", "Yeah!", &SID_DP[1][10], 57, 0, 0}}},
};
static const engine_t SID_ENG = {44100.0f, 50.125f, sid_start, sid_frame, sid_render, sid_release};
