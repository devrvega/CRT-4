/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's voices: what every console shares. A voice is a note on one of the console's channels (or a drum
 * part on one); the console's engine (crt4_<console>.c) makes its sound. Between them sits the music driver,
 * as in the games: once a frame (60 a second; 50 on a PAL machine) it moves the envelope, the vibrato, the
 * pitch sweep, the glide and the arpeggio of a chord, and the engine sets its channel's registers from them.
 * Between frames the channel runs on its own, sample by sample. Included once, by crt4.c.
 *
 * A track: four voices on the consoles that play chords; on the mono ones, one voice (a chord becomes an
 * arpeggio, a note a frame) and a second for the echo the trackers made: the line again, a few steps later
 * and quieter, on a channel of its own. */

#define NV 4                           /* voices a track */
#define SUB 32                         /* the engines' inner blocks */

/* what a knob does (a sound's patch says which four it has); the engines read the ones that concern them */
enum {
    KT_NONE, KT_WAVE, KT_ATTACK, KT_DECAY, KT_SUSTAIN, KT_RELEASE, KT_VIB, KT_SWEEP, KT_ARPSPD, KT_ARPMODE,
    KT_PW, KT_PWM, KT_CUTOFF, KT_RES, KT_FENV, KT_MOD, KT_FB, KT_DETUNE, KT_TONE, KT_NOISE, KT_SPEED,
    KT_OCTAVE, KT_START, KT_BEND, KT_DRIVE, KT_RING, KT_VOWEL, KT_N
};

/* the driver's part of every patch: the envelope in frames, the vibrato, the sweep, the arpeggio. Values the
 * knobs can move are kept 0..100 (as a knob), the engines turn them into their own units. */
enum { DF_HWENV = 1, DF_AUTOARP = 2, DF_NOREL = 4, DF_HOLD = 8 };
typedef struct {
    uint8_t kt[4];                     /* the sound's four knobs: KT_* */
    uint8_t att, dec, sus, rel;        /* 0..100 (the driver's envelope, unless DF_HWENV) */
    uint8_t vib, vspd, vdly;           /* vibrato: depth, speed, delay (0..100) */
    int8_t sweep;                      /* pitch sweep, semitones a frame x 1/8 (+ up) */
    uint8_t sweep_t;                   /* its frames (0: as long as the note) */
    uint8_t arp;                       /* a chord: frames a note (1..8; 0: 1) */
    uint8_t flags;                     /* DF_* */
    int8_t oct;                        /* octaves the sound sits above or below the note */
} drv_t;

/* ---------------------------------------------------------- the engines' state --- */
typedef struct {                       /* the chip consoles: oscillators, counters, noise */
    float ph, ph2, inc;                /* the channel's phase (turns), a second one, the step */
    float nph, nout, acc, hold;        /* the noise clock, its output; a slow DAC's hold */
    float hp, lp, lp2;                 /* filters */
    uint32_t lfsr, cnt, cnt2;
    int32_t div;                       /* a divider's count */
    float sv[2];                       /* the SID's filter */
    float pw, pwd;                     /* pulse width, its motion */
    float mod;                         /* the FDS's or the PCE's modulator phase */
    const uint8_t *smp;                /* a sample (DMC, digi, DDA, wave kit) */
    uint32_t spos, slen;
    float sfrac, srate;
    int32_t dlt;                       /* the DMC's counter */
    float wt[32];                      /* a wave table being morphed (the Game Boy's synth); a history */
    uint32_t aux[4];                   /* an engine's own counters (the TIA's two channels) */
} chip_st;
typedef struct {                       /* the YM2612: four operators */
    float ph[4], att[4], o1, o2;       /* phases (turns), attenuations (0..1023, the chip's), op 1's last two */
    uint8_t stg[4];                    /* 0 attack, 1 decay, 2 sustain, 3 release, 4 off */
    float inc[4];                      /* phase steps */
    float rate[4][4];                  /* each operator's EG steps a sample: attack, decay, sustain, release */
    float sl[4], tl[4];                /* sustain level, total level (attenuation units) */
    float lfo, egc;
    float dac;                         /* the last output (the PSG's square, a DAC sample) */
    uint32_t lfsr;
    float ph5;
    const uint8_t *smp;
    uint32_t spos, slen;
    float sfrac, srate;
} fm_st;
typedef struct {                       /* the sample consoles: a sample in its own format */
    const uint8_t *blk;                /* the block being played */
    const uint8_t *start, *loop, *end; /* the sample, its loop, its end */
    uint32_t pos;                      /* in the block */
    float frac;                        /* the position between samples */
    float step;                        /* samples a native sample (the pitch) */
    int32_t p1, p2;                    /* the decoder's last two (prediction) */
    int16_t buf[32];                   /* the block, decoded */
    int16_t hist[4];                   /* the interpolator's last four */
    uint8_t bn, fmt;                   /* samples in a block; the format */
    int32_t env;                       /* the hardware envelope (the SNES's units, 0..2047) */
    float lvl;                         /* ... the others': a level, 0..1 */
    uint8_t est;                       /* its stage: 0 attack 1 decay 2 sustain 3 release 4 off */
    int32_t ecnt;                      /* its rate counter */
    float g2, ph2;                     /* a second, detuned voice (the drivers' doubling) */
    const void *s;                     /* the bank's description of the sample */
} smp_st;

