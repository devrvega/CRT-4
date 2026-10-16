/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: a four-track groovebox that plays the sound chips of nine game consoles and home
 * computers. One console at a time: its drums on track 1, any of its sixteen sounds on tracks 2-4, made the
 * way that console made them (its channels, its sample format and rate, its interpolation, its envelopes,
 * its own echo or reverb). Each console keeps its own song: sixteen 16-step patterns holding all four
 * tracks, chains of patterns, chords the tracks can follow, punch-in effects.
 *
 *   ATARI 2600   the TIA: two channels of its sixteen waveforms (dividers and polynomial counters), so its
 *                notes are only near the scale, as the console's were
 *   C64          the SID (6581): triangle, saw, pulse, noise and their combinations, ring and sync, its ADSR
 *                and its multimode filter; 4-bit digis
 *   NES          the 2A03 (two pulses, the triangle, the noise, the 1-bit DMC) and the Famicom's expansion
 *                chips (VRC6, FDS, N163, 5B); its non-linear mix and output filters
 *   PC ENGINE    the HuC6280: six 32-step 5-bit wavetables, noise, the LFO, 5-bit DDA; the CD-ROM's ADPCM
 *   GENESIS      the YM2612 (four-operator FM, eight algorithms, its 9-bit DAC and its ladder) and the
 *                SN76489 (three squares, a noise); 8-bit DAC drums
 *   GAME BOY     two pulses, the 4-bit wave channel, the noise; hard left / right; its high-pass
 *   SNES         the S-DSP: BRR samples at 32 kHz, Gaussian interpolation, ADSR and GAIN, echo through
 *                its 8-tap FIR
 *   PLAYSTATION  the SPU: ADPCM samples at 44.1 kHz, Gaussian interpolation, its ADSR, its reverb
 *   N64          ADPCM samples mixed at 22 kHz (or 32), its reverbs, chorus and flange
 *
 * Mono consoles (2600, C64, NES, Game Boy) play a chord as the composers of the time did: an arpeggio, a
 * note a frame. The samples are made by tools/bank (no recordings), stored in each console's own format.
 *
 * Threads: en_render() runs in the audio interrupt. The patterns and the settings (en_cfg, en_pat) are
 * shared: the main loop writes them, the render reads them (a byte or a word at a time). Everything else
 * is a command, drained at the start of a block; the state marked (UI) is written by the render and only
 * read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTEP 16                    /* a pattern: 16 sixteenths (fewer with Length) */
#define EN_NTRACK 4                    /* the drums, then three tracks */
#define EN_NSYN 3
#define EN_NPAT 16
#define EN_CHAIN_MAX 64
#define EN_NPART 11                    /* a kit's parts: the black keys */
#define EN_NKIT 2                      /* kits a console */
#define EN_NSOUND 16                   /* sounds a console: bass, keys, leads, its own (four each) */
#define EN_NFX 16
#define EN_NCHORD 16
#define EN_NNOTE 4                     /* notes in a step (a chord) */
#define EN_NOLOCK 0xFFu

extern const char *const EN_NOTE_NAME[12];

/* --------------------------------------------------------------- the consoles --- */
#define EN_NCON 9
enum { CON_2600, CON_C64, CON_NES, CON_PCE, CON_GEN, CON_GB, CON_SNES, CON_PS1, CON_N64 };
enum { PAN_MONO, PAN_LCR, PAN_FULL };  /* what the console's output could do with a channel */
typedef struct {
    const char *name;                  /* "NES" (the screen's, in capitals) */
    const char *full;                  /* "Nintendo Entertainment System" */
    const char *chip;                  /* "Ricoh 2A03" */
    uint16_t year;
    uint8_t mono;                      /* 1: a track plays one note at a time; chords are arpeggios */
    uint8_t pan;                       /* PAN_* */
    const char *fx_knob[4];            /* the FX page's four knobs */
    uint8_t fx_def[4];
    uint8_t fx_zones[4];               /* a knob that is a switch: its positions (0: continuous 0..100) */
} en_console_t;
const en_console_t *en_console(int con);
extern volatile uint8_t en_con;        /* the console playing (the app sets it with en_set_console) */
void en_fx_text(int con, int k, int v, char *buf);   /* an FX knob as the screen shows it (10 bytes) */

