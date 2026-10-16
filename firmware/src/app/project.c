/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4 project: the global settings in one flash object (OBJ_GLOB); each console's song in three more
 * (its settings, and its sixteen patterns in two banks), so a change to one pattern rewrites one small
 * object and a console's song is there when it comes back. A console never saved plays its demo. Part of
 * the unity build, after app.h. */
glob_t glob;
song_t song;
static bank_t bank_buf;
static uint32_t dirty;

void project_dirty(int what) { dirty |= (uint32_t)what; }
int project_is_dirty(void) { return dirty != 0; }

static void glob_defaults(void)
{
    memset(&glob, 0, sizeof glob);
    glob.magic = GLOB_MAGIC;
    glob.format = PROJ_FORMAT;
    glob.con = CON_NES;
    glob.leds = 1;
    en_glob_defaults();
    glob.glob = en_glob;
}

void project_defaults(void)              /* the console's demo, and where the player starts on it */
{
    int t, con = glob.con % EN_NCON;
    memset(&song, 0, sizeof song);
    song.magic = SONG_MAGIC;
    song.format = PROJ_FORMAT;
    en_demo(con);                        /* the settings and all sixteen patterns */
    song.track = 1;
    song.part = 0;
    for (t = 0; t < EN_NSYN; t++)
        song.oct[t] = 0;
    song.oct[0] = -1;                    /* the bass track starts an octave down */
    en_cfg.con = (uint8_t)con;
}

int song_valid(const song_t *s, int con)
{
    int t;
    if (s->magic != SONG_MAGIC || s->format != PROJ_FORMAT || !en_cfg_valid(&s->cfg) || s->cfg.con != con ||
        s->track >= EN_NTRACK || s->part >= EN_NPART)
        return 0;
    for (t = 0; t < EN_NSYN; t++)
        if (s->oct[t] < -3 || s->oct[t] > 3)
            return 0;
    return 1;
}
static int glob_valid(const glob_t *g)
{
    int i;
    if (g->magic != GLOB_MAGIC || g->format != PROJ_FORMAT || g->con >= EN_NCON || g->leds > 2u)
        return 0;
    for (i = 0; i < G_N; i++)
        if (g->glob.v[i] < en_glob_info(i)->lo || g->glob.v[i] > en_glob_info(i)->hi)
            return 0;
    return 1;
}

/* the console's song from flash: its settings and both banks (a bank missing or broken: empty patterns);
 * 0 = it was there */
static int song_load(int con)
{
    int n, b, i;
    memset(&song, 0, sizeof song);
    n = plat_store_load(OBJ_SONG(con), &song, sizeof song);
    if (!(glob.seen >> con & 1u) || n != (int)sizeof song || !song_valid(&song, con)) {
        project_defaults();
        dirty |= PD_SONG | PD_PAT(0) | PD_PAT(BANK_PATS);
        return -1;
    }
    en_cfg = song.cfg;
    for (b = 0; b < NBANK; b++) {
        n = plat_store_load(OBJ_BANK(con, b), &bank_buf, sizeof bank_buf);
        if (n == (int)sizeof bank_buf && bank_buf.magic == BANK_MAGIC && bank_buf.format == PROJ_FORMAT &&
            bank_buf.con == (uint32_t)con && bank_buf.bank == (uint32_t)b) {
            for (i = 0; i < BANK_PATS; i++)
                if (en_pat_valid(&bank_buf.pat[i])) {
                    en_pat[b * BANK_PATS + i] = bank_buf.pat[i];
                } else {
                    en_pat_clear(&en_pat[b * BANK_PATS + i]);
                    dirty |= PD_PAT(b * BANK_PATS);
                }
        } else {
            for (i = 0; i < BANK_PATS; i++)
                en_pat_clear(&en_pat[b * BANK_PATS + i]);
            dirty |= PD_PAT(b * BANK_PATS);   /* written as it is now, empty, at the next save */
        }
    }
    return 0;
}

int project_load(void)
{
    int n;
    dirty = 0;
    memset(&glob, 0, sizeof glob);
    n = plat_store_load(OBJ_GLOB, &glob, sizeof glob);
    if (n != (int)sizeof glob || !glob_valid(&glob)) {
        glob_defaults();
        dirty |= PD_GLOB;
    }
    en_glob = glob.glob;
    if (song_load(glob.con) != 0 || (dirty & PD_GLOB))
        return -1;
    return 0;
}

int project_save(void)
{
    int b, rc = 0, con = glob.con % EN_NCON;
    if (!dirty)
        return 0;
    for (b = 0; b < NBANK; b++)
        if (dirty & PD_PAT(b * BANK_PATS)) {
            bank_buf.magic = BANK_MAGIC;
            bank_buf.format = PROJ_FORMAT;
            bank_buf.con = (uint32_t)con;
            bank_buf.bank = (uint32_t)b;
            memcpy(bank_buf.pat, &en_pat[b * BANK_PATS], sizeof bank_buf.pat);
            if (plat_store_save(OBJ_BANK(con, b), &bank_buf, sizeof bank_buf))
                rc = -1;
            else
                dirty &= ~(uint32_t)PD_PAT(b * BANK_PATS);
        }
    if (dirty & PD_SONG) {             /* the settings after the patterns: a song only ever names saved patterns */
        song.cfg = en_cfg;
        if (plat_store_save(OBJ_SONG(con), &song, sizeof song))
            rc = -1;
        else {
            dirty &= ~PD_SONG;
            if (!(glob.seen >> con & 1u)) {
                glob.seen |= (uint16_t)(1u << con);
                dirty |= PD_GLOB;
            }
        }
    }
    if (dirty & PD_GLOB) {
        glob.glob = en_glob;
        if (plat_store_save(OBJ_GLOB, &glob, sizeof glob))
            rc = -1;
        else
            dirty &= ~PD_GLOB;
    }
    return rc;
}

/* another console: this one's song saved (if it changed), the other's loaded (or its demo) */
int project_console(int con)
{
    int rc = 0;
    con = (con % EN_NCON + EN_NCON) % EN_NCON;
    if (dirty & ~PD_GLOB)
        rc = project_save();
    glob.con = (uint8_t)con;
    dirty |= PD_GLOB;
    song_load(con);
    return rc;
}