typedef struct sv_s {
    uint8_t on, gate, note, acc;
    uint8_t echo;                      /* an echo of the track's line (quieter, on its own voice) */
    uint8_t done;                      /* the engine says it has finished */
    int16_t lock[2];                   /* knob A, B for this note (-1: the track's) */
    float vel;
    float pitch, tgt, gl;              /* the note (semitones) now, its target, the glide a frame (1: none) */
    uint8_t chord[EN_NNOTE], nchord, ci, cdir;   /* a chord on a mono console: its notes, the arp's place */
    int32_t gate_left;                 /* samples until the gate closes; < 0: held (a key) */
    int32_t t;                         /* samples since the note began */
    float fade;                        /* > 0: fading out for a stolen voice */
    struct {
        uint8_t note, acc, live;
        float vel;
        int32_t gate;
        int16_t lock[2];
    } pend;
    uint8_t pending;
    uint32_t age;
    /* the driver */
    float fleft;                       /* samples to the next frame */
    uint16_t frame;                    /* frames since the note began */
    uint8_t est;                       /* the envelope: 0 attack 1 decay 2 sustain 3 release */
    float elev;                        /* its level 0..1 */
    float vol;                         /* the volume this frame (the engine quantizes it as its chip did) */
    float poff;                        /* the pitch this frame, over the note: vibrato, sweep, arpeggio, effects */
    float sweep;                       /* the sweep so far */
    float k[4];                        /* the knobs this frame, 0..1 (a lock first) */
    float pan;                         /* -1 .. 1 (a drum part's own) */
    int8_t trk;                        /* the track (-1: a drum part) */
    uint8_t part;
    const drv_t *d;                    /* the patch's driver part */
    const void *patch;                 /* the engine's patch */
    union {
        chip_st c;
        fm_st f;
        smp_st s;
    } e;
} sv_t;

typedef struct {
    sv_t v[NV];
    int8_t snd;
    uint8_t held[8], held_snd[8], held_n;   /* the keys held (live), oldest first; and the notes they sound */
    int32_t idle;
    float last_pitch;
    float out[264];
    float alt[264];                    /* what a voice sends to the other side (the Game Boy's ping-pong) */
    uint32_t ages;
    /* the tracker's echo: notes to play again (mono consoles, and the chip consoles' poly ones) */
    struct {
        int32_t at;                    /* samples from now */
        uint8_t note[EN_NNOTE], nn;
        float vel;
        int32_t gate;
        uint8_t rep;
    } eq[8];
    uint8_t eqn;
} st_t;
static st_t trk[EN_NSYN];
static sv_t dv[EN_NPART];              /* the drum parts, a voice each */
static float fade_buf[264];
static float *alt_out;                 /* the track's other side (0: none) */
static float *alt_seg;                 /* ... where the engine's render writes it, this segment (0: nowhere) */
static int alt_used;
static int poly_cap = NV;              /* voices a track may sound (the governor in en_render lowers it) */
static float tick_len = 230.0f;        /* samples a tick (24 a step): the sequencer's (crt4.c) */
static float drv_hz = 60.0f;           /* the driver's frames a second (the console's; 50 on a PAL machine) */
static float fx_dive, fx_vib, fx_vibph;   /* the punch-in effects that reach the voices: a pitch dive, a vibrato */

/* ---------------------------------------------------------------- the engines --- */
typedef struct {
    float rate;                        /* the rate the voices run at (the console's own) */
    float frame_hz;                    /* the driver's frames a second */
    void (*start)(sv_t *v, int legato);   /* a note: the channel from the top, or (legato) kept */
    void (*frame)(sv_t *v);            /* a frame: the channel's registers from the driver's values */
    void (*render)(sv_t *v, uint32_t n, float *out);   /* n samples, mono, added; sets done when finished */
    void (*release)(sv_t *v);          /* the gate shut */
} engine_t;
static const engine_t *eng;            /* the console's */
static float ratio = 1.0f;             /* the console's rate / the output's */

