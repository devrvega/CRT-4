/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's sample bank: every sample the consoles play, made here (no recordings) and written as C in each
 * console's own format, sized as its games would have had them.
 *
 *   cc -O2 -o gen_bank tools/bank/gen_bank.c -lm && ./gen_bank build/gen/crt4_bank.h
 *
 * A melodic sample is its attack and then a loop of a whole number of periods that is also a whole number
 * of the format's blocks (the sample's rate is chosen to make both true, as the consoles' sound designers
 * did); the console's envelope does the rest. A drum plays once. Each sample is made at 48 kHz, cut to its
 * length, resampled to its rate, normalized, encoded, and decoded again to check it. */
#include "dsp.h"
#include "drums.h"
#include "inst.h"
#include "enc.h"

enum { BF_PCM8, BF_PCM4, BF_PCM5, BF_DMC1, BF_DPCM4, BF_BRR, BF_SPU, BF_VAD, BF_OKI };
static const char *const FMT_NAME[] = {"BF_PCM8", "BF_PCM4", "BF_PCM5", "BF_DMC1", "BF_DPCM4", "BF_BRR", "BF_SPU", "BF_VAD", "BF_OKI"};
static const int FMT_BLOCK[] = {1, 2, 1, 8, 2, 16, 28, 16, 2};
enum { C_2600, C_C64, C_NES, C_PCE, C_GEN, C_GB, C_SNES, C_PS1, C_N64, C_N };
static const char *const CON_NAME[C_N] = {"2600", "C64", "NES", "PC Engine", "Genesis", "Game Boy", "SNES", "PlayStation", "N64"};
static const int CON_BUDGET[C_N] = {0, 10000, 12000, 28000, 22000, 12000, 70000, 100000, 76000};

typedef buf_t (*maker_t)(double f);
typedef struct {
    const char *id;
    int con, fmt;
    double rate;                       /* the rate wanted (a looped sample's is moved a little to fit) */
    int root;                          /* the MIDI note it is made at */
    double att;                        /* seconds before the loop (or kept, for a one-shot) */
    double loop;                       /* the loop's length wanted, seconds (0: plays once) */
    double gain;                       /* its peak */
    maker_t make;
} sdef_t;

/* ------------------------------------------------------------- the sources --- */
#define DRUM(name, expr) \
    static buf_t name(double f) \
    { \
        (void)f; \
        return expr; \
    }
/* analog machines: the 808, the 909 (the house kits of the 16-bit games came from these) */
DRUM(k808, d_kick(130, 48, 0.035, 0.6, 0.15, 0.1))
DRUM(k909, d_kick(260, 52, 0.022, 0.38, 0.6, 0.35))
DRUM(sn808, d_snare(185, 330, 0.12, 0.2, 0.8, 1800))
DRUM(sn909, d_snare(195, 345, 0.1, 0.24, 1.1, 2400))
DRUM(tom909, d_tom(130, 0.35, 0.25, 0.25))
DRUM(rim808, d_rim(1667))
DRUM(clap808, d_clap(0.25, 4))
DRUM(chat808, d_hat(0.05, 7600, 0.25))
DRUM(ohat808, d_hat(0.4, 7600, 0.25))
DRUM(chat909, d_hat(0.06, 8800, 0.55))
DRUM(ohat909, d_hat(0.45, 8800, 0.55))
DRUM(crash, d_cymbal(1.2, 0.8, 0))
DRUM(ride, d_cymbal(1.4, 0.5, 1))
DRUM(cow808, d_cowbell(540, 800, 0.3))
/* acoustic */
DRUM(kick_ac, d_kick_ac(58, 0.35, 0.8))
DRUM(snare_ac, d_snare_ac(190, 0.25, 0.9))
DRUM(tom_ac, d_tom(120, 0.5, 0.15, 0.35))
DRUM(timp, d_timpani(98, 1.6))
DRUM(cbd, d_concert_bd(48, 1.4))
DRUM(csnare, d_snare_ac(220, 0.2, 1.0))
DRUM(roll, d_roll(0.9))
DRUM(tri_s, d_triangle(1800, 0.25))
DRUM(tri_l, d_triangle(1800, 1.6))
DRUM(piatti, d_cymbal(1.6, 1.0, 0))
DRUM(tamb, d_tamb(0.35))
DRUM(wood, d_claves(1100, 0.06))
DRUM(claves, d_claves(2500, 0.07))
DRUM(conga, d_conga(196, 0.4, 0.0))
DRUM(cslap, d_conga(260, 0.25, 1.0))
DRUM(bongo, d_conga(400, 0.18, 0.4))
DRUM(surdo, d_conga(75, 0.9, 0.0))
DRUM(logd, d_log(330, 0.35))
DRUM(shaker, d_shaker(0.02, 0.12))
DRUM(agogo, d_cowbell(830, 1250, 0.35))
DRUM(burst, d_burst(0.3, 3000))
static buf_t samba_whistle(double f)
{
    buf_t b = whistle(f), c;
    int i;
    (void)c;
    for (i = 0; i < b.n; i++) {        /* the pea: a fast trill */
        double t = (double)i / SR;
        b.x[i] *= 0.6 + 0.4 * sin(TAU * 28.0 * t);
        if (t > 0.35)
            b.x[i] *= exp(-(t - 0.35) / 0.03);
    }
    return b;
}
/* the voices that shout: "hey", "yeah", "go" */
static buf_t hey(double f) { return shout(f, &V_EH, &V_EE, 0.3); }
static buf_t yeah(double f) { return shout(f, &V_EE, &V_AH, 0.35); }
static buf_t ho(double f) { return shout(f, &V_UH, &V_OO, 0.3); }

