/* SPDX-License-Identifier: GPL-3.0-only */
/* The nine consoles: what each is called, what its FX page does, its sounds and kits, its engine, and its
 * output (its effects, its filters, the TV it played through). Included once, by crt4.c. */

#include "crt4_fx.c"
#include "crt4_tia.c"
#include "crt4_sid.c"
#include "crt4_nes.c"
#include "crt4_pce.c"
#include "crt4_gen.c"
#include "crt4_gb.c"
#include "crt4_smp.c"

static const en_console_t CONS[EN_NCON] = {
    {"ATARI 2600", "Atari Video Computer System", "TIA", 1977, 1, PAN_MONO,
     {"Echo", "Repeats", "Region", "TV"}, {33, 30, 0, 25}, {6, 0, 2, 0}},
    {"C64", "Commodore 64", "SID 6581", 1982, 1, PAN_MONO,
     {"Echo", "Repeats", "Chip", "TV"}, {33, 30, 0, 15}, {6, 0, 2, 0}},
    {"NES", "Nintendo Entertainment System", "Ricoh 2A03", 1983, 1, PAN_MONO,
     {"Echo", "Repeats", "Region", "TV"}, {33, 30, 0, 15}, {6, 0, 2, 0}},
    {"PC ENGINE", "PC Engine / TurboGrafx-16", "HuC6280", 1987, 0, PAN_FULL,
     {"Echo", "Repeats", "Stereo", "TV"}, {33, 30, 100, 10}, {6, 0, 2, 0}},
    {"GENESIS", "Sega Genesis / Mega Drive", "YM2612 + SN76489", 1988, 0, PAN_LCR,
     {"Echo", "Repeats", "Model", "TV"}, {33, 30, 0, 10}, {6, 0, 2, 0}},
    {"GAME BOY", "Nintendo Game Boy", "DMG", 1989, 1, PAN_LCR,
     {"Echo", "Repeats", "Model", "Speaker"}, {33, 30, 0, 0}, {6, 0, 3, 0}},
    {"SNES", "Super Nintendo Entertainment System", "S-DSP", 1990, 0, PAN_FULL,
     {"Delay", "Feedback", "Filter", "Echo"}, {40, 45, 0, 40}, {0, 0, 4, 0}},
    {"PLAYSTATION", "Sony PlayStation", "SPU", 1994, 0, PAN_FULL,
     {"Reverb", "Depth", "Feedback", "Tone"}, {75, 45, 50, 60}, {9, 0, 0, 0}},
    {"N64", "Nintendo 64", "RCP audio", 1996, 0, PAN_FULL,
     {"Effect", "Amount", "Feedback", "Rate"}, {25, 40, 50, 0}, {5, 0, 0, 2}},
};
const en_console_t *en_console(int con) { return &CONS[(unsigned)con % EN_NCON]; }
static int con_echo_chip(void) { return en_con <= CON_GB; }   /* the chip consoles: the tracker's echo */

static const en_sound_t *con_sounds(int con)
{
    switch (con) {
    case CON_2600: return TIA_SND;
    case CON_C64: return SID_SND;
    case CON_NES: return NES_SND;
    case CON_PCE: return PCE_SND;
    case CON_GEN: return GEN_SND;
    case CON_GB: return GB_SND;
    case CON_SNES: return SNES_SND;
    case CON_PS1: return PS1_SND;
    default: return N64_SND;
    }
}
static const dkit_t *kit_of(int con, int k)
{
    const dkit_t *K;
    switch (con) {
    case CON_2600: K = TIA_KIT; break;
    case CON_C64: K = SID_KIT; break;
    case CON_NES: K = NES_KIT; break;
    case CON_PCE: K = PCE_KIT; break;
    case CON_GEN: K = GEN_KIT; break;
    case CON_GB: K = GB_KIT; break;
    case CON_SNES: K = SNES_KIT; break;
    case CON_PS1: K = PS1_KIT; break;
    default: K = N64_KIT; break;
    }
    return &K[(unsigned)k % EN_NKIT];
}
const en_sound_t *en_sound(int con, int s) { return &con_sounds(con)[(unsigned)s % EN_NSOUND]; }
static const en_sound_t *snd_of_trk(int t) { return en_sound(en_con, trk[t].snd); }

static const engine_t *con_engine(int con)
{
    switch (con) {
    case CON_2600: return &TIA_ENG;
    case CON_C64: return &SID_ENG;
    case CON_NES: return &NES_ENG;
    case CON_PCE: return &PCE_ENG;
    case CON_GEN: return &GEN_ENG;
    case CON_GB: return &GB_ENG;
    case CON_SNES: return &SNES_ENG;
    case CON_PS1: return &PS1_ENG;
    default: return &N64_ENG;
    }
}