static inline float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float knob(const sv_t *v, int i) { return v->k[i]; }
/* the value of the knob that does kt, or base (0..1) if no knob does */
static inline float kv(const sv_t *v, int kt, float base)
{
    const drv_t *d = v->d;
    if (d->kt[0] == kt)
        return v->k[0];
    if (d->kt[1] == kt)
        return v->k[1];
    if (d->kt[2] == kt)
        return v->k[2];
    if (d->kt[3] == kt)
        return v->k[3];
    return base;
}
static inline float pct(uint8_t x) { return (float)x * 0.01f; }
static int has_kt(const drv_t *d, int kt) { return d->kt[0] == kt || d->kt[1] == kt || d->kt[2] == kt || d->kt[3] == kt; }
/* the frames an envelope stage takes, from 0..1: 0 none; 0.2 about 70 ms, 0.5 under a second, 1 five */
static inline float frames_of(float x) { return x <= 0.0f ? 0.0f : 0.5f + 300.0f * fm_powf(x, 2.6f); }

/* the knobs this frame: the note's lock, the track's knob, or the kit's */
static void knobs_now(sv_t *v)
{
    int i;
    if (v->trk >= 0) {
        const uint8_t *s = en_cfg.sk[v->trk][(unsigned)trk[v->trk].snd % EN_NSOUND];
        for (i = 0; i < 4; i++)
            v->k[i] = (float)((i < 2 && v->lock[i] >= 0) ? v->lock[i] : s[i]) * 0.01f;
    }
}

/* the driver: a frame */
static void drv_frame(sv_t *v)
{
    const drv_t *d = v->d;
    float a, dk, s, r, depth;
    knobs_now(v);
    if (!(d->flags & DF_HWENV)) {      /* the envelope: linear in the chip's volume steps, as the drivers did */
        a = frames_of(kv(v, KT_ATTACK, pct(d->att)));
        if (v->trk < 0)                /* a drum: its Decay knob scales the part's own */
            dk = frames_of(pct(d->dec)) * fm_exp2f((v->k[1] - 0.5f) * 3.0f);
        else
            dk = frames_of(kv(v, KT_DECAY, pct(d->dec)));
        s = kv(v, KT_SUSTAIN, pct(d->sus));
        r = frames_of(kv(v, KT_RELEASE, pct(d->rel)));
        if (!v->gate && v->est < 3 && !(d->flags & DF_HOLD && v->gate_left < 0))
            v->est = 3;
        switch (v->est) {
        case 0:
            v->elev += a < 1.0f ? 1.0f : 1.0f / a;
            if (v->elev >= 1.0f) {
                v->elev = 1.0f;
                v->est = 1;
            }
            break;
        case 1:
            if (d->dec == 0 && !(d->kt[0] == KT_DECAY || d->kt[1] == KT_DECAY || d->kt[2] == KT_DECAY || d->kt[3] == KT_DECAY)) {
                v->elev = 1.0f;        /* no decay: held at full */
                v->est = 2;
                break;
            }
            v->elev -= dk < 1.0f ? 1.0f : 1.0f / dk;
            if (v->elev <= s) {
                v->elev = s;
                v->est = 2;
                if (s <= 0.0f)
                    v->done = 1;
            }
            break;
        case 2:
            v->elev = s;
            if (s <= 0.0f)
                v->done = 1;
            break;
        default:
            if (d->flags & DF_NOREL)
                v->elev = 0.0f;
            else
                v->elev -= r < 1.0f ? 1.0f : 1.0f / r;
            if (v->elev <= 0.0f) {
                v->elev = 0.0f;
                v->done = 1;
            }
            break;
        }
    } else if (!v->gate && v->est < 3) {
        v->est = 3;
        if (eng->release)
            eng->release(v);
    }
    /* the pitch: glide, then what the frame adds */
    if (v->gl < 1.0f)
        v->pitch += (v->tgt - v->pitch) * v->gl;
    else
        v->pitch = v->tgt;
    v->poff = 12.0f * (float)d->oct;
    if (v->nchord > 1) {               /* a chord on a mono console: a note a frame (or a few) */
        int fr = d->arp ? d->arp : 1, mode = 0;
        if (d->kt[0] == KT_ARPSPD || d->kt[1] == KT_ARPSPD || d->kt[2] == KT_ARPSPD || d->kt[3] == KT_ARPSPD)
            fr = 1 + (int)(kv(v, KT_ARPSPD, 0.0f) * 5.99f);
        mode = (int)(kv(v, KT_ARPMODE, 0.0f) * 3.99f);
        if (v->frame % (uint16_t)fr == 0 && v->frame) {
            int n = v->nchord;
            switch (mode) {
            case 1: v->ci = (uint8_t)((v->ci + n - 1) % n); break;          /* down */
            case 2:                                                            /* up and down */
                if (n > 1) {
                    int c = (int)v->ci + (v->cdir ? -1 : 1);
                    if (c >= n) {
                        c = n - 2;
                        v->cdir = 1;
                    } else if (c < 0) {
                        c = 1;
                        v->cdir = 0;
                    }
                    v->ci = (uint8_t)c;
                }
                break;
            case 3: v->ci = (uint8_t)(rnd() % (uint32_t)n); break;            /* at random */
            default: v->ci = (uint8_t)((v->ci + 1) % n); break;               /* up */
            }
        }
        v->poff += (float)(v->chord[v->ci % v->nchord] - v->chord[0]);
    }
    depth = kv(v, KT_VIB, pct(d->vib));
    if ((depth > 0.0f && v->frame >= (uint16_t)(d->vdly * 2u)) || fx_vib > 0.0f) {   /* the vibrato: a triangle, a frame a step */
        float spd = 0.02f + 0.1f * pct(d->vspd), ph, tri;
        ph = (float)v->frame * spd;
        ph -= fm_floorf(ph);
        tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        v->poff += tri * depth * 0.8f;
        if (fx_vib > 0.0f) {
            float q = fx_vibph - fm_floorf(fx_vibph);
            v->poff += (q < 0.5f ? 4.0f * q - 1.0f : 3.0f - 4.0f * q) * 0.6f * fx_vib;
        }
    }
    {   /* the sweep: up or down so many eighths of a semitone a frame (a knob scales it) */
        float sw = (float)d->sweep * 0.125f;
        if (d->kt[0] == KT_SWEEP || d->kt[1] == KT_SWEEP || d->kt[2] == KT_SWEEP || d->kt[3] == KT_SWEEP) {
            float x = kv(v, KT_SWEEP, 0.5f) * 2.0f - 1.0f;   /* -1 .. 1: down .. up, finer near the middle */
            sw = x * fm_fabsf(x) * (d->sweep ? fm_fabsf((float)d->sweep) : 12.0f) * 0.125f;
        }
        if (d->sweep_t == 0 || v->frame < d->sweep_t)
            v->sweep += sw;
        v->poff += v->sweep;
    }
    v->poff -= fx_dive;
    if (v->pitch + v->poff < 6.0f)     /* no lower than a chip could go (and the sweeps stop there) */
        v->poff = 6.0f - v->pitch;
    else if (v->pitch + v->poff > 132.0f)
        v->poff = 132.0f - v->pitch;
    if (eng->frame)
        eng->frame(v);
    v->frame++;
}