/* melodic */
static buf_t i_synbass(double f) { return va(f, 1, 0, 0, 220, 6.0, 0.18, 0.35, 0.7, 0.002); }
static buf_t i_synbass64(double f) { return va(f, 2, 0.004, 1, 300, 5.0, 0.25, 0.2, 0.7, 0.004); }
static buf_t i_acid(double f) { return va(f, 1, 0, 0, 260, 9.0, 0.12, 0.8, 0.5, 0.001); }
static buf_t i_reese(double f) { return va(f, 2, 0.006, 0, 700, 0.5, 0.3, 0.1, 0.9, 0.004); }
static buf_t i_square(double f) { return va(f, 1, 0, 1, 5000, 0, 1, 0, 0.6, 0.004); }
static buf_t i_synlead(double f) { return va(f, 2, 0.003, 0, 2600, 1.5, 0.3, 0.3, 0.8, 0.01); }
static buf_t i_synbrass(double f) { return va(f, 3, 0.004, 0, 900, 3.0, 0.35, 0.15, 0.8, 0.04); }
static buf_t i_pad(double f)
{
    buf_t b = va(f, 6, 0.006, 0, 1600, 0.6, 0.6, 0.1, 1.0, 0.25);
    room(&b, 0.35, 1.3);
    return b;
}
static buf_t i_strings(double f) { return strings(f, 0.14, 8); }
static buf_t i_cdstrings(double f) { return strings(f, 0.2, 6); }
static buf_t i_choir(double f) { return choir(f, 8, &V_AH); }
static buf_t i_choir_oo(double f) { return choir(f, 6, &V_OO); }
static buf_t i_brass(double f) { return brass(f, 3, 0.8); }
static buf_t i_brass1(double f) { return brass(f, 2, 0.7); }
static buf_t i_odgtr(double f) { return guitar(f, 6.0, 1.0); }
static buf_t i_distgtr(double f) { return guitar(f, 22.0, 1.0); }
static buf_t i_hit(double f) { return orch_hit(f); }
/* the NES's sampled basses (Sunsoft's way): short loops of a bass's waveform */
static buf_t i_bassw(double f, int kind)
{
    buf_t b = buf_new(0.25);
    double ph = 0;
    int i;
    for (i = 0; i < b.n; i++) {
        double t = (double)i / SR, x, p;
        ph += f / SR;
        p = ph - floor(ph);
        switch (kind) {
        case 0: x = 0.8 * (1.0 - 2.0 * p) + 0.3 * sin(TAU * ph); break;     /* a saw, round */
        case 1: x = (p < 0.5 ? 0.8 : -0.8) * 0.8 + 0.4 * sin(TAU * ph); break;   /* hollow */
        default: x = sin(TAU * ph + 1.6 * sin(TAU * ph) * exp(-t / 0.08)); break;  /* a slap's pluck */
        }
        b.x[i] = x;
    }
    return b;
}
static buf_t i_bassw0(double f) { return i_bassw(f, 0); }
static buf_t i_bassw1(double f) { return i_bassw(f, 1); }
static buf_t i_bassw2(double f) { return i_bassw(f, 2); }

/* the break: a bar of an original funk pattern, played on an acoustic kit in a small room. Its eleven
 * slices are the Jungle kit's parts. */