/* the sounds: each console's sixteen, in rows of four (bass, keys, leads, its own), and two kits */
typedef struct {
    const char *name;                  /* "Triangle" */
    const char *after;                 /* what it is made of: "2A03 triangle" */
    const char *knob[4];               /* its four knobs (A and B can be locked on a step) */
    uint8_t def[4];
    uint8_t zones[4];                  /* a knob that is a switch: its positions (0: continuous) */
    const void *patch;                 /* the console engine's own description */
} en_sound_t;
const en_sound_t *en_sound(int con, int s);
const char *en_kit_name(int con, int kit);
const char *en_part_name(int con, int kit, int part);   /* "BD" (short, for the screen) */
const char *en_part_long(int con, int kit, int part);   /* "Bass drum" */
extern const char *const EN_DRUM_KNOB[4];               /* the parts' knobs: Tune, Decay, Tone, Level */
void en_sound_text(int con, int snd, int k, int v, char *buf);   /* a sound's knob as the screen shows it */
int en_sound_zones(int con, int snd, int k);
void en_drum_text(int k, int v, char *buf);
void en_drum_def(int con, int kit, int part, uint8_t *k4);       /* a part's knobs as the kit starts */

/* the punch-in effects (1..16; 0 none), on the white keys with FX */
enum { FX_NONE, FX_RPT16, FX_RPT8, FX_RPT4, FX_STUTTER, FX_REVERSE, FX_HALF, FX_GLITCH, FX_FILL, FX_OCTUP,
       FX_OCTDN, FX_SOLODRUM, FX_SOLOSYN, FX_FADE, FX_DIVE, FX_VIBRATO, FX_ECHO };
extern const char *const EN_FX_NAME[EN_NFX + 1];

/* the chords (on the white keys with ARP): in C, moved by Key */
typedef struct {
    const char *name;                  /* in C: "Dm7" */
    uint8_t root;                      /* pitch class */
    uint8_t q;                         /* CQ_* */
} en_chord_t;
enum { CQ_MAJ, CQ_MIN, CQ_DIM, CQ_SUS4, CQ_DOM7, CQ_MAJ7, CQ_MIN7 };
extern const en_chord_t EN_CHORD[EN_NCHORD];
void en_chord_name(int c, int key, char *buf);     /* "F#m7" (8 bytes) */

/* ------------------------------------------------------------ the settings --- */
/* a song's parameters */
enum {
    P_TEMPO, P_SWING, P_KEY,                       /* the Patterns page */
    P_FX1, P_FX2, P_FX3, P_FX4,                    /* the FX page: the console's effect (en_console()->fx_knob) */
    P_NPARAMS
};
/* per track */
enum { TP_SOUND, TP_LEVEL, TP_PAN, TP_SEND, TP_GLIDE, TP_FOLLOW, TP_N };
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
const en_param_t *en_tparam_info(int i);
void en_param_text(int i, int v, char *buf);       /* the value as the screen shows it (10 bytes) */
void en_tparam_text(int t, int i, int v, char *buf);

/* a console's song: everything but the patterns */
typedef struct {
    int16_t par[P_NPARAMS];
    uint8_t tp[EN_NTRACK][TP_N];
    uint8_t sk[EN_NSYN][EN_NSOUND][4];             /* every track's knobs, for every sound (0..100) */
    uint8_t dk[EN_NKIT][EN_NPART][4];              /* both kits' parts: tune, decay, tone, level (0..100) */
    uint8_t chain[EN_CHAIN_MAX], chain_n;          /* the pattern chain (chain_n 0: the pattern alone) */
    uint8_t chords[EN_CHAIN_MAX], chords_n;        /* the chord chain, a chord a pattern (0: none) */
    uint8_t pat;                                   /* the pattern selected */
    uint8_t mute;                                  /* bit t: track t muted */
    uint8_t con;                                   /* the console it is for */
    uint8_t rsv[5];
} en_cfg_t;
extern en_cfg_t en_cfg;

/* the settings every console shares (Setup) */
enum { G_TUNE, G_MIDI, G_CLOCK, G_N };
typedef struct {
    int16_t v[G_N];
} en_glob_t;
extern en_glob_t en_glob;
const en_param_t *en_glob_info(int i);
void en_glob_text(int i, int v, char *buf);