/* a voice's samples: the engine's, split at the driver's frames */
static void voice_run(sv_t *v, uint32_t n, float *out, float *alt)
{
    uint32_t j = 0;
    float flen = eng->rate / drv_hz;
    while (j < n) {
        uint32_t m;
        if (v->fleft <= 0.0f) {
            drv_frame(v);
            v->fleft += flen;
        }
        m = (uint32_t)v->fleft + 1u;
        if (m > n - j)
            m = n - j;
        alt_seg = alt ? alt + j : 0;
        eng->render(v, m, out + j);
        v->fleft -= (float)m;
        j += m;
    }
    v->t += (int32_t)n;
}

/* --------------------------------------------------------------- the notes --- */
static const en_sound_t *snd_of_trk(int t);
static float glide_of(int t, int legato)              /* the glide's share a frame (1: none) */
{
    float g = (float)en_cfg.tp[t + 1][TP_GLIDE] * 0.01f;
    if (g <= 0.0f)                                    /* none set: a slide still slides (three frames or so) */
        return legato ? 0.45f : 1.0f;
    return 1.0f / (1.0f + 30.0f * g * g);
}

static void voice_init(sv_t *v, int note, float vel, int acc, int32_t gate, const int16_t *lock, int legato)
{
    v->on = 1;
    v->gate = 1;
    v->done = 0;
    v->note = (uint8_t)note;
    v->acc = (uint8_t)acc;
    v->lock[0] = lock ? lock[0] : -1;
    v->lock[1] = lock ? lock[1] : -1;
    v->vel = vel;
    v->tgt = (float)note;
    v->gate_left = gate;
    v->fade = 0.0f;
    v->pending = 0;
    if (!legato) {
        v->t = 0;
        v->frame = 0;
        v->fleft = 0.0f;
        v->est = 0;
        v->elev = 0.0f;
        v->sweep = 0.0f;
        v->ci = 0;
        v->cdir = 0;
    }
    knobs_now(v);
}

static void voice_start(st_t *T, int t, sv_t *v, int note, float vel, int acc, int32_t gate, const int16_t *lock)
{
    const en_sound_t *s = snd_of_trk(t);
    float from = T->last_pitch;
    v->trk = (int8_t)t;
    v->patch = s->patch;
    v->d = (const drv_t *)s->patch;
    voice_init(v, note, vel, acc, gate, lock, 0);
    v->gl = glide_of(t, 0);
    v->pitch = (v->gl < 1.0f && from > 0.0f) ? from : (float)note;
    v->age = ++T->ages;
    v->pan = 0.0f;
    eng->start(v, 0);
    T->last_pitch = (float)note;
}