static int break_slice[12];
static buf_t i_break(double f)
{
    /* sixteenths at 172 bpm; K kick, S snare, s ghost, H hat, O open hat, R ride */
    static const char *const K = "K.K.......K.K...";
    static const char *const S = "....S..s.s..S..s";
    static const char *const H = "HHHHHHHHHHHHHHOH";
    double st = 60.0 / 172.0 / 4.0;
    buf_t b = buf_new(st * 16 + 0.3), x;
    int s;
    (void)f;
    for (s = 0; s < 16; s++) {
        int at = (int)(s * st * SR + (s & 1 ? 0.012 * SR : 0));
        if (K[s] == 'K') {
            x = d_kick_ac(60, 0.3, 0.9);
            buf_add(&b, &x, at, 1.0);
            buf_free(&x);
        }
        if (S[s] != '.') {
            x = d_snare_ac(200, 0.22, 1.0);
            buf_add(&b, &x, at, S[s] == 'S' ? 0.9 : 0.3);
            buf_free(&x);
        }
        if (H[s] != '.') {             /* (darker hats than a drum machine's: the break is kept at 22 kHz) */
            x = H[s] == 'O' ? d_hat(0.3, 5600, 0.6) : d_hat(0.045, 5600, 0.6);
            buf_add(&b, &x, at, H[s] == 'O' ? 0.6 : s % 4 == 0 ? 0.5 : 0.38);
            buf_free(&x);
        }
    }
    room(&b, 0.35, 0.9);
    {   /* the slices: where the hits are */
        static const int SL[11] = {0, 2, 4, 6, 7, 9, 10, 12, 13, 14, 15};
        int k;
        for (k = 0; k < 11; k++)
            break_slice[k] = (int)(SL[k] * st * SR);
        break_slice[11] = b.n;
    }
    return b;
}