static int con_wave_text(int con, int snd, int k, int pos, char *b)
{
    const drv_t *d = (const drv_t *)en_sound(con, snd)->patch;
    if (d->kt[k & 3] != KT_WAVE)
        return 0;
    switch (con) {
    case CON_2600: return tia_wave_text(snd, pos, b);
    case CON_C64: return sid_wave_text(snd, pos, b);
    case CON_NES: return nes_wave_text(snd, pos, b);
    case CON_PCE: return pce_wave_text(snd, pos, b);
    case CON_GB: return gb_wave_text(snd, pos, b);
    default: return 0;
    }
}
int en_sound_zones(int con, int snd, int k) { return en_sound(con, snd)->zones[k & 3]; }
void en_sound_text(int con, int snd, int k, int v, char *b)
{
    const en_sound_t *s = en_sound(con, snd);
    const drv_t *d = (const drv_t *)s->patch;
    int z = s->zones[k & 3], pos = z ? (v * (z - 1) + 50) / 100 : 0;
    switch (d->kt[k & 3]) {
    case KT_OCTAVE: {
        static const char *const O[3] = {"-1", "0", "+1"};
        scopy(b, O[pos % 3], 10);
        return;
    }
    case KT_ARPMODE: {
        static const char *const M[4] = {"Up", "Down", "Up-down", "Random"};
        scopy(b, M[pos & 3], 10);
        return;
    }
    case KT_ARPSPD: {
        static const char *const A[6] = {"1 frame", "2 frames", "3 frames", "4 frames", "5 frames", "6 frames"};
        scopy(b, A[pos % 6], 10);
        return;
    }
    default: break;
    }
    if (z && con_wave_text(con, snd, k, pos, b))
        return;
    itoa_s(v, b);
}

void en_fx_text(int con, int k, int v, char *b)
{
    const en_console_t *c = en_console(con);
    int z = c->fx_zones[k & 3], pos = z ? (v * (z - 1) + 50) / 100 : 0;
    if (z && (k & 3) == 0 && con <= CON_GB) {
        static const char *const E[6] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2"};
        scopy(b, E[pos % 6], 10);
        return;
    }
    switch (con) {
    case CON_2600:
    case CON_NES:
        if (k == 2) {
            scopy(b, pos ? "PAL" : "NTSC", 10);
            return;
        }
        break;
    case CON_C64:
        if (k == 2) {
            scopy(b, pos ? "8580" : "6581", 10);
            return;
        }
        break;
    case CON_PCE:
        if (k == 2) {
            scopy(b, pos ? "Stereo" : "Mono", 10);
            return;
        }
        break;
    case CON_GEN:
        if (k == 2) {
            scopy(b, pos ? "Model 2" : "Model 1", 10);
            return;
        }
        break;
    case CON_GB:
        if (k == 2) {
            static const char *const M[3] = {"DMG", "Pocket", "Color"};
            scopy(b, M[pos % 3], 10);
            return;
        }
        break;
    case CON_SNES:
        if (k == 0) {                  /* the echo's delay: EDL x 16 ms */
            char *e = itoa_s(16 * (1 + v * 14 / 100), b);
            scopy(e, "ms", 3);
            return;
        }
        if (k == 2) {
            static const char *const F[4] = {"Flat", "Soft", "Dark", "Bright"};
            scopy(b, F[pos & 3], 10);
            return;
        }
        break;
    case CON_PS1:
        if (k == 0) {
            static const char *const R[9] = {"Room", "Studio S", "Studio M", "Studio L", "Hall", "Half echo",
                                             "Space echo", "Chaos", "Delay"};
            scopy(b, R[pos % 9], 10);
            return;
        }
        break;
    case CON_N64:
        if (k == 0) {
            static const char *const T[5] = {"Room", "Big room", "Echo", "Chorus", "Flange"};
            scopy(b, T[pos % 5], 10);
            return;
        }
        if (k == 3) {
            scopy(b, pos ? "32 kHz" : "22 kHz", 10);
            return;
        }
        break;
    default: break;
    }
    itoa_s(v, b);
}
static int fx_pos(int k)                          /* the FX page's knob k as its position (a switch's) */
{
    int z = en_console(en_con)->fx_zones[k & 3], v = clampi(en_cfg.par[P_FX1 + (k & 3)], 0, 100);
    return z ? (v * (z - 1) + 50) / 100 : v;
}

uint32_t en_bank_bytes(int con) { return BK_CON_BYTES[(unsigned)con % EN_NCON]; }

/* the console's output: its effect (on the sends), its filters, its TV */
static void con_out(float *l, float *r, float *sl, float *sr, uint32_t n)
{
    switch (en_con) {
    case CON_2600: tia_out(l, r, n); break;
    case CON_C64: sid_out(l, r, n); break;
    case CON_NES: nes_out(l, r, n); break;
    case CON_PCE: pce_out(l, r, n); break;
    case CON_GEN: gen_out(l, r, n); break;
    case CON_GB: gb_out(l, r, n); break;
    case CON_SNES: snes_out(l, r, sl, sr, n); break;
    case CON_PS1: ps1_out(l, r, sl, sr, n); break;
    default: n64_out(l, r, sl, sr, n); break;
    }
    if (en_con <= CON_GB)
        tv_run(l, r, n, (float)fx_pos(3) * 0.01f * (en_con == CON_GB ? 0.0f : 1.0f));
}
static void con_control(void)                     /* once a chunk */
{
    switch (en_con) {
    case CON_2600: tia_control(); break;
    case CON_C64: sid_control(); break;
    case CON_NES: nes_control(); break;
    case CON_GEN: gen_model2 = fx_pos(2); break;
    case CON_N64: {                    /* its mixing rate: 22 or 32 kHz (Rate) */
        const engine_t *e = fx_pos(3) ? &N64_ENG32 : &N64_ENG;
        if (e != eng && !en_playing) {
            eng = e;
            ratio = eng->rate / EN_SR;
        }
        break;
    }
    default: break;
    }
}
static void con_reset(void) { fx_reset(); }
static void cons_init(void)
{
    nes_init();
    tia_init();
    sid_init();
    pce_init();
    gen_init();
    gb_init();
    smp_init();
}