/* a mono console's track: one voice; a chord in it is an arpeggio; a note over a held one glides into it */
static void mono_note_on(int t, const int *notes, int nn, float vel, int acc, int32_t gate, const int16_t *lock, int echo)
{
    st_t *T = &trk[t];
    sv_t *v = &T->v[echo ? 1 : 0];
    int i, legato = !echo && v->on && v->gate && v->est < 3 && v->trk == t && nn == 1 && v->nchord <= 1;
    if (legato) {
        voice_init(v, notes[0], vel, acc, gate, lock, 1);
        v->gl = glide_of(t, 1);
        v->nchord = 1;
        v->chord[0] = (uint8_t)notes[0];
        if (acc)
            v->elev = 1.0f;
        eng->start(v, 1);
        T->last_pitch = (float)notes[0];
        return;
    }
    voice_start(T, t, v, notes[0], vel, acc, gate, lock);
    v->echo = (uint8_t)echo;
    v->nchord = (uint8_t)(nn > EN_NNOTE ? EN_NNOTE : nn);
    for (i = 0; i < v->nchord; i++)
        v->chord[i] = (uint8_t)notes[i];
}

/* a note for track t: the sequencer's (gate in samples) or a key's (gate < 0: until en_note off) */
static void poly_note_on(int t, int note, float vel, int acc, int32_t gate, const int16_t *lock, int echo)
{
    st_t *T = &trk[t];
    int i, best = -1;
    if (note < 12 || note > 120)
        return;
    for (i = 0; i < NV; i++)                                 /* the same note: that voice again */
        if (T->v[i].on && T->v[i].note == note && !T->v[i].pending && T->v[i].echo == echo) {
            best = i;
            break;
        }
    if (best < 0) {
        int sounding = 0;
        for (i = 0; i < NV; i++)
            sounding += T->v[i].on && !T->v[i].pending && T->v[i].fade <= 0.0f;
        if (sounding < poly_cap)
            for (i = 0; i < NV; i++)
                if (!T->v[i].on) {
                    best = i;
                    break;
                }
    }
    if (best < 0) {                                          /* steal: an echo, a released one, the oldest */
        float q = 1e9f;
        for (i = 0; i < NV; i++) {
            sv_t *v = &T->v[i];
            float score = v->elev + (v->gate ? 10.0f : 0.0f) + (v->echo ? 0.0f : 5.0f) + (float)(v->age) * 1e-6f +
                          (v->pending ? 20.0f : 0.0f);
            if (v->on && score < q) {
                q = score;
                best = i;
            }
        }
    }
    {
        sv_t *v = &T->v[best];
        if (v->on && v->t > 0 && v->note != note) {          /* fade it, then the new note */
            v->fade = 1.0f;
            v->pending = 1;
            v->pend.note = (uint8_t)note;
            v->pend.vel = vel;
            v->pend.acc = (uint8_t)acc;
            v->pend.live = (uint8_t)echo;
            v->pend.gate = gate;
            v->pend.lock[0] = lock ? lock[0] : -1;
            v->pend.lock[1] = lock ? lock[1] : -1;
            return;
        }
        voice_start(T, t, v, note, vel, acc, gate, lock);
        v->echo = (uint8_t)echo;
        v->nchord = 1;
        v->chord[0] = (uint8_t)note;
    }
}

static int con_mono(void) { return en_console(en_con)->mono; }

/* the notes of a step (or the keys held together) */
static void syn_notes_on(int t, const int *notes, int nn, float vel, int acc, int32_t gate, const int16_t *lock, int echo)
{
    int k;
    if (nn < 1)
        return;
    if (con_mono()) {
        int ns[EN_NNOTE], m = 0;
        for (k = 0; k < nn && m < EN_NNOTE; k++)
            if (notes[k] >= 12 && notes[k] <= 120)
                ns[m++] = notes[k];
        if (m == 1 && (((const drv_t *)snd_of_trk(t)->patch)->flags & DF_AUTOARP)) {   /* a lone note: the chord's */
            int pc[4], c, base = ns[0], i;
            if (chord_now >= 0) {
                c = en_chord_tones(chord_now, 0, pc);
                for (i = 0; i < c && m < EN_NNOTE; i++) {
                    int x = base + ((pc[i] - base) % 12 + 12) % 12;
                    if (x != base)
                        ns[m++] = x;
                }
            } else {
                int r = ((base - key_off()) % 12 + 12) % 12, third = (r == 2 || r == 4 || r == 9 || r == 11) ? 3 : 4;
                ns[m++] = base + third;
                ns[m++] = base + (r == 11 ? 6 : 7);
            }
            for (i = 1; i < m; i++) {                        /* in order */
                int x = ns[i], j = i;
                while (j > 0 && ns[j - 1] > x) {
                    ns[j] = ns[j - 1];
                    j--;
                }
                ns[j] = x;
            }
        }
        if (m)
            mono_note_on(t, ns, m, vel, acc, gate, lock, echo);
    } else {
        for (k = 0; k < nn; k++)
            poly_note_on(t, notes[k], vel, acc, gate, lock, echo);
    }
}