/* ----------------------------------------------------------------- the list --- */
#define DR(id, con, fmt, rate, len, make) {id, con, fmt, rate, 60, len, 0, 0.92, make}
#define MEL(id, con, fmt, rate, root, att, loop, make) {id, con, fmt, rate, root, att, loop, 0.92, make}
static const sdef_t S[] = {
    /* NES: 1-bit DMC, at the DMC's fastest rate for drums */
    DR("NES_KICK", C_NES, BF_DMC1, 33144, 0.22, k909),
    DR("NES_SNARE", C_NES, BF_DMC1, 33144, 0.2, sn909),
    DR("NES_TOM", C_NES, BF_DMC1, 24858, 0.3, tom909),
    DR("NES_TIMP", C_NES, BF_DMC1, 21307, 0.5, timp),
    DR("NES_RIM", C_NES, BF_DMC1, 33144, 0.06, rim808),
    DR("NES_CLAP", C_NES, BF_DMC1, 33144, 0.2, clap808),
    MEL("NES_HIT", C_NES, BF_DMC1, 24858, 60, 0.45, 0, i_hit),
    MEL("NES_BASS", C_NES, BF_DMC1, 33144, 45, 0.0, 0.05, i_bassw0),
    MEL("NES_BASS2", C_NES, BF_DMC1, 33144, 45, 0.0, 0.05, i_bassw1),
    MEL("NES_BASS3", C_NES, BF_DMC1, 33144, 45, 0.0, 0.05, i_bassw2),
    /* Game Boy: 4-bit kits for the wave channel (a tracker's kits: 11468 Hz) */
    DR("GB_KICK", C_GB, BF_PCM4, 11468, 0.25, k808),
    DR("GB_SNARE", C_GB, BF_PCM4, 11468, 0.2, sn808),
    DR("GB_TOM", C_GB, BF_PCM4, 11468, 0.3, tom909),
    DR("GB_RIM", C_GB, BF_PCM4, 11468, 0.05, rim808),
    DR("GB_CLAP", C_GB, BF_PCM4, 11468, 0.2, clap808),
    DR("GB_CHAT", C_GB, BF_PCM4, 11468, 0.06, chat808),
    DR("GB_OHAT", C_GB, BF_PCM4, 11468, 0.3, ohat808),
    DR("GB_CRASH", C_GB, BF_PCM4, 11468, 0.4, crash),
    DR("GB_COW", C_GB, BF_PCM4, 11468, 0.2, cow808),
    /* C64: 4-bit digis through the volume register */
    DR("C64_KICK", C_C64, BF_PCM4, 7800, 0.25, k909),
    DR("C64_SNARE", C_C64, BF_PCM4, 7800, 0.22, sn909),
    DR("C64_TOM", C_C64, BF_PCM4, 7800, 0.3, tom909),
    DR("C64_RIM", C_C64, BF_PCM4, 7800, 0.05, rim808),
    DR("C64_CLAP", C_C64, BF_PCM4, 7800, 0.2, clap808),
    DR("C64_CHAT", C_C64, BF_PCM4, 7800, 0.06, chat909),
    DR("C64_OHAT", C_C64, BF_PCM4, 7800, 0.25, ohat909),
    DR("C64_CRASH", C_C64, BF_PCM4, 7800, 0.35, crash),
    MEL("C64_VOICE", C_C64, BF_PCM4, 7800, 57, 0.4, 0, yeah),
    /* PC Engine: 5-bit DDA, and the CD-ROM's ADPCM */
    DR("PCE_KICK", C_PCE, BF_PCM5, 7000, 0.25, k909),
    DR("PCE_SNARE", C_PCE, BF_PCM5, 7000, 0.22, sn909),
    DR("PCE_TOM", C_PCE, BF_PCM5, 7000, 0.3, tom_ac),
    DR("PCE_RIM", C_PCE, BF_PCM5, 7000, 0.05, rim808),
    DR("PCE_CLAP", C_PCE, BF_PCM5, 7000, 0.2, clap808),
    DR("PCE_CHAT", C_PCE, BF_PCM5, 7000, 0.06, chat909),
    DR("PCE_OHAT", C_PCE, BF_PCM5, 7000, 0.25, ohat909),
    DR("PCE_CRASH", C_PCE, BF_PCM5, 7000, 0.4, crash),
    MEL("PCE_VOICE", C_PCE, BF_PCM5, 7000, 55, 0.35, 0, ho),
    MEL("PCE_CDSTR", C_PCE, BF_OKI, 16000, 60, 0.3, 0.25, i_cdstrings),
    MEL("PCE_CDGTR", C_PCE, BF_OKI, 16000, 52, 0.35, 0.12, i_distgtr),
    MEL("PCE_CDCHOIR", C_PCE, BF_OKI, 16000, 60, 0.3, 0.25, i_choir),
    /* Genesis: the DAC, 4-bit deltas at 13 kHz (the drivers of the time halved their drums so) */
    DR("GEN_KICK", C_GEN, BF_DPCM4, 13000, 0.28, k909),
    DR("GEN_SNARE", C_GEN, BF_DPCM4, 13000, 0.24, sn909),
    DR("GEN_TOM", C_GEN, BF_DPCM4, 13000, 0.32, tom909),
    DR("GEN_RIM", C_GEN, BF_DPCM4, 13000, 0.05, rim808),
    DR("GEN_CLAP", C_GEN, BF_DPCM4, 13000, 0.22, clap808),
    DR("GEN_CHAT", C_GEN, BF_DPCM4, 13000, 0.06, chat909),
    DR("GEN_OHAT", C_GEN, BF_DPCM4, 13000, 0.3, ohat909),
    DR("GEN_CRASH", C_GEN, BF_DPCM4, 13000, 0.45, crash),
    DR("GEN_COW", C_GEN, BF_DPCM4, 13000, 0.2, cow808),
    MEL("GEN_HIT", C_GEN, BF_DPCM4, 13000, 60, 0.45, 0, i_hit),
    MEL("GEN_VOICE", C_GEN, BF_DPCM4, 13000, 57, 0.33, 0, hey),
    /* SNES: BRR; 64 KiB of sound memory held the songs' samples, so: short, low rates, tight loops */
    DR("SN_KICK", C_SNES, BF_BRR, 14000, 0.22, kick_ac),
    DR("SN_SNARE", C_SNES, BF_BRR, 16000, 0.2, snare_ac),
    DR("SN_TOM", C_SNES, BF_BRR, 12000, 0.3, tom_ac),
    DR("SN_RIM", C_SNES, BF_BRR, 16000, 0.05, rim808),
    DR("SN_CLAP", C_SNES, BF_BRR, 14000, 0.18, clap808),
    DR("SN_CHAT", C_SNES, BF_BRR, 22000, 0.05, chat909),
    DR("SN_OHAT", C_SNES, BF_BRR, 20000, 0.22, ohat909),
    DR("SN_CRASH", C_SNES, BF_BRR, 16000, 0.4, crash),
    DR("SN_RIDE", C_SNES, BF_BRR, 16000, 0.3, ride),
    DR("SN_CBD", C_SNES, BF_BRR, 10000, 0.5, cbd),
    DR("SN_CSNARE", C_SNES, BF_BRR, 16000, 0.18, csnare),
    DR("SN_TIMP", C_SNES, BF_BRR, 10000, 0.45, timp),
    DR("SN_WOOD", C_SNES, BF_BRR, 16000, 0.05, wood),
    DR("SN_ROLL", C_SNES, BF_BRR, 12000, 0.5, roll),
    DR("SN_TRI", C_SNES, BF_BRR, 18000, 0.3, tri_l),
    DR("SN_PIATTI", C_SNES, BF_BRR, 16000, 0.4, piatti),
    DR("SN_TAMB", C_SNES, BF_BRR, 20000, 0.2, tamb),
    MEL("SN_SLAP", C_SNES, BF_BRR, 16000, 40, 0.12, 0.05, slap_bass),
    MEL("SN_FINGER", C_SNES, BF_BRR, 12000, 40, 0.1, 0.05, finger_bass),
    MEL("SN_SYNBASS", C_SNES, BF_BRR, 12000, 40, 0.08, 0.05, i_synbass),
    MEL("SN_PIZZ", C_SNES, BF_BRR, 16000, 55, 0.18, 0.04, pizzicato),
    MEL("SN_STRINGS", C_SNES, BF_BRR, 16000, 60, 0.2, 0.1, i_strings),
    MEL("SN_CHOIR", C_SNES, BF_BRR, 16000, 60, 0.18, 0.1, i_choir),
    MEL("SN_PIANO", C_SNES, BF_BRR, 18000, 60, 0.25, 0.04, piano),
    MEL("SN_EPIANO", C_SNES, BF_BRR, 16000, 60, 0.2, 0.04, fm_epiano),
    MEL("SN_FLUTE", C_SNES, BF_BRR, 16000, 72, 0.12, 0.06, flute),
    MEL("SN_BRASS", C_SNES, BF_BRR, 16000, 60, 0.12, 0.06, i_brass),
    MEL("SN_ODGTR", C_SNES, BF_BRR, 16000, 52, 0.15, 0.05, i_odgtr),
    MEL("SN_SQUARE", C_SNES, BF_BRR, 16000, 60, 0.02, 0.02, i_square),
    MEL("SN_HIT", C_SNES, BF_BRR, 12000, 60, 0.35, 0, i_hit),
    MEL("SN_HARP", C_SNES, BF_BRR, 18000, 60, 0.2, 0.04, harp),
    MEL("SN_MARIMBA", C_SNES, BF_BRR, 16000, 60, 0.3, 0, marimba),
    MEL("SN_STEEL", C_SNES, BF_BRR, 18000, 60, 0.25, 0.04, steel_drum),
    /* PlayStation: SPU ADPCM; 512 KiB of sound memory: longer, brighter */
    MEL("PS_BREAK", C_PS1, BF_SPU, 22050, 60, 0, 0, i_break),
    DR("PS_KICK", C_PS1, BF_SPU, 22050, 0.3, k909),
    DR("PS_SNARE", C_PS1, BF_SPU, 22050, 0.25, snare_ac),
    DR("PS_TOM", C_PS1, BF_SPU, 18000, 0.3, tom_ac),
    DR("PS_RIM", C_PS1, BF_SPU, 22050, 0.05, rim808),
    DR("PS_CLAP", C_PS1, BF_SPU, 22050, 0.22, clap808),
    DR("PS_CHAT", C_PS1, BF_SPU, 32000, 0.05, chat909),
    DR("PS_OHAT", C_PS1, BF_SPU, 24000, 0.25, ohat909),
    DR("PS_CRASH", C_PS1, BF_SPU, 20000, 0.45, crash),
    DR("PS_RIDE", C_PS1, BF_SPU, 22050, 0.35, ride),
    MEL("PS_REESE", C_PS1, BF_SPU, 18000, 40, 0.05, 0.25, i_reese),
    MEL("PS_SUB", C_PS1, BF_SPU, 11025, 36, 0.01, 0.04, sub_bass),
    MEL("PS_FRETLESS", C_PS1, BF_SPU, 16000, 40, 0.25, 0.05, fretless),
    MEL("PS_ACID", C_PS1, BF_SPU, 22050, 45, 0.3, 0, i_acid),
    MEL("PS_STRINGS", C_PS1, BF_SPU, 22050, 60, 0.2, 0.12, i_strings),
    MEL("PS_CHOIR", C_PS1, BF_SPU, 22050, 60, 0.2, 0.12, i_choir),
    MEL("PS_ORGAN", C_PS1, BF_SPU, 22050, 48, 0.15, 0.1, organ),
    MEL("PS_RHODES", C_PS1, BF_SPU, 22050, 60, 0.25, 0.05, rhodes),
    MEL("PS_SYNLEAD", C_PS1, BF_SPU, 22050, 60, 0.05, 0.05, i_synlead),
    MEL("PS_DISTGTR", C_PS1, BF_SPU, 22050, 52, 0.2, 0.06, i_distgtr),
    MEL("PS_BRASS", C_PS1, BF_SPU, 22050, 60, 0.15, 0.06, i_brass),
    MEL("PS_PANFLUTE", C_PS1, BF_SPU, 22050, 72, 0.15, 0.08, pan_flute),
    MEL("PS_HARPSI", C_PS1, BF_SPU, 22050, 60, 0.25, 0.04, harpsichord),
    MEL("PS_PAD", C_PS1, BF_SPU, 18000, 60, 0.3, 0.2, i_pad),
    MEL("PS_SITAR", C_PS1, BF_SPU, 22050, 57, 0.35, 0.05, sitar),
    MEL("PS_VOICE", C_PS1, BF_SPU, 22050, 64, 0.2, 0.12, solo_voice),
    /* N64: its ADPCM kind; mixed at 22 kHz, so its samples are 16 kHz or so */
    DR("N6_KICK", C_N64, BF_VAD, 16000, 0.25, kick_ac),
    DR("N6_SNARE", C_N64, BF_VAD, 16000, 0.22, snare_ac),
    DR("N6_TOM", C_N64, BF_VAD, 12000, 0.35, tom_ac),
    DR("N6_RIM", C_N64, BF_VAD, 16000, 0.05, rim808),
    DR("N6_CLAP", C_N64, BF_VAD, 16000, 0.2, clap808),
    DR("N6_CHAT", C_N64, BF_VAD, 22050, 0.05, chat909),
    DR("N6_OHAT", C_N64, BF_VAD, 22050, 0.25, ohat909),
    DR("N6_CRASH", C_N64, BF_VAD, 20000, 0.5, crash),
    DR("N6_COW", C_N64, BF_VAD, 16000, 0.2, agogo),
    DR("N6_SURDO", C_N64, BF_VAD, 11025, 0.6, surdo),
    DR("N6_CONGA", C_N64, BF_VAD, 14000, 0.3, conga),
    DR("N6_CSLAP", C_N64, BF_VAD, 16000, 0.2, cslap),
    DR("N6_BONGO", C_N64, BF_VAD, 16000, 0.18, bongo),
    DR("N6_LOG", C_N64, BF_VAD, 14000, 0.3, logd),
    DR("N6_CLAVES", C_N64, BF_VAD, 16000, 0.06, claves),
    DR("N6_SHAKER", C_N64, BF_VAD, 22050, 0.12, shaker),
    DR("N6_TAMB", C_N64, BF_VAD, 20000, 0.22, tamb),
    MEL("N6_WHISTLE", C_N64, BF_VAD, 16000, 84, 0.42, 0, samba_whistle),
    MEL("N6_SLAP", C_N64, BF_VAD, 16000, 40, 0.12, 0.05, slap_bass),
    MEL("N6_TUBA", C_N64, BF_VAD, 12000, 36, 0.12, 0.05, tuba),
    MEL("N6_UPRIGHT", C_N64, BF_VAD, 12000, 40, 0.15, 0.05, upright_bass),
    MEL("N6_SYNBASS", C_N64, BF_VAD, 12000, 40, 0.08, 0.05, i_synbass64),
    MEL("N6_MARIMBA", C_N64, BF_VAD, 16000, 60, 0.35, 0, marimba),
    MEL("N6_ACCORD", C_N64, BF_VAD, 16000, 60, 0.08, 0.06, accordion),
    MEL("N6_PIANO", C_N64, BF_VAD, 18000, 60, 0.25, 0.04, piano),
    MEL("N6_CHOIR", C_N64, BF_VAD, 16000, 60, 0.15, 0.12, i_choir_oo),
    MEL("N6_SYNBRASS", C_N64, BF_VAD, 16000, 60, 0.12, 0.06, i_synbrass),
    MEL("N6_OCARINA", C_N64, BF_VAD, 16000, 72, 0.1, 0.06, ocarina),
    MEL("N6_FIDDLE", C_N64, BF_VAD, 16000, 67, 0.15, 0.06, fiddle),
    MEL("N6_WHISTLE2", C_N64, BF_VAD, 16000, 79, 0.1, 0.05, whistle),
    MEL("N6_STEEL", C_N64, BF_VAD, 16000, 60, 0.25, 0.04, steel_drum),
    MEL("N6_KAZOO", C_N64, BF_VAD, 14000, 60, 0.08, 0.05, kazoo),
    MEL("N6_BANJO", C_N64, BF_VAD, 18000, 60, 0.2, 0.03, banjo),
    MEL("N6_MUSICBOX", C_N64, BF_VAD, 18000, 72, 0.3, 0.03, music_box),
};
#define NS ((int)(sizeof S / sizeof S[0]))