/* ------------------------------------------------------------ the patterns --- */
enum { EN_F_ACC = 1, EN_F_SLIDE = 2 };             /* a step's flags; bits 4-5: the ratchet (x1..x4) */
#define EN_RATCHET(fl) ((((fl) >> 4) & 3u) + 1u)
typedef struct {
    uint8_t note[EN_NNOTE];            /* MIDI notes, ascending, 0 = none; note[0] == 0: the step is empty */
    uint8_t len;                       /* the gate, in quarter steps (1..64) */
    uint8_t fl;
    uint8_t lock[2];                   /* knob A and B on this step (0..100), EN_NOLOCK: the knob's own */
} en_step_t;
typedef struct {
    uint16_t hit[EN_NPART];            /* the drums: bit s = step s */
    uint16_t acc;                      /* the drums' accents */
    uint8_t dlock[2][EN_NSTEP];        /* the drums' tune and decay on a step: 50 is none, EN_NOLOCK none */
    uint8_t fx[EN_NSTEP];              /* effects written on steps (FX_*) */
    uint8_t len;                       /* steps in the pattern, 1..16 */
    uint8_t rsv[3];
    en_step_t syn[EN_NSYN][EN_NSTEP];
} en_pat_t;
extern en_pat_t en_pat[EN_NPAT];

void en_cfg_defaults(int con);         /* a console's empty song */
void en_glob_defaults(void);
void en_pat_clear(en_pat_t *p);
int en_cfg_valid(const en_cfg_t *c);
int en_pat_valid(const en_pat_t *p);

/* ---------------------------------------------------------------- commands --- */
/* (main loop -> render) */
void en_init(void);                    /* the settings and patterns are the caller's */
void en_set_console(int con);          /* stopped, quiet: the console whose song en_cfg now holds */
void en_play(int on);                  /* run from the chain's top (or the pattern) / stop */
void en_pattern(int p);                /* play pattern p next: at the end of this one, at once if stopped */
void en_chain_restart(void);           /* the chain (just edited) from its top, at the end of this pattern */
void en_note(int trk, int note, int on, int acc);   /* a track's key (recorded while writing) */
void en_hit(int part, int acc);        /* a drum part (recorded while writing) */
void en_rec(int on);                   /* write: keys, knobs A and B and effects go into the pattern */
void en_fx(int fx);                    /* the punch-in effect held (0: none) */
void en_knob(int trk, int k);          /* knob A or B (k 0, 1) of a track just turned: written while writing */
void en_panic(void);
/* MIDI clock in: ticks drive the 1/16 grid (6 a step) while they keep coming */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* ------------------------------------------------------ state for the screen --- */
extern volatile uint8_t en_playing, en_ext, en_recording;
extern volatile uint8_t en_step;                   /* the step playing (after any effect) */
extern volatile uint8_t en_tick;                   /* 0..23 in the step */
extern volatile uint8_t en_pat_now, en_pat_next;   /* the pattern playing; the one that follows */
extern volatile uint8_t en_chain_pos;              /* where in the chain */
extern volatile uint8_t en_chord_now;              /* the chord playing (0xFF none) */
extern volatile uint8_t en_fx_now;                 /* the effect sounding (held or written) */
extern volatile uint32_t en_steps;                 /* steps played since start (for the scenes, the tests) */
extern volatile uint8_t en_trk_on[EN_NTRACK];      /* counts each track's notes (the scenes move on a change) */
extern volatile uint8_t en_trk_note[EN_NTRACK];    /* the last note played (drums: the part) */
extern volatile uint8_t en_trk_acc[EN_NTRACK];     /* ... and whether accented */
extern volatile uint8_t en_gates;                  /* bit t: a note of track t is held */
extern volatile uint8_t en_part_on[EN_NPART];      /* counts each drum part's hits */
extern volatile float en_level[EN_NTRACK];         /* each track's loudness now, 0..1 (meters) */
extern volatile uint32_t en_rec_count;             /* notes written live (tests) */
/* where the render's time goes (the device's SysEx reports it): a clock the app may give, the time spent in
 * each part since it was last cleared, in its ticks */
enum { PR_SEQ, PR_DRUMS, PR_TRK1, PR_TRK2, PR_TRK3, PR_SENDS, PR_MASTER, PR_OTHER, PR_N };
extern uint32_t (*en_time)(void);
extern volatile uint32_t en_prof[PR_N];
#ifdef CRT4_BENCH
extern volatile uint32_t en_prof_worst[PR_N + 1];
#endif

/* the pieces of the instrument the tests and the screen use */
int en_transpose(int trk, int note);   /* a written note as it sounds now (key, chord, effects) */
int en_chord_tones(int c, int key, int *pc);   /* chord c's pitch classes (3 or 4) */
void en_demo(int con);                 /* fills en_cfg and the patterns with the console's demo song */
extern const char *const EN_DEMO_NAME[EN_NCON];
uint32_t en_bank_bytes(int con);       /* the console's sample bank in flash (0: none) */