/* the echo the trackers made: the step again, so many sixteenths later, quieter (send), and again (feedback) */
static int con_echo_chip(void);
static float echo_samples(void);
static void echo_queue(int t, const int *notes, int nn, float vel, int32_t gate, int rep)
{
    st_t *T = &trk[t];
    float send = (float)en_cfg.tp[t + 1][TP_SEND] * 0.01f;
    int k;
    if (!con_echo_chip() || (send <= 0.0f && en_fx_now != FX_ECHO) || T->eqn >= 8 || rep > 3)
        return;
    if (en_fx_now == FX_ECHO)
        send = fm_maxf(send, 0.7f);
    {
        int i = T->eqn++;
        T->eq[i].at = (int32_t)(echo_samples() * (float)(rep + 1));
        T->eq[i].nn = (uint8_t)(nn > EN_NNOTE ? EN_NNOTE : nn);
        for (k = 0; k < T->eq[i].nn; k++)
            T->eq[i].note[k] = (uint8_t)notes[k];
        T->eq[i].vel = vel * send;
        T->eq[i].gate = gate;
        T->eq[i].rep = (uint8_t)rep;
    }
}
static void echo_run(int t, uint32_t n)
{
    st_t *T = &trk[t];
    int i = 0;
    float fb = en_fx_now == FX_ECHO ? 0.8f : (float)en_cfg.par[P_FX2] * 0.01f;
    while (i < T->eqn) {
        T->eq[i].at -= (int32_t)n;
        if (T->eq[i].at <= 0) {
            int ns[EN_NNOTE], k, nn = T->eq[i].nn, rep = T->eq[i].rep;
            float vel = T->eq[i].vel;
            int32_t gate = T->eq[i].gate;
            for (k = 0; k < nn; k++)
                ns[k] = T->eq[i].note[k];
            T->eq[i] = T->eq[--T->eqn];
            if (vel > 0.04f) {
                syn_notes_on(t, ns, nn, vel, 0, gate, 0, 1);
                if (fb > 0.05f && rep < 3) {           /* the next repeat, further down */
                    float v0 = vel * fb;
                    if (v0 > 0.04f && T->eqn < 8) {
                        int j = T->eqn++;
                        T->eq[j].at = (int32_t)echo_samples();
                        T->eq[j].nn = (uint8_t)nn;
                        for (k = 0; k < nn; k++)
                            T->eq[j].note[k] = (uint8_t)ns[k];
                        T->eq[j].vel = v0;
                        T->eq[j].gate = gate;
                        T->eq[j].rep = (uint8_t)(rep + 1);
                    }
                }
            }
        } else {
            i++;
        }
    }
}

static void syn_note_off(int t, int note)                    /* a key let go */
{
    st_t *T = &trk[t];
    int i;
    if (con_mono()) {                                        /* mono: back to the last key still held */
        sv_t *v = &T->v[0];
        if (v->on && v->gate_left < 0 && (v->note == note || v->nchord > 1)) {
            if (T->held_n) {
                int back = T->held_snd[T->held_n - 1];
                v->tgt = (float)back;
                v->gl = glide_of(t, 1);
                v->note = (uint8_t)back;
                v->nchord = 1;
                v->chord[0] = (uint8_t)back;
                T->last_pitch = (float)back;
            } else {
                v->gate = 0;
            }
        }
        return;
    }
    for (i = 0; i < NV; i++) {
        sv_t *v = &T->v[i];
        if (v->on && v->gate_left < 0 && v->note == note && !v->echo)
            v->gate = 0;
        if (v->pending && v->pend.note == note && v->pend.gate < 0)
            v->pend.gate = 1;
    }
}

/* the track's sound changed: everything it was playing stops */
static void trk_sound(int t, int snd)
{
    st_t *T = &trk[t];
    int i;
    T->snd = (int8_t)snd;
    for (i = 0; i < NV; i++) {
        T->v[i].on = 0;
        T->v[i].pending = 0;
        T->v[i].elev = 0.0f;
    }
    T->eqn = 0;
}

static void trk_quiet(int t)
{
    st_t *T = &trk[t];
    int i;
    for (i = 0; i < NV; i++) {
        T->v[i].gate = 0;
        T->v[i].pending = 0;
        if (T->v[i].gate_left < 0)
            T->v[i].gate_left = 0;
    }
    T->eqn = 0;
}

static void trk_reset(int t)                                 /* at once: a new console */
{
    st_t *T = &trk[t];
    int i;
    for (i = 0; i < NV; i++) {
        T->v[i].on = 0;
        T->v[i].pending = 0;
        T->v[i].elev = 0.0f;
    }
    T->eqn = 0;
    T->held_n = 0;
    T->last_pitch = 0.0f;
    T->snd = (int8_t)(en_cfg.tp[t + 1][TP_SOUND] % EN_NSOUND);
}