/* ---------------------------------------------------------------- the writer --- */
static FILE *out;
static int con_bytes[C_N];
static void emit_bytes(const char *name, const bytes_t *b)
{
    int i;
    fprintf(out, "static const uint8_t %s[%d] = {", name, b->n);
    for (i = 0; i < b->n; i++)
        fprintf(out, "%s%d%s", i % 24 == 0 ? "\n    " : "", b->d[i], i + 1 < b->n ? "," : "");
    fprintf(out, "\n};\n");
}

typedef struct {
    int nsamp, loop, rate, root, x0, x1, fmt;
    int book[4][2];
    double err;
} made_t;

/* attack, then the loop (k periods = m blocks); returns the samples at the chosen rate */
static double *shape(const sdef_t *d, int *n_out, int *loop_out, double *rate_out)
{
    double f = mtof(d->root), *y;
    int blk = FMT_BLOCK[d->fmt], n, i;
    buf_t b;
    if (d->fmt == BF_DMC1 || d->fmt == BF_PCM4 || d->fmt == BF_DPCM4 || d->fmt == BF_OKI)
        blk = 1;
    rseed(0x1234567ull + (uint64_t)(d->id[0] * 131 + d->id[3] * 7 + d->id[strlen(d->id) - 1]));
    b = d->make(f);
    dc_remove(&b);
    if (d->loop <= 0) {                /* a one-shot: its length, trimmed, faded */
        if (d->att > 0 && (int)(d->att * SR) < b.n)
            b.n = (int)(d->att * SR);
        trim(&b, 0.003);
        fade_out(&b, fmin(0.02, b.n / SR * 0.2));
        y = resample(b.x, b.n, SR, d->rate, &n);
        n = (n + blk - 1) / blk * blk;
        y = (double *)realloc(y, (size_t)n * sizeof(double));
        for (i = (int)((double)b.n * d->rate / SR); i < n; i++)
            if (i >= 0)
                y[i] = 0;
        *loop_out = -1;
        *rate_out = d->rate;
    } else {                           /* a loop of k periods in m blocks: the rate made to fit */
        int k = (int)lrint(d->loop * f), m, ls, le, ny, xf;
        double rate;
        if (k < 1)
            k = 1;
        m = (int)lrint((double)k * d->rate / (blk * f));
        if (m < 1)
            m = 1;
        rate = f * blk * m / k;
        y = resample(b.x, b.n, SR, rate, &ny);
        ls = (int)ceil(d->att * rate / blk) * blk;
        if (ls < blk * 2 && d->att > 0)
            ls = blk * 2;
        le = ls + blk * m;
        if (le > ny) {
            fprintf(stderr, "%s: the source is too short for its loop\n", d->id);
            exit(1);
        }
        xf = (int)(rate / f * 2.0);    /* a crossfade over two periods */
        if (xf > ls)
            xf = ls;
        if (xf > blk * m / 2)
            xf = blk * m / 2;
        loop_xfade(y, ls, le, xf);
        n = le;
        *loop_out = ls;
        *rate_out = rate;
    }
    buf_free(&b);
    {
        double p = 0;
        for (i = 0; i < n; i++)
            p = fmax(p, fabs(y[i]));
        if (p > 0)
            for (i = 0; i < n; i++)
                y[i] *= d->gain / p;
    }
    *n_out = n;
    return y;
}

