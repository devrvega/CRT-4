/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4 app: what is saved (the global settings, and each console's song) and the app's entry points.
 * Included by the unity build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/crt4.h"

#ifndef EN_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION "DEV"
#endif
#define GLOB_MAGIC 0x47345443u           /* "CT4G" (IND-1's and LCD-4's, at the same place in flash, differ) */
#define SONG_MAGIC 0x53345443u           /* "CT4S" */
#define BANK_MAGIC 0x42345443u           /* "CT4B" */
#define PROJ_FORMAT 1u
#define NBANK 2                          /* each console's patterns, in two flash objects of eight */
#define BANK_PATS (EN_NPAT / NBANK)
_Static_assert(NCONS == EN_NCON, "plat.h and crt4.h count the consoles alike");

/* what every console shares (OBJ_GLOB) */
typedef struct {
    uint32_t magic, format;
    uint8_t con;                         /* the console in use */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint16_t seen;                       /* bit c: console c has a song of its own (else its demo) */
    en_glob_t glob;                      /* tune, MIDI out, clock */
    uint8_t rsv[20];
} glob_t;

/* a console's song (OBJ_SONG): its settings, and the player's things on it */
typedef struct {
    uint32_t magic, format;
    en_cfg_t cfg;
    uint8_t track;                       /* the track selected */
    uint8_t part;                        /* the drum part selected */
    int8_t oct[EN_NSYN];                 /* the keyboard's octave, each track (-3..3) */
    uint8_t rsv[11];
} song_t;

/* eight patterns (OBJ_BANK) */
typedef struct {
    uint32_t magic, format, con, bank;
    en_pat_t pat[BANK_PATS];
} bank_t;
_Static_assert(sizeof(song_t) <= PLAT_STORE_MAX && sizeof(bank_t) <= PLAT_STORE_MAX, "a flash object holds them");

extern glob_t glob;
extern song_t song;

void project_defaults(void);             /* the console's demo song, the player's defaults */
int project_load(void);                  /* the globals and the console's song: 0 = loaded (else defaults) */
int project_save(void);                  /* what is marked dirty: 0 = saved and verified */
int project_console(int con);            /* save this console's song if it changed, load con's: 0 = done */
void project_dirty(int what);            /* PD_GLOB, PD_SONG, or a pattern's bank: PD_PAT(p) */
int project_is_dirty(void);
int song_valid(const song_t *s, int con);
#define PD_GLOB 1u
#define PD_SONG 2u
#define PD_PAT(p) (4u << ((p) / BANK_PATS))
#define PD_ALL 0xFu

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
void ui_console(int c);