/* a track's chunk (n samples at the console's rate): its voices into T->out (mono), then into l, r at its
 * pan; 0 if it was silent (and l, r untouched) */
static float pan_of(int t);
static int trk_run(int t, uint32_t n, float *l, float *r, float gain)
{
    st_t *T = &trk[t];
    int i, snd = en_cfg.tp[t + 1][TP_SOUND] % EN_NSOUND, any = 0;
    uint32_t j;
    float pan = pan_of(t), gl = gain * (pan > 0.0f ? 1.0f - pan : 1.0f), gr = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    if (snd != T->snd)
        trk_sound(t, snd);
    echo_run(t, (uint32_t)((float)n / ratio));   /* (in the output's samples, as the sequencer counts) */
    for (i = 0; i < NV; i++)
        any |= T->v[i].on;
    if (!any) {
        en_level[t + 1] *= 0.97f;
        return 0;
    }
    if (poly_cap < NV) {               /* the governor's cap holds the ringing ones too: the quietest go first */
        int on = 0;
        for (i = 0; i < NV; i++)
            on += T->v[i].on && T->v[i].fade <= 0.0f;
        while (on > poly_cap) {
            int best = -1;
            float q = 1e9f;
            for (i = 0; i < NV; i++) {
                sv_t *v = &T->v[i];
                float score = v->elev + (v->gate ? 10.0f : 0.0f) + (float)(v->age) * 1e-6f;
                if (v->on && v->fade <= 0.0f && score < q) {
                    q = score;
                    best = i;
                }
            }
            if (best < 0)
                break;
            T->v[best].fade = 1.0f;
            T->v[best].pending = 0;
            on--;
        }
    }
    for (j = 0; j < n; j++)
        T->out[j] = T->alt[j] = 0.0f;
    alt_out = T->alt;
    alt_used = 0;
    for (i = 0; i < NV; i++) {
        sv_t *v = &T->v[i];
        if (!v->on)
            continue;
        if (v->fade > 0.0f) {                                    /* stolen: faded out over 1.5 ms, then the new */
            uint32_t m = (uint32_t)(v->fade * 48.0f) + 1u;      /* (only what the fade still lets be heard) */
            if (m > n)
                m = n;
            for (j = 0; j < m; j++)
                fade_buf[j] = 0.0f;
            voice_run(v, m, fade_buf, 0);
            for (j = 0; j < m; j++) {
                float g = v->fade - (float)j * (1.0f / 48.0f);
                T->out[j] += fade_buf[j] * (g > 0.0f ? g : 0.0f);
            }
            v->fade -= (float)n * (1.0f / 48.0f);
            if (v->fade <= 0.0f) {
                v->fade = 0.0f;
                v->elev = 0.0f;
                if (v->pending) {
                    int16_t lk[2];
                    uint8_t echo = v->pend.live;
                    lk[0] = v->pend.lock[0];
                    lk[1] = v->pend.lock[1];
                    voice_start(T, t, v, v->pend.note, v->pend.vel, v->pend.acc, v->pend.gate, lk);
                    v->echo = echo;
                    v->nchord = 1;
                    v->chord[0] = v->note;
                } else {
                    v->on = 0;
                }
            }
            continue;
        }
        voice_run(v, n, T->out, T->alt);
        if (v->gate_left > 0) {
            v->gate_left -= (int32_t)((float)n / ratio);
            if (v->gate_left <= 0) {
                v->gate_left = 0;
                v->gate = 0;
            }
        }
        if (v->done)
            v->on = 0;
    }
    for (j = 0; j < n; j++) {
        l[j] += T->out[j] * gl;
        r[j] += T->out[j] * gr;
    }
    if (alt_used) {                    /* the other side: hard over from the track's (right, if it is centred) */
        float al = pan > 0.0f ? gain : pan < 0.0f ? 0.0f : 0.0f, ar = pan > 0.0f ? 0.0f : gain;
        for (j = 0; j < n; j++) {
            l[j] += T->alt[j] * al;
            r[j] += T->alt[j] * ar;
        }
    }
    alt_out = 0;
    {
        float pk = 0.0f;
        for (j = 0; j < n; j += 4)
            pk = fm_maxf(pk, fm_fabsf(T->out[j]));
        en_level[t + 1] = fm_maxf(pk * gain * 1.5f, en_level[t + 1] * 0.97f);
    }
    return 1;
}

/* ---------------------------------------------------------------- the drums --- */
typedef struct {
    const char *s, *l;                 /* its short and long names */
    const void *patch;                 /* the engine's patch (its driver part first) */
    uint8_t note;                      /* the pitch it plays at (Tune moves it) */
    int8_t pan;                        /* -100 .. 100 */
    uint8_t choke;                     /* a group: a part cuts the others of its group (the hats) */
} dpart_t;
typedef struct {
    const char *name;
    dpart_t p[EN_NPART];
} dkit_t;
static const dkit_t *kit_of(int con, int k);

