/* SPDX-License-Identifier: GPL-3.0-only */
/* The demo songs: one a console, each in the manner of its own games (and each the console's song the first
 * time it is chosen; Setup's Demo brings it back). Original music, written in C and left to the chords.
 * Included once, by crt4.c.
 *
 * A drum row is 16 characters, x a hit, X an accented one. A synth line is 16 tokens: "." a rest, a note
 * ("C3", "Eb2", "F#4"), notes joined by "+" for a chord; after it "!" accents, "~" slides into the next,
 * ":N" holds it N sixteenths, "*N" ratchets it (2..4). */
const char *const EN_DEMO_NAME[EN_NCON] = {"Blast Off", "Datasette", "Overworld", "Shoot!", "Night Street", "Route 1",
                                           "World Map", "Jungle Run", "Island Hub"};

static void d_drum(en_pat_t *p, int part, const char *row)
{
    int s;
    for (s = 0; s < EN_NSTEP && row[s]; s++)
        if (row[s] == 'x' || row[s] == 'X') {
            p->hit[part] |= (uint16_t)(1u << s);
            if (row[s] == 'X')
                p->acc |= (uint16_t)(1u << s);
        }
}

static int d_note(const char **ps)                /* "C#3" -> 49; 0 if not a note */
{
    static const int8_t L[7] = {9, 11, 0, 2, 4, 5, 7};   /* A B C D E F G */
    const char *s = *ps;
    int n, oct = 0, neg = 0;
    if (*s < 'A' || *s > 'G')
        return 0;
    n = L[*s++ - 'A'];
    if (*s == '#') {
        n++;
        s++;
    } else if (*s == 'b') {
        n--;
        s++;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        oct = oct * 10 + (*s++ - '0');
    *ps = s;
    return 12 * ((neg ? -oct : oct) + 1) + n;
}

static void d_line(en_pat_t *p, int t, const char *line)
{
    const char *s = line;
    int step = 0;
    while (*s && step < EN_NSTEP) {
        en_step_t *e = &p->syn[t][step];
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        if (*s == '.') {
            s++;
        } else {
            int k = 0, n;
            while ((n = d_note(&s)) != 0) {
                if (k < EN_NNOTE)
                    e->note[k++] = (uint8_t)n;
                if (*s == '+')
                    s++;
                else
                    break;
            }
            e->len = 3;
            for (;;) {
                if (*s == '!') {
                    e->fl |= EN_F_ACC;
                    s++;
                } else if (*s == '~') {
                    e->fl |= EN_F_SLIDE;
                    s++;
                } else if (*s == ':') {
                    int v = 0;
                    s++;
                    while (*s >= '0' && *s <= '9')
                        v = v * 10 + (*s++ - '0');
                    e->len = (uint8_t)clampi(v * 4, 1, 64);
                } else if (*s == '*') {
                    s++;
                    e->fl |= (uint8_t)((clampi(*s++ - '0', 1, 4) - 1) << 4);
                } else {
                    break;
                }
            }
        }
        while (*s && *s != ' ')
            s++;
        step++;
    }
}

static void d_knobs(int t, int snd, int a, int b, int c, int d)
{
    en_cfg.sk[t][snd][0] = (uint8_t)a;
    en_cfg.sk[t][snd][1] = (uint8_t)b;
    en_cfg.sk[t][snd][2] = (uint8_t)c;
    en_cfg.sk[t][snd][3] = (uint8_t)d;
}

static void d_tracks(int kit, int s1, int s2, int s3)
{
    en_cfg.tp[0][TP_SOUND] = (uint8_t)kit;
    en_cfg.tp[1][TP_SOUND] = (uint8_t)s1;
    en_cfg.tp[2][TP_SOUND] = (uint8_t)s2;
    en_cfg.tp[3][TP_SOUND] = (uint8_t)s3;
}

static void d_chords(const uint8_t *c, int n)
{
    int i;
    for (i = 0; i < n; i++)
        en_cfg.chords[i] = c[i];
    en_cfg.chords_n = (uint8_t)n;
}
static void d_chain(const uint8_t *c, int n)
{
    int i;
    for (i = 0; i < n; i++)
        en_cfg.chain[i] = c[i];
    en_cfg.chain_n = (uint8_t)n;
    en_cfg.pat = c[0];
}

void en_demo(int con)
{
    int i;
    en_pat_t *a = &en_pat[0], *b = &en_pat[1], *c = &en_pat[2], *d = &en_pat[3];
    for (i = 0; i < EN_NPAT; i++)
        en_pat_clear(&en_pat[i]);
    en_cfg_defaults(con);
    switch (con) {
    case CON_2600: {    /* Blast Off: a buzzing bass, a chiming arpeggio, a square lead out of tune with itself */
        static const uint8_t CH[4] = {5, 3, 4, 7}, CHN[4] = {0, 0, 1, 1};
        en_cfg.par[P_TEMPO] = 128;
        d_tracks(0, 1, 4, 8);
        d_drum(a, 0, "X.......X.......");
        d_drum(a, 1, "....X.......X...");
        d_drum(a, 7, "x.x.x.x.x.x.x.x.");
        d_line(a, 0, "A3 . A3 . A4 . A3 . A3 . G3 . E3 . G3 .");
        d_line(a, 1, "A4+C5+E5:2 . . . . . . . A4+C5+E5:2 . . . . . . .");
        *b = *a;
        d_drum(b, 9, "X...............");
        d_drum(b, 10, "..............x.");
        d_line(b, 2, "E5:2 . A5 . G5:2 . E5 . D5 . E5:2 . . C5 . D5");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_C64: {     /* Datasette: a filtered pulse bass leaping octaves, arpeggios a frame a note, a pulse lead */
        static const uint8_t CH[4] = {5, 3, 0, 4}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 125;
        d_tracks(0, 0, 4, 8);
        d_drum(a, 0, "X..x..X...x.....");
        d_drum(a, 1, "....X.......X..x");
        d_drum(a, 7, "x.xxx.xxx.xxx.xx");
        d_line(a, 0, "A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 G2 G3 E2 E3");
        d_line(a, 1, "A3+C4+E4:4 . . . . . . . A3+C4+E4:4 . . . . . . .");
        *b = *a;
        d_line(b, 2, "A5:3 . . C6 . B5 . A5 E5:4 . . . G5 . A5 .");
        *c = *b;
        d_line(c, 2, "C6:2 . B5 . A5 . G5 . A5:6 . . . . . . .");
        d_drum(c, 9, "X...............");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_NES: {     /* Overworld: the triangle's bass, the noise's beat, a 25% lead, the other pulse an echo */
        static const uint8_t CH[4] = {0, 3, 4, 0}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 150;
        d_tracks(0, 0, 4, 9);
        en_cfg.tp[3][TP_SEND] = 45;
        d_drum(a, 0, "X.....x.X.......");
        d_drum(a, 1, "....X.......X...");
        d_drum(a, 7, "x.x.x.x.x.x.x.x.");
        d_line(a, 0, "C3 . G3 . C3 . G3 . C3 . G3 . E3 . G3 .");
        d_line(a, 1, ". . C4+E4+G4 . . . C4+E4+G4 . . . C4+E4+G4 . . . . .");
        *b = *a;
        d_line(b, 2, "E5 . G5 . C6:2 . . B5 . G5 . E5:2 . . D5");
        *c = *b;
        d_line(c, 2, "F5 . A5 . C6 . A5 . G5:3 . . . . . . .");
        d_drum(c, 2, "............x...");
        d_drum(c, 3, ".............x..");
        d_drum(c, 4, "..............x.");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_PCE: {     /* Shoot!: a slap wave bass on eighths, organ stabs, a bright shooter's lead */
        static const uint8_t CH[4] = {5, 6, 4, 5}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 160;
        d_tracks(1, 1, 5, 9);
        d_drum(a, 0, "X...X...X...X...");
        d_drum(a, 1, "....X.......X...");
        d_drum(a, 7, "..x...x...x...x.");
        d_line(a, 0, "A2 A2 A3 A2 A2 A2 A3 A2 G2 G2 G3 G2 E2 E2 E3 G2");
        d_line(a, 1, ". . A3+C4+E4 . . . A3+C4+E4 . . . G3+B3+D4 . . . . .");
        *b = *a;
        d_line(b, 2, "A5:2 . C6 . E6:2 . D6 . C6 . B5:2 . A5 . G5");
        *c = *b;
        d_line(c, 2, "A5 C6 E6 A6:4 . . . G6 E6 D6 E6:4 . . .");
        d_drum(c, 9, "X...............");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_GEN: {     /* Night Street: a house kit on the DAC, an FM bass, the FM piano's chords, a screaming lead */
        static const uint8_t CH[4] = {13, 9, 10, 13}, CHN[4] = {0, 0, 1, 2};
        en_cfg.par[P_TEMPO] = 124;
        en_cfg.par[P_SWING] = 20;
        d_tracks(0, 1, 4, 8);
        d_drum(a, 0, "X...X...X...X...");
        d_drum(a, 6, "....X.......X...");
        d_drum(a, 7, "..x...x...x...x.");
        d_drum(a, 8, "..............x.");
        d_line(a, 0, "A2 . A3 . . A2 . A3 A2 . A3 . G2 . G3 .");
        d_line(a, 1, ". . A3+C4+E4+G4 . . . . . . A3+C4+E4+G4 . . . . . . .");
        *b = *a;
        d_line(b, 2, "E5:3 . . G5 . A5:3 . . . G5 . E5 . D5 .");
        *c = *b;
        d_line(c, 2, "C6:4 . . . B5 . A5 . G5:4 . . . E5 . . .");
        d_drum(c, 9, "X...............");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_GB: {      /* Route 1: a wave bass walking, plucked arpeggios, a pulse lead, its echo on the other side */
        static const uint8_t CH[4] = {0, 5, 3, 4}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 140;
        d_tracks(0, 0, 4, 11);
        en_cfg.tp[2][TP_PAN] = 0;
        en_cfg.tp[3][TP_PAN] = 50;
        d_drum(a, 0, "X.......X.......");
        d_drum(a, 1, "....X.......X...");
        d_drum(a, 7, "x.x.x.x.x.x.x.x.");
        d_line(a, 0, "C3 . E3 . G3 . E3 . C3 . E3 . G3 . B3 .");
        d_line(a, 1, "C4+E4+G4:2 . . . . . . . C4+E4+G4:2 . . . . . . .");
        *b = *a;
        d_line(b, 2, "G5:2 . E5 . C5 . E5 . G5 . A5 . G5:2 . . .");
        *c = *b;
        d_line(c, 2, "A5 . G5 . F5 . E5 . D5:2 . . . C5:2 . . .");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_SNES: {    /* World Map: a finger bass, strings under a flute, in the echo */
        static const uint8_t CH[4] = {0, 5, 3, 4}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 100;
        d_tracks(0, 1, 4, 8);
        for (i = 1; i < EN_NTRACK; i++)
            en_cfg.tp[i][TP_SEND] = 45;
        en_cfg.tp[0][TP_SEND] = 20;
        d_drum(a, 0, "X.......X.x.....");
        d_drum(a, 1, "....X.......X...");
        d_drum(a, 7, "x.x.x.x.x.x.x.x.");
        d_line(a, 0, "C2:2 . . . G2 . . . C3:2 . . . G2 . E2 .");
        d_line(a, 1, "C4+E4+G4:16 . . . . . . . . . . . . . . .");
        *b = *a;
        d_line(b, 2, "G5:6 . . . . . E5:2 . C5:4 . . . D5 . E5 .");
        *c = *b;
        d_line(c, 2, "F5:4 . . . E5:2 . D5:2 . C5:8 . . . . . . .");
        d_drum(c, 9, "X...............");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    case CON_PS1: {     /* Jungle Run: the break cut up, a reese under it, a pad in the space echo, a lead */
        static const uint8_t CH[4] = {5, 3, 0, 4}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 170;
        d_tracks(0, 0, 13, 8);
        en_cfg.tp[2][TP_SEND] = 55;
        en_cfg.tp[3][TP_SEND] = 35;
        en_cfg.tp[0][TP_SEND] = 10;
        d_drum(a, 0, "X.........X.....");
        d_drum(a, 1, "..x.............");
        d_drum(a, 2, "....X.......X...");
        d_drum(a, 3, "......x.........");
        d_drum(a, 4, ".......x........");
        d_drum(a, 5, ".........x......");
        d_drum(a, 8, ".............x..");
        d_drum(a, 9, "..............x.");
        d_drum(a, 10, "...............x");
        d_line(a, 0, "A1:6 . . . . . F1:4 . . . G1:6 . . . . .");
        d_line(a, 1, "A3+C4+E4:16 . . . . . . . . . . . . . . .");
        *b = *a;
        d_drum(b, 4, ".......x...x....");
        d_drum(b, 7, "............X...");
        d_line(b, 2, "E5:3 . . D5 . C5:2 . . E5:4 . . . G5 . A5");
        *c = *b;
        d_line(c, 2, "A5:6 . . . . . G5:2 . E5:8 . . . . . . .");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    default: {          /* Island Hub: a tuba bouncing, a marimba, an ocarina over the top, in the room */
        static const uint8_t CH[4] = {0, 3, 4, 0}, CHN[4] = {0, 1, 0, 2};
        en_cfg.par[P_TEMPO] = 112;
        en_cfg.par[P_SWING] = 30;
        d_tracks(1, 1, 4, 9);
        for (i = 1; i < EN_NTRACK; i++)
            en_cfg.tp[i][TP_SEND] = 35;
        d_drum(a, 0, "X.......X.......");
        d_drum(a, 4, "..x.x.....x.x...");
        d_drum(a, 6, "x.x.x.x.x.x.x.x.");
        d_drum(a, 7, ".x.x.x.x.x.x.x.x");
        d_line(a, 0, "C2 . . G2 . . C3 . G2 . . E2 . . G2 .");
        d_line(a, 1, "C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 . . .");
        *b = *a;
        d_line(b, 2, "E5:2 . G5 . C6:3 . . B5 A5 G5:2 . E5:2 . . .");
        *c = *b;
        d_line(c, 2, "F5:2 . A5 . C6:2 . A5 . G5:6 . . . . . . .");
        d_drum(c, 10, "X...............");
        d_chords(CH, 4);
        d_chain(CHN, 4);
        break;
    }
    }
    (void)d;
    (void)d_knobs;
}

#if defined(CRT4_BENCH) || defined(EN_HOST)
/* the heaviest songs, to measure the load (the bench build's SysEx loads them): k 0 every track four-note
 * chords (the mono consoles' arpeggios) and every drum on every step; k 1 the console's demo */
void crt4_bench(int k)
{
    static const uint8_t HEAVY[EN_NCON][3] = {{5, 5, 5}, {5, 6, 9}, {13, 6, 14}, {6, 3, 10}, {7, 4, 9},
                                              {2, 10, 11}, {4, 5, 9}, {4, 13, 15}, {7, 5, 8}};
    int t, s, p, con = en_cfg.con;
    en_demo(con);
    if (k == 1) {
        en_play(1);
        return;
    }
    en_cfg.chain_n = 0;
    en_cfg.chords_n = 0;
    en_cfg.pat = 0;
    en_pat_clear(&en_pat[0]);
    for (p = 0; p < EN_NPART; p++)
        en_pat[0].hit[p] = 0xFFFFu;
    for (t = 0; t < EN_NSYN; t++) {
        int snd = HEAVY[con][t];
        en_cfg.tp[t + 1][TP_SOUND] = (uint8_t)snd;
        en_cfg.tp[t + 1][TP_LEVEL] = 60;
        en_cfg.tp[t + 1][TP_SEND] = 40;
        for (s = 0; s < EN_NSTEP; s++) {
            en_step_t *e = &en_pat[0].syn[t][s];
            e->note[0] = (uint8_t)(48 + t * 7 + (s % 5));
            e->note[1] = (uint8_t)(e->note[0] + 4);
            e->note[2] = (uint8_t)(e->note[0] + 7);
            e->note[3] = (uint8_t)(e->note[0] + 11);
            e->len = 6;
            e->fl = (uint8_t)((s & 3) == 0 ? EN_F_ACC : 0);
        }
    }
    en_cfg.tp[0][TP_SEND] = 40;
    en_cfg.par[P_TEMPO] = 140;
    en_play(1);
}
#endif