int main(int argc, char **argv)
{
    int s, c, total = 0;
    made_t *mm = (made_t *)calloc(NS, sizeof(made_t));
    if (argc < 2) {
        fprintf(stderr, "gen_bank OUT.h\n");
        return 2;
    }
    out = fopen(argv[1], "w");
    if (!out)
        return 1;
    fprintf(out, "/* Generated by tools/bank/gen_bank.c: CRT-4's samples, every one made by the program, none recorded.\n"
                 " * The source is the program; this file is its output. GPL-3.0-only. */\n#pragma once\n");
    fprintf(out, "#define BK_NOLOOP 0xFFFFFFFFu\n");
    fprintf(out, "enum { BF_PCM8, BF_PCM4, BF_PCM5, BF_DMC1, BF_DPCM4, BF_BRR, BF_SPU, BF_VAD, BF_OKI };\n");
    fprintf(out, "typedef struct {\n    const uint8_t *data;\n    uint32_t nsamp, loop;  /* samples; the loop's start (BK_NOLOOP: none) */\n"
                 "    uint16_t rate;         /* Hz: the root note sounds at this rate */\n    uint8_t fmt, root;\n"
                 "    int16_t x0;            /* DMC: the counter's start; OKI: the signal at the loop */\n"
                 "    uint8_t x1;            /* OKI: the step index at the loop */\n    const int16_t *book;   /* VAD: four predictors (Q11) */\n} bk_smp_t;\n");
    fprintf(out, "enum {\n");
    for (s = 0; s < NS; s++)
        fprintf(out, "    BK_%s,\n", S[s].id);
    fprintf(out, "    BK_N\n};\n");
    for (s = 0; s < NS; s++) {
        const sdef_t *d = &S[s];
        int n, loop, i;
        double rate, *y = shape(d, &n, &loop, &rate);
        bytes_t b = {0, 0, 0};
        char nm[64];
        made_t *m = &mm[s];
        m->x0 = 0;
        m->x1 = 0;
        switch (d->fmt) {
        case BF_PCM8: enc_pcm8(&b, y, n); break;
        case BF_PCM4: enc_pcm4(&b, y, n); break;
        case BF_PCM5: enc_pcm5(&b, y, n); break;
        case BF_DMC1: enc_dmc1(&b, y, n, &m->x0); break;
        case BF_DPCM4: enc_dpcm4(&b, y, n); break;
        case BF_BRR: enc_brr(&b, y, n, loop >= 0 ? loop / 16 : -1); break;
        case BF_SPU: enc_spu(&b, y, n, loop >= 0 ? loop / 28 : -1); break;
        case BF_VAD:
            vad_book(y, n, m->book);
            enc_vad(&b, y, n, loop >= 0 ? loop / 16 : -1, m->book);
            break;
        case BF_OKI: {                 /* the loop's state kept (the decoder is set to it there) */
            int sig = 0, idx = 0, k, hi = 0;
            for (k = 0; k < n; k++) {
                int tgt = (int)lrint(y[k] * 2047.0), q, best = 0, bs = 0, bi = 0;
                double be = 1e300;
                if (k == loop) {
                    m->x0 = sig;
                    m->x1 = idx;
                }
                for (q = 0; q < 16; q++) {
                    int ss = sig, ix = idx, v = oki_one(q, &ss, &ix);
                    double e = (double)(v - tgt) * (double)(v - tgt);
                    if (e < be) {
                        be = e;
                        best = q;
                        bs = ss;
                        bi = ix;
                    }
                }
                sig = bs;
                idx = bi;
                if (k & 1)
                    put8(&b, hi << 4 | best);
                else
                    hi = best;
            }
            if (n & 1)
                put8(&b, hi << 4);
            break;
        }
        default: break;
        }
        snprintf(nm, sizeof nm, "bk_%s", d->id);
        for (i = 0; nm[i]; i++)
            nm[i] = (char)(nm[i] >= 'A' && nm[i] <= 'Z' ? nm[i] + 32 : nm[i]);
        emit_bytes(nm, &b);
        if (d->fmt == BF_VAD) {
            fprintf(out, "static const int16_t %s_book[8] = {%d, %d, %d, %d, %d, %d, %d, %d};\n", nm, m->book[0][0],
                    m->book[0][1], m->book[1][0], m->book[1][1], m->book[2][0], m->book[2][1], m->book[3][0], m->book[3][1]);
        }
        m->nsamp = n;
        m->loop = loop;
        m->rate = (int)lrint(rate);
        m->root = d->root;
        m->fmt = d->fmt;
        con_bytes[d->con] += b.n;
        total += b.n;
        fprintf(stderr, "  %-12s %-8s %5d Hz %6d samples %6d bytes%s\n", d->id, FMT_NAME[d->fmt] + 3, m->rate, n, b.n,
                loop >= 0 ? " (looped)" : "");
        free(y);
        free(b.d);
    }
    fprintf(out, "static const bk_smp_t BK[BK_N] = {\n");
    for (s = 0; s < NS; s++) {
        char nm[64];
        int i;
        snprintf(nm, sizeof nm, "bk_%s", S[s].id);
        for (i = 0; nm[i]; i++)
            nm[i] = (char)(nm[i] >= 'A' && nm[i] <= 'Z' ? nm[i] + 32 : nm[i]);
        char ls[24];
        if (mm[s].loop >= 0)
            snprintf(ls, sizeof ls, "%d", mm[s].loop);
        else
            snprintf(ls, sizeof ls, "BK_NOLOOP");
        fprintf(out, "    {%s, %d, %s, %d, %s, %d, %d, %d, %s%s},\n", nm, mm[s].nsamp, ls, mm[s].rate,
                FMT_NAME[mm[s].fmt], mm[s].root, mm[s].x0, mm[s].x1, S[s].fmt == BF_VAD ? nm : "0",
                S[s].fmt == BF_VAD ? "_book" : "");
    }
    fprintf(out, "};\n");
    fprintf(out, "static const uint32_t BK_CON_BYTES[%d] = {", C_N);
    for (c = 0; c < C_N; c++)
        fprintf(out, "%d%s", con_bytes[c], c + 1 < C_N ? ", " : "");
    fprintf(out, "};\n");
    fprintf(out, "/* the break's slices (samples), the Jungle kit's parts */\nstatic const uint32_t BK_BREAK_SLICE[12] = {");
    for (s = 0; s < 12; s++) {
        int v = (int)((double)break_slice[s] * 22050.0 / SR) / 28 * 28;
        fprintf(out, "%d%s", v, s < 11 ? ", " : "");
    }
    fprintf(out, "};\n");
    fclose(out);
    fprintf(stderr, "bank: %d bytes\n", total);
    for (c = 0; c < C_N; c++) {
        fprintf(stderr, "  %-12s %6d bytes (budget %d)%s\n", CON_NAME[c], con_bytes[c], CON_BUDGET[c],
                con_bytes[c] > CON_BUDGET[c] ? "  OVER" : "");
    }
    free(mm);
    return 0;
}