const char *const EN_DRUM_KNOB[4] = {"Tune", "Decay", "Tone", "Level"};
void en_drum_text(int k, int v, char *b)          /* tune in semitones, the rest 0..100 */
{
    int n = 0;
    char t[4];
    if (k == 0) {
        v = (v - 50) * 12 / 50;
        if (v > 0)
            *b++ = '+';
        else if (v < 0) {
            *b++ = '-';
            v = -v;
        }
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 3);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
void en_drum_def(int con, int kit, int part, uint8_t *k4)
{
    (void)con;
    (void)kit;
    (void)part;
    k4[0] = 50;
    k4[1] = 50;
    k4[2] = 50;
    k4[3] = 80;
}
const char *en_kit_name(int con, int kit) { return kit_of(con, kit)->name; }
const char *en_part_name(int con, int kit, int part) { return kit_of(con, kit)->p[(unsigned)part % EN_NPART].s; }
const char *en_part_long(int con, int kit, int part) { return kit_of(con, kit)->p[(unsigned)part % EN_NPART].l; }

/* a drum part's knobs reach its voice as the driver's: Decay scales the envelope, Tone is the patch's own */
static void drum_fire(int kit, int p, float vel, int tune_off, int dec_off)
{
    const dpart_t *dp = &kit_of(en_con, kit)->p[p];
    const uint8_t *k = en_cfg.dk[kit][p];
    sv_t *v = &dv[p];
    int q;
    float tune = (float)((int)k[0] - 50) * 0.24f + (float)tune_off * 0.24f;
    if (dp->choke)                                    /* the closed hat cuts the open one */
        for (q = 0; q < EN_NPART; q++)
            if (q != p && kit_of(en_con, kit)->p[q].choke == dp->choke && dv[q].on)
                dv[q].fade = 1.0f;
    v->trk = -1;
    v->part = (uint8_t)p;
    v->patch = dp->patch;
    v->d = (const drv_t *)dp->patch;
    voice_init(v, dp->note, vel * (0.2f + 0.8f * (float)k[3] * 0.01f) * 1.25f, vel > 0.9f, -1, 0, 0);
    v->tgt = v->pitch = (float)dp->note + tune;
    v->gl = 1.0f;
    v->k[0] = (float)k[0] * 0.01f;
    v->k[1] = fm_clampf(((float)k[1] + (float)dec_off) * 0.01f, 0.0f, 1.0f);
    v->k[2] = (float)k[2] * 0.01f;
    v->k[3] = (float)k[3] * 0.01f;
    v->pan = (float)dp->pan * 0.01f;
    v->nchord = 1;
    v->chord[0] = dp->note;
    v->echo = 0;
    eng->start(v, 0);
    v->fade = 0.0f;
}

static int drums_run(uint32_t n, float *outl, float *outr, int pan_ok)
{
    int p, any = 0;
    uint32_t j;
    for (p = 0; p < EN_NPART; p++)
        any |= dv[p].on;
    if (!any)
        return 0;
    for (j = 0; j < n; j++)
        outl[j] = outr[j] = 0.0f;
    for (p = 0; p < EN_NPART; p++) {
        sv_t *v = &dv[p];
        float pan, gl, gr;
        if (!v->on)
            continue;
        for (j = 0; j < n; j++)
            fade_buf[j] = 0.0f;
        {   /* the driver sees the knobs the part has: k[] were set when it fired; keep them */
            float k0 = v->k[0], k1 = v->k[1], k2 = v->k[2], k3 = v->k[3];
            uint32_t m = n;
            if (v->fade > 0.0f) {                         /* choked: only what the fade still lets be heard */
                m = (uint32_t)(v->fade * 96.0f) + 1u;
                if (m > n)
                    m = n;
            }
            voice_run(v, m, fade_buf, 0);
            v->k[0] = k0;
            v->k[1] = k1;
            v->k[2] = k2;
            v->k[3] = k3;
        }
        if (v->fade > 0.0f) {                             /* choked: out in a few ms */
            for (j = 0; j < n; j++) {
                float g = v->fade - (float)j * (1.0f / 96.0f);
                fade_buf[j] *= g > 0.0f ? g : 0.0f;
            }
            v->fade -= (float)n * (1.0f / 96.0f);
            if (v->fade <= 0.0f)
                v->done = 1;
        }
        pan = pan_ok ? v->pan : 0.0f;
        gl = pan > 0.0f ? 1.0f - pan : 1.0f;
        gr = pan < 0.0f ? 1.0f + pan : 1.0f;
        for (j = 0; j < n; j++) {
            outl[j] += fade_buf[j] * gl;
            outr[j] += fade_buf[j] * gr;
        }
        if (v->done)
            v->on = 0;
    }
    return 1;
}

static void drums_quiet(void)
{
    int p;
    for (p = 0; p < EN_NPART; p++)
        dv[p].on = 0;
}
