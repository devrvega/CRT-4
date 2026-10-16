/* SPDX-License-Identifier: GPL-3.0-only */
/* CRT-4's scenes: one a console, in that console's manner (its resolution, its palette, its tricks), and
 * each moving with the music: the drums, the bass, the chords and the lead each push something. Pip, who
 * lived in LCD-4's LCD, is in all of them, drawn as each era would have drawn him. All the art is CRT-4's
 * own (no characters, logos or screens of the consoles' games). Also here: each console's power-on, and
 * the shelf the consoles sit on (GLO + ALGORITHM). Part of ui.c.
 *
 * The scene's area is 232 x 92 screen pixels. The 2D consoles draw in "game pixels" of 2 x 2 (116 x 46;
 * the 2600 and the C64 wider still, as theirs were), the 3D ones (N64, PlayStation) in screen pixels. */

#define SCX 4                          /* the scene's corner on the screen */
#define SCY 27
#define SW 232
#define SH 92
#define GW 116                         /* game pixels (2 x 2) */
#define GH 46

typedef struct {
    uint32_t ms, frame;
    int playing, step, beat;
    uint32_t steps;
    uint32_t ago[EN_NPART];            /* ms since each drum part played */
    uint32_t nago[EN_NTRACK];          /* ms since each track's last note */
    uint32_t ncount[EN_NTRACK];        /* notes so far, each track */
    int note[EN_NTRACK], acc[EN_NTRACK], gate;
    float lvl[EN_NTRACK];
    int chord;
    int tempo;
} scn_t;

/* ----------------------------------------------------------- the drawing kit --- */
static int gsx = 2, gsy = 2;           /* a game pixel's size (screen pixels) */
static inline void gp(int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)(SW / gsx) && (unsigned)y < (unsigned)(SH / gsy))
        cv_rect(SCX + gsx * x, SCY + gsy * y, gsx, gsy, c);
}
static void grect(int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h, mw = SW / gsx, mh = SH / gsy;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > mw)
        x1 = mw;
    if (y1 > mh)
        y1 = mh;
    if (x1 > x && y1 > y)
        cv_rect(SCX + gsx * x, SCY + gsy * y, gsx * (x1 - x), gsy * (y1 - y), c);
}
/* a sprite drawn from text: '.' clear, '0'..'9' and 'a'..'f' the palette's colours; mirrored if flip */
static void gspr(int x, int y, const char *const *art, const uint16_t *pal, int flip)
{
    int r, c;
    for (r = 0; art[r]; r++) {
        int w = slen(art[r]);
        for (c = 0; c < w; c++) {
            char ch = art[r][flip ? w - 1 - c : c];
            int i = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
            if (i >= 0)
                gp(x + c, y + r, pal[i]);
        }
    }
}
static void gline(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, guard = 600;
    while (guard--) {
        int e2 = 2 * err;
        gp(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static void gdisc(int cx, int cy, int r, uint16_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                gp(cx + x, cy + y, c);
}
static uint32_t srnd(uint32_t x)       /* a hash: the same "random" each frame for the same x */
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
static float since(uint32_t ago, float len) { return ago < (uint32_t)(len * 1000.0f) ? 1.0f - (float)ago / (len * 1000.0f) : 0.0f; }

/* a screen pixel of the scene, read and written (for the post effects) */
static inline uint16_t sread(int x, int y) { return swap16(cv_px[(uint32_t)(SCY + y + cv_oy) * cv_w + (uint32_t)(SCX + x)]); }
static inline void swrite(int x, int y, uint16_t c) { cv_px[(uint32_t)(SCY + y + cv_oy) * cv_w + (uint32_t)(SCX + x)] = swap16(c); }
static inline uint16_t *srow(int y) { return &cv_px[(uint32_t)(SCY + y + cv_oy) * cv_w + SCX]; }   /* a line of it */
/* a pixel's channels spread apart (green above, red and blue below), so that one multiply by up to 32 scales
 * all three at once */
static inline uint32_t spread565(uint32_t c) { return (c | c << 16) & 0x07E0F81Fu; }
static inline uint16_t pack565(uint32_t x) { return (uint16_t)((x & 0x07E0F81Fu) | (x & 0x07E0F81Fu) >> 16); }
static void scanlines(int dark)        /* a CRT's: every other line a little darker */
{
    int x, y;
    uint32_t k = (256u - (uint32_t)dark) >> 3;
    if (!dark)
        return;
    for (y = 1; y < SH; y += 2) {
        uint16_t *r = srow(y);
        for (x = 0; x < SW; x++)
            r[x] = swap16(pack565((spread565(swap16(r[x])) * k) >> 5));
    }
}
static uint16_t mix565(uint16_t a, uint16_t b, int t)   /* t 0..256: a .. b */
{
    uint32_t ra = a >> 11, ga = (a >> 5) & 63u, ba = a & 31u, rb = b >> 11, gb = (b >> 5) & 63u, bb = b & 31u;
    uint32_t u = (uint32_t)t, v = 256u - u;
    return (uint16_t)((((ra * v + rb * u) >> 8) << 11) | (((ga * v + gb * u) >> 8) << 5) | ((ba * v + bb * u) >> 8));
}

/* Pip, in any era: a round head with two eyes, a body, two legs (walking: frame 0..3). size: the head's
 * radius in game pixels; pal: outline, body, light, eyes */
static void pip_draw(int x, int y, int r, int frame, const uint16_t *pal, int jump, int flip)
{
    int hy = y + r, by = y + 2 * r + 1, bw = r + r / 2, k;
    (void)flip;
    gdisc(x, hy, r + 1, pal[0]);                     /* the head, outlined */
    gdisc(x, hy, r, pal[1]);
    if (r >= 3)
        gdisc(x - r / 2, hy - r / 2, r / 3, pal[2]); /* a shine */
    gp(x - r / 3 - (r > 3), hy, pal[3]);            /* the eyes */
    gp(x + r / 3 + (r > 3), hy, pal[3]);
    if (r >= 4) {
        gp(x - r / 3 - 1, hy - 1, pal[3]);
        gp(x + r / 3 + 1, hy - 1, pal[3]);
    }
    grect(x - bw / 2 - 1, by - 1, bw + 2, r + 2, pal[0]);   /* the body */
    grect(x - bw / 2, by, bw, r, pal[1]);
    for (k = 0; k < 2; k++) {                        /* the legs, walking */
        int lx = x - bw / 4 + k * (bw / 2) - (k == 0), step = jump ? (k ? 1 : -1) : ((frame + k * 2) & 3) - 1;
        grect(lx + (step > 0 ? 1 : step < 0 ? -1 : 0), by + r, 1 + (r > 4), r / 2 + 1 - (jump && k), pal[0]);
    }
}

/* ---------------------------------------------------------------- the scenes --- */
#define C16(r, g, b) RGB(r, g, b)

/* ATARI 2600: a space shooter over the console's colour bands. Chunky (each game pixel 4 wide, as its
 * playfield's), one colour a sprite, flickering when there are many. Pip's ship follows the lead's pitch,
 * the lead fires, the snare blows up an invader, the kick shakes the bands, the bass rolls the ground. */
static void scn_2600(const scn_t *s)
{
    static const uint16_t BANDS[8] = {C16(68, 40, 0), C16(104, 48, 24), C16(132, 20, 60), C16(100, 20, 120),
                                      C16(48, 32, 140), C16(24, 60, 140), C16(16, 92, 92), C16(44, 108, 28)};
    int x, y, k, sx;
    gsx = 4;
    gsy = 2;
    for (y = 0; y < GH; y++) {                     /* the bands, sliding with the beat */
        int b = ((y + (int)(s->steps & 7u) + (s->ago[0] < 120 ? 1 : 0)) / 6) & 7;
        grect(0, y, 58, 1, y < 4 ? C16(0, 0, 0) : BANDS[b]);
    }
    {   /* the score: notes played, in chunky digits */
        char t[8];
        uint32_t sc = s->ncount[1] + s->ncount[2] + s->ncount[3];
        int i;
        for (i = 5; i >= 0; i--) {
            t[i] = (char)('0' + sc % 10u);
            sc /= 10u;
        }
        t[6] = 0;
        cv_ptext(SCX + 80, SCY + 1, t, 1, C16(252, 188, 116));
    }
    for (k = 0; k < 6; k++) {                     /* the invaders (half of them each frame: the flicker) */
        int ix = 6 + k * 9 + (int)((s->steps / 2u) % 5u) - 2, iy = 8 + (k & 1) * 6, dead = s->ago[1] < 300 && (int)(s->ncount[0] % 6u) == k;
        if ((k + (int)s->frame) & 1)
            continue;
        if (dead) {
            for (x = 0; x < 4; x++)
                gp(ix + (int)(srnd(s->frame + (uint32_t)x) % 5u), iy + (int)(srnd(s->frame * 7u + (uint32_t)x) % 4u), C16(252, 252, 84));
        } else {
            static const char *const INV[] = {".1.1.", "11111", "1.1.1", ".1.1.", 0};
            static const uint16_t P[2] = {0, C16(180, 252, 120)};
            gspr(ix, iy, INV, P, 0);
        }
    }
    sx = 8 + (int)((float)((s->note[3] > 0 ? s->note[3] : 60) % 24) * 1.8f);   /* the ship, at the lead's pitch */
    {
        static const char *const SHIP[] = {"..1..", ".111.", "11111", "1.2.1", 0};
        static const char *const PIP[] = {".11.", "1221", ".11.", 0};
        static const uint16_t P[3] = {0, C16(108, 144, 252), C16(252, 252, 252)};
        gspr(sx - 2, 36, SHIP, P, 0);
        gspr(sx - 1, 33, PIP, P, 0);              /* Pip, in his cockpit */
    }
    if (s->nago[3] < 220)                          /* the shot */
        for (y = 33 - (int)(s->nago[3] / 7u); y < 33 && y >= 3; y++)
            gp(sx, y, C16(252, 252, 84));
    for (x = 0; x < 58; x++)                       /* the ground, rolling with the bass */
        gp(x, 42 + ((x + (int)(s->ncount[1] * 3u)) % 7 == 0), C16(152, 100, 40));
    gsx = gsy = 2;
}

/* C64: the tape loading's stripes in the border (thick and fast on the kick), a scroller inside: Pip runs
 * (the bass), jumps to the lead's notes, the stars twinkle on the snare; a line of text scrolls at the foot
 * through a cycling raster bar */
static void scn_c64(const scn_t *s)
{
    static const uint16_t VIC[16] = {C16(0, 0, 0), C16(255, 255, 255), C16(136, 57, 50), C16(103, 182, 189),
                                     C16(139, 63, 150), C16(85, 160, 73), C16(64, 49, 141), C16(191, 206, 114),
                                     C16(139, 84, 41), C16(87, 66, 0), C16(184, 105, 98), C16(80, 80, 80),
                                     C16(120, 120, 120), C16(148, 224, 137), C16(120, 105, 196), C16(159, 159, 159)};
    int x, y, k;
    uint32_t seed = s->steps * 977u + (s->ago[0] < 100 ? s->frame : 0u);
    gsx = 2;
    gsy = 1;
    for (y = 0; y < SH; y++) {                     /* the border: the loader's stripes */
        uint32_t r = srnd(seed + (uint32_t)(y / (s->ago[0] < 100 ? 2 : 5)));
        uint16_t c = VIC[r % 16u];
        grect(0, y, 116, 1, c);
    }
    gsx = gsy = 2;
    grect(8, 4, 100, 38, VIC[6]);                  /* the screen inside */
    for (k = 0; k < 14; k++) {                     /* stars */
        int sx = 8 + (int)((srnd((uint32_t)k) % 100u + 200u - (s->frame / 2u + (uint32_t)k * 3u) % 100u) % 100u), sy = 5 + (int)(srnd((uint32_t)k + 50u) % 20u);
        gp(sx, sy, (s->ago[1] < 160 && (k & 1)) ? VIC[1] : VIC[15]);
    }
    for (x = 8; x < 108; x++) {                    /* the hills (double-wide pixels: the multicolour mode) */
        int hh = 30 - (int)(4.0f * fm_sinf((float)(x + (int)(s->frame / 2u)) * 0.12f));
        grect(x, hh, 1, 42 - hh, (x / 2) & 1 ? VIC[5] : VIC[13]);
    }
    grect(8, 38, 100, 4, VIC[9]);
    {   /* Pip: running with the bass, jumping to the lead */
        static const uint16_t P[4] = {C16(0, 0, 0), C16(184, 105, 98), C16(255, 255, 255), C16(0, 0, 0)};
        float j = since(s->nago[3], 0.35f);
        int py = 25 - (int)(j * (1.0f - j) * 4.0f * 12.0f);
        pip_draw(30, py, 3, (int)(s->ncount[1] & 3u), P, j > 0.0f, 0);
    }
    {   /* the scroller: through a bar of cycling colours */
        static const char MSG[] = "    CRT-4 PRESENTS PIP ... GREETINGS TO EVERY CHIP ...    ";
        char t[18];
        int i, off = (int)((s->ms / 60u) % (sizeof MSG - 17));
        for (i = 0; i < 17; i++)
            t[i] = MSG[off + i];
        t[17] = 0;
        cv_rect(SCX + 16, SCY + 82, 200, 8, VIC[(s->frame / 3u) % 16u == 6 ? 1 : (s->frame / 3u) % 16u]);
        cv_ptext(SCX + 18, SCY + 82, t, 1, VIC[0]);
    }
}

/* NES: a side-scroller. The sky, hills and clouds, blocks of the world's own, the ground; Pip runs (the
 * bass moves the world on), jumps at the kick and bumps the blocks with the lead's notes; a note pops out. */
static void scn_nes(const scn_t *s)
{
    static const uint16_t SKY = C16(92, 148, 252), HILL = C16(0, 168, 0), DARK = C16(0, 88, 0), BRK = C16(200, 76, 12),
                          BRD = C16(0, 0, 0), GND = C16(228, 92, 16), CL = C16(252, 252, 252), GOLD = C16(252, 188, 60);
    int x, y, k, scroll = (int)(s->ncount[1] * 2u + s->steps) % 64;
    grect(0, 0, GW, GH, SKY);
    for (k = 0; k < 3; k++) {                      /* clouds, slower than the world */
        int cx = ((k * 47 + 140 - scroll / 2) % 140) - 12;
        gdisc(cx, 8 + k * 3, 3, CL);
        gdisc(cx + 4, 7 + k * 3, 4, CL);
        gdisc(cx + 8, 8 + k * 3, 3, CL);
    }
    for (k = 0; k < 3; k++) {                      /* hills */
        int hx = ((k * 53 + 160 - scroll) % 160) - 20;
        for (y = 0; y < 10; y++)
            grect(hx - y, 38 - y + 1, 2 * y + 1, 1, y < 2 ? DARK : HILL);
    }
    for (k = 0; k < 4; k++) {                      /* blocks: bumped by the lead's notes */
        int bx = ((k * 29 + 120 - scroll) % 120) - 4, bump = (int)(s->ncount[3] % 4u) == k && s->nago[3] < 180;
        int by = 22 - (bump ? 2 : 0);
        grect(bx, by, 8, 8, BRD);
        grect(bx + 1, by + 1, 6, 6, k == 1 ? GOLD : BRK);
        if (k != 1) {
            grect(bx + 1, by + 4, 6, 1, BRD);
            gp(bx + 4, by + 1, BRD);
            gp(bx + 2, by + 5, BRD);
        } else {
            static const char *const NOTE[] = {"..11", "..1.", "..1.", "111.", "11..", 0};
            static const uint16_t P[2] = {0, C16(136, 20, 0)};
            gspr(bx + 2, by + 1, NOTE, P, 0);
        }
        if (bump) {                                /* the note, popping out */
            static const char *const NOTE[] = {"..11", "..1.", "111.", 0};
            static const uint16_t P[2] = {0, CL};
            gspr(bx + 2, by - 6 - (int)(s->nago[3] / 40u), NOTE, P, 0);
        }
    }
    for (x = 0; x < GW; x++)                       /* the ground */
        for (y = 39; y < GH; y++)
            gp(x, y, ((x + scroll) % 8 == 0 || y == 39 || y == 43) ? BRD : GND);
    {
        static const uint16_t P[4] = {C16(0, 0, 0), C16(248, 56, 0), C16(252, 216, 168), C16(0, 0, 0)};
        float j = since(s->ago[0], 0.4f);
        int py = 29 - (int)(j * (1.0f - j) * 4.0f * 10.0f);
        pip_draw(24, py, 2, (int)(s->ncount[1] & 3u), P, j > 0.0f, 0);
    }
    {   /* the score */
        char t[16] = "PIP ";
        uint32_t sc = (s->ncount[1] + s->ncount[2] + s->ncount[3]) * 10u;
        int i;
        for (i = 9; i >= 4; i--) {
            t[i] = (char)('0' + sc % 10u);
            sc /= 10u;
        }
        t[10] = 0;
        cv_ptext(SCX + 4, SCY + 1, t, 1, CL);
    }
}

/* PC ENGINE: a horizontal shooter in deep space. Stars in three layers, a ringed planet, Pip's little ship
 * bobbing with the bass and firing the lead, enemies in waves that burst on the snare (big, bright) */
static void scn_pce(const scn_t *s)
{
    int k;
    grect(0, 0, GW, GH, C16(0, 0, 28));
    for (k = 0; k < 40; k++) {                     /* three layers of stars */
        int layer = k % 3, sp = layer + 1, sx = (int)((srnd((uint32_t)k) % 116u + 1000u - (s->frame * (uint32_t)sp) / 2u % 116u) % 116u);
        gp(sx, (int)(srnd((uint32_t)k + 99u) % 46u), layer == 2 ? C16(255, 255, 255) : layer ? C16(150, 150, 220) : C16(80, 80, 140));
    }
    {   /* the planet */
        int px = 86 - (int)((s->frame / 20u) % 40u), py = 14;
        gdisc(px, py, 9, C16(200, 90, 40));
        gdisc(px - 3, py - 3, 4, C16(240, 150, 80));
        gline(px - 14, py + 3, px + 14, py - 3, C16(255, 220, 140));
    }
    {   /* Pip's ship */
        static const char *const SHIP[] = {"..11....", ".1221...", "11111111", "1333333.", ".1111...", 0};
        static const uint16_t P[4] = {0, C16(0, 200, 255), C16(255, 255, 255), C16(255, 136, 0)};
        int sy = 20 + (int)(3.0f * fm_sinf((float)s->ncount[1] * 0.9f)) - (s->note[3] > 0 ? (s->note[3] % 12) / 3 : 0);
        gspr(14, sy, SHIP, P, 0);
        gdisc(17, sy - 1, 1, C16(255, 230, 0));    /* Pip, under the canopy */
        if (s->nago[3] < 300)
            grect(23 + (int)(s->nago[3] / 6u), sy + 2, 6, 1, C16(255, 255, 120));
        if ((s->frame & 1u))
            gp(13, sy + 3, C16(255, 136, 0));      /* the exhaust */
    }
    for (k = 0; k < 4; k++) {                      /* the enemies: a wave, bursting on the snare */
        int ex = 104 - (int)((s->frame / 2u + (uint32_t)k * 30u) % 120u) + 10, ey = 8 + k * 9;
        int hit = s->ago[1] < 250 && (int)(s->ncount[0] % 4u) == k;
        if (hit) {
            int r = 1 + (int)((250u - s->ago[1]) / 60u);
            gdisc(ex, ey, r + 1, C16(255, 60, 120));
            gdisc(ex, ey, r, C16(255, 230, 0));
        } else {
            gdisc(ex, ey, 3, C16(255, 60, 120));
            gp(ex - 1, ey, C16(255, 255, 255));
        }
    }
}

/* GENESIS: a city at night, at speed. Buildings in three layers scrolling at their own rates (the faster
 * the nearer), windows lit, a neon sign that flickers on the snare, the road's dithered glow; Pip runs
 * flat out (speed lines), leaps to the lead; the kick shakes the camera */
static void scn_gen(const scn_t *s)
{
    static const uint16_t SKY0 = C16(16, 0, 48), SKY1 = C16(72, 0, 96);
    int x, y, k, shake = s->ago[0] < 90 ? 1 : 0, sp = (int)(s->frame * 3u);
    for (y = 0; y < 30; y++)                       /* the sky: a gradient, dithered in two colours */
        for (x = 0; x < GW; x++)
            gp(x, y + shake, ((x + y) & 1) && y > 12 + (int)(srnd((uint32_t)x) % 6u) ? SKY1 : SKY0);
    for (k = 0; k < 3; k++) {                      /* three layers of buildings */
        static const uint16_t L[3] = {C16(40, 20, 80), C16(24, 24, 56), C16(8, 8, 24)};
        int sp_k = sp * (k + 1) / 4;
        for (x = 0; x < GW; x++) {
            int wx = x + sp_k, bh = 8 + (int)(srnd((uint32_t)(wx / 9 + k * 100)) % 14u) + k * 4, top = 38 - bh;
            grect(x, top + shake, 1, bh, L[k]);
            if (k == 2 && (wx % 9) > 1 && (wx % 9) < 7 && ((wx / 3) & 1))
                for (y = top + 2; y < 36; y += 3)
                    if (srnd((uint32_t)(wx * 31 + y)) % 3u == 0u)
                        gp(x, y + shake, C16(255, 216, 80));
        }
    }
    {   /* the neon sign */
        int on = !(s->ago[1] < 140 && (s->frame & 2u));
        cv_ptext(SCX + 150 - (int)((s->frame * 2u) % 260u) + 100, SCY + 14 + 2 * shake, "NITE", 1, on ? C16(255, 72, 200) : C16(80, 20, 60));
    }
    for (y = 38; y < GH; y++)                      /* the road */
        for (x = 0; x < GW; x++)
            gp(x, y, (((x + sp) / 6 + y) & 1) && y == 42 ? C16(255, 216, 0) : y == 38 ? C16(36, 144, 255) : C16(20, 20, 30));
    {
        static const uint16_t P[4] = {C16(0, 0, 0), C16(36, 144, 255), C16(180, 220, 255), C16(255, 255, 255)};
        float j = since(s->nago[3], 0.35f);
        int py = 26 - (int)(j * (1.0f - j) * 4.0f * 9.0f) + shake;
        for (k = 0; k < 4; k++)                    /* speed lines */
            grect(8 - (int)((s->frame + (uint32_t)k * 5u) % 7u), py + 2 + k * 2, 6, 1, C16(180, 220, 255));
        pip_draw(22, py, 3, (int)(s->frame / 2u & 3u), P, j > 0.0f, 0);
    }
}

/* GAME BOY: a route seen from above, in its four greens. Grass, a path, trees, patches of tall grass that
 * rustle with the drums; Pip walks a step a note; an accent starts a "battle" (the screen flashes and
 * wipes). The LCD is slow: each frame fades in over the last (the ghosting). */
static void scn_gb(const scn_t *s)
{
    static const uint16_t G[4] = {C16(155, 188, 15), C16(139, 172, 15), C16(48, 98, 48), C16(15, 56, 15)};
    int x, y, k, walk = (int)(s->ncount[1] + s->ncount[2] + s->ncount[3]);
    int battle = (s->acc[3] || s->acc[1]) && s->nago[3] < 500 ? 1 : 0;
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++) {
            int wy = y + walk, tile = ((x / 8) + (wy / 8)) & 1, path = x > 50 && x < 66;
            uint16_t c = path ? G[0] : G[1];
            if (!path && tile && ((x + wy) % 4 == 0))
                c = G[2];                          /* grass tufts */
            if (!path && (x / 8 + wy / 8 * 3) % 7 == 0 && (x % 8 < 7) && (wy % 8 < 7))
                c = ((x + wy + (s->ago[7] < 150 ? 1 : 0)) & 1) ? G[2] : G[3];   /* tall grass, rustling */
            gp(x, y, c);
        }
    for (k = 0; k < 6; k++) {                      /* trees beside the path */
        int ty = (k * 16 - walk % 16 + 64) % 64 - 10, tx = k & 1 ? 74 : 34;
        gdisc(tx, ty, 5, G[3]);
        gdisc(tx - 1, ty - 1, 3, G[2]);
        grect(tx - 1, ty + 5, 2, 3, G[3]);
    }
    {
        static const uint16_t P[4] = {C16(15, 56, 15), C16(155, 188, 15), C16(155, 188, 15), C16(15, 56, 15)};
        pip_draw(58, 20, 3, walk & 3, P, 0, 0);
    }
    if (battle) {                                  /* the wild encounter: a flash, then a wipe */
        uint32_t t = s->nago[3];
        if (t < 200) {
            if ((t / 50u) & 1u)
                grect(0, 0, GW, GH, G[3]);
        } else {
            int w = (int)((t - 200u) * GW / 300u);
            for (y = 0; y < GH; y += 2)
                grect(y & 2 ? GW - w : 0, y, w, 2, G[3]);
        }
    }
}

/* SNES: an airship over a world map that turns (Mode 7: the map a plane under the camera, each line of the
 * screen a step further, its scale and rotation set line by line), a sky in bands over the horizon (the
 * HDMA's gradient), clouds half seen through (colour math). The bass turns the map, the lead lifts the ship,
 * the kick spins its propeller. */
static float sn_noise(int u, int v, int cell)  /* value noise: a random height a cell, blended between (repeating
                                                * every 1024: the map wraps, as Mode 7's did) */
{
    int cu = u >> cell, cv = v >> cell, k, per = (1024 >> cell) - 1;
    float fu = (float)(u & ((1 << cell) - 1)) / (float)(1 << cell), fv = (float)(v & ((1 << cell) - 1)) / (float)(1 << cell), h[4];
    for (k = 0; k < 4; k++)
        h[k] = (float)(srnd((uint32_t)(((cu + (k & 1)) & per) * 73856093) ^ (uint32_t)(((cv + (k >> 1)) & per) * 19349663)) & 255u);
    fu = fu * fu * (3.0f - 2.0f * fu);
    fv = fv * fv * (3.0f - 2.0f * fv);
    return (h[0] + (h[1] - h[0]) * fu) * (1.0f - fv) + (h[2] + (h[3] - h[2]) * fu) * fv;
}
/* the world, 1024 x 1024 in texels of 4 x 4: seas (with a grid), coasts, plains, forests, mountains, snow;
 * a kind a texel, two a byte, made the first time it is seen */
static const uint16_t SN_KIND[8] = {C16(32, 72, 184), C16(40, 88, 200), C16(232, 216, 152), C16(80, 176, 72),
                                    C16(24, 104, 40), C16(32, 120, 48), C16(136, 120, 104), C16(220, 216, 208)};
static uint8_t sn_tab[256 * 128] EN_POOL;
static uint8_t sn_made;
static void sn_make(void)
{
    int tu, tv;
    for (tv = 0; tv < 256; tv++)
        for (tu = 0; tu < 256; tu++) {
            int u = tu * 4 + 2, v = tv * 4 + 2, k;
            float n = 0.65f * sn_noise(u, v, 7) + 0.35f * sn_noise((u + 999) & 1023, (v + 777) & 1023, 5);
            if (n < 112.0f)
                k = ((tu & 7) == 0 || (tv & 7) == 0) ? 1 : 0;
            else if (n < 122.0f)
                k = 2;
            else if (n < 160.0f)
                k = 3;
            else if (n < 185.0f)
                k = (tu ^ tv) & 1 ? 4 : 5;
            else
                k = n < 200.0f ? 6 : 7;
            sn_tab[tv * 128 + (tu >> 1)] |= (uint8_t)(k << ((tu & 1) * 4));
        }
    sn_made = 1;
}
static void scn_snes(const scn_t *s)
{
    float ang = (float)s->ncount[1] * 0.06f + (float)s->ms * 0.00004f, ca = fm_cosf(ang), sa = fm_sinf(ang);
    float px = 512.0f + (float)s->ms * 0.01f * ca, py = 512.0f + (float)s->ms * 0.01f * sa;
    int x, y, hor = 30;
    if (!sn_made)
        sn_make();
    for (y = 0; y < SH; y++) {
        if (y < hor) {                             /* the sky: bands, one colour a line */
            uint16_t c = mix565(C16(40, 48, 160), C16(248, 176, 120), y * 256 / hor);
            cv_rect(SCX, SCY + y, SW, 1, c);
        } else {                                   /* the plane: this line's distance, then a step a pixel */
            float d = 600.0f / (float)(y - hor + 6);
            /* u, v in 1/64ths of the world's units: a texel is 256 of them */
            int32_t u = (int32_t)((px + d * (ca * -1.0f - sa * 1.6f) * 30.0f) * 64.0f);
            int32_t v = (int32_t)((py + d * (sa * -1.0f + ca * 1.6f) * 30.0f) * 64.0f);
            int32_t du = (int32_t)(d * ca * 120.0f / (float)SW * 64.0f), dv = (int32_t)(d * sa * 120.0f / (float)SW * 64.0f);
            uint16_t pal[8], *r = srow(y);
            int k;
            for (k = 0; k < 8; k++)                /* the haze at the horizon */
                pal[k] = swap16(y < hor + 6 ? mix565(SN_KIND[k], C16(248, 176, 120), (hor + 6 - y) * 40) : SN_KIND[k]);
            for (x = 0; x < SW; x += 2) {
                uint32_t tu = (uint32_t)(u >> 8) & 255u, tv = (uint32_t)(v >> 8) & 255u;
                uint16_t c = pal[(sn_tab[tv * 128u + (tu >> 1)] >> ((tu & 1u) * 4u)) & 7u];
                r[x] = c;
                r[x + 1] = c;
                u += du;
                v += dv;
            }
        }
    }
    {   /* the airship and its shadow */
        static const char *const SHIP[] = {"....1111....", "..11222211..", ".1222222221.", "1333333333331", ".1111111111.", "...14..41...", 0};
        static const uint16_t P[5] = {0, C16(64, 40, 24), C16(200, 72, 72), C16(160, 112, 64), C16(248, 248, 248)};
        int alt = 6 + (s->note[3] > 0 ? (s->note[3] % 24) / 3 : 4), sx = 52;
        gsx = gsy = 2;
        grect(sx + 2, 38, 10, 2, C16(24, 40, 32));
        gspr(sx, 22 - alt, SHIP, P, 0);
        gdisc(sx + 6, 22 - alt - 2, 1, C16(248, 120, 32));   /* Pip, at the wheel */
        if ((s->frame & 1u) || s->ago[0] < 100)
            grect(sx - 2, 24 - alt, 2, 3, C16(248, 248, 248));   /* the propeller */
    }
    {   /* a cloud, half there */
        int cx = (int)((s->ms / 40u) % 300u) - 40, cy = 12, i;
        for (i = -8; i <= 8; i++)
            for (y = -3; y <= 3; y++)
                if (i * i + 4 * y * y < 64 && SCX + cx + i * 2 >= SCX && cx + i * 2 < SW - 2 && cx + i * 2 >= 0) {
                    uint16_t c = sread(cx + i * 2, cy + y);
                    swrite(cx + i * 2, cy + y, mix565(c, C16(255, 255, 255), 128));
                    swrite(cx + i * 2 + 1, cy + y, mix565(sread(cx + i * 2 + 1, cy + y), C16(255, 255, 255), 128));
                }
    }
}

#include "scenes3d.c"

/* -------------------------------------------------------------- power-on --- */
/* each console's start, CRT-4's own homage to its era's: about two seconds */
#define BOOT_MS 2200u
static void boot_draw(uint32_t t)
{
    const theme_t *T = th();
    int k;
    switch (en_con) {
    case CON_2600:                             /* colour bands sweeping, then the title, chunky */
        gsx = 4;
        gsy = 2;
        for (k = 0; k < GH; k++)
            grect(0, k, 58, 1, RGB((uint32_t)(k * 9 + t / 8u) & 255u, (uint32_t)(k * 5 + t / 5u) & 255u, 140));
        gsx = gsy = 2;
        if (t > 900u) {
            grect(10, 12, 96, 22, RGB(0, 0, 0));
            cv_ptext(SCX + 52, SCY + 32, "CRT-4", 3, T->ink);
        }
        break;
    case CON_C64: {                            /* the blue screen, its words, then LOAD and the stripes */
        cv_rect(SCX, SCY, SW, SH, T->bezel);
        cv_rect(SCX + 12, SCY + 8, SW - 24, SH - 16, T->scr);
        cv_ptext(SCX + 18, SCY + 12, "*** CRT-4 SOUND SYSTEM ***", 1, T->ink);
        if (t > 400u)
            cv_ptext(SCX + 18, SCY + 26, "READY.", 1, T->ink);
        if (t > 800u)
            cv_ptext(SCX + 18, SCY + 36, "LOAD \"PIP\",8,1", 1, T->ink);
        if (t > 1200u) {
            cv_ptext(SCX + 18, SCY + 50, "LOADING", 1, T->ink);
            for (k = 0; k < SH; k += 3)
                cv_rect(SCX, SCY + k, 12, 3, RGB(srnd((uint32_t)k + t / 40u) & 255u, srnd((uint32_t)k * 3u + t) & 255u, 200));
        }
        if (t > 1800u)
            cv_ptext(SCX + 18, SCY + 62, "RUN", 1, T->ink);
        if (((t / 300u) & 1u))
            cv_rect(SCX + 18 + (t > 1800u ? 26 : t > 800u ? 0 : 0), SCY + (t > 1800u ? 72 : t > 800u ? 46 : 36), 6, 8, T->ink);
        break;
    }
    case CON_NES:                              /* black, the title, PUSH START blinking */
        cv_rect(SCX, SCY, SW, SH, RGB(0, 0, 0));
        if (t > 300u)
            cv_ptext(SCX + 116 - ptext_w("CRT-4", 4) / 2, SCY + 14, "CRT-4", 4, RGB(248, 56, 0));
        if (t > 900u && ((t / 250u) & 1u))
            cv_ptext(SCX + 116 - ptext_w("PUSH START", 1) / 2, SCY + 62, "PUSH START", 1, RGB(252, 252, 252));
        break;
    case CON_PCE:                              /* a white flash, the title in chrome */
        cv_rect(SCX, SCY, SW, SH, t < 300u ? RGB(255, 255, 255) : RGB(0, 0, 28));
        if (t > 300u) {
            int y;
            for (y = 0; y < 24; y++)
                cv_rect(SCX + 40, SCY + 20 + y, 152, 1, mix565(RGB(255, 255, 255), RGB(255, 136, 0), y * 10));
            cv_ptext(SCX + 116 - ptext_w("CRT-4", 3) / 2, SCY + 21, "CRT-4", 3, RGB(0, 0, 28));
            cv_ptext(SCX + 116 - ptext_w("STARRING PIP", 1) / 2, SCY + 60, "STARRING PIP", 1, RGB(255, 255, 255));
        }
        break;
    case CON_GEN: {                            /* the name, and a shine sweeping across it */
        int sx = (int)(t / 4u) - 40;
        cv_rect(SCX, SCY, SW, SH, RGB(0, 0, 0));
        cv_ptext(SCX + 116 - ptext_w("CRT-4", 4) / 2, SCY + 28, "CRT-4", 4, RGB(36, 108, 255));
        if (sx > 0 && sx < SW) {
            int y;
            for (y = 28; y < 60; y++) {
                int xx = sx - (y - 28) / 2;
                if (xx >= 0 && xx < SW - 4) {
                    int i;
                    for (i = 0; i < 4; i++)
                        if (sread(xx + i, y) != RGB(0, 0, 0))
                            swrite(xx + i, y, RGB(255, 255, 255));
                }
            }
        }
        break;
    }
    case CON_GB: {                             /* the name coming down from the top */
        int y = t < 1400u ? -12 + (int)(t * 50u / 1400u) : 38;
        cv_rect(SCX, SCY, SW, SH, T->scr);
        if (y > -12)
            cv_ptext(SCX + 116 - ptext_w("CRT-4", 3) / 2, SCY + y, "CRT-4", 3, T->ink);
        break;
    }
    case CON_SNES: {                           /* a fade from black, the name, a sparkle */
        int a = t < 800u ? (int)(t * 256u / 800u) : 256;
        int y;
        for (y = 0; y < SH; y++)
            cv_rect(SCX, SCY + y, SW, 1, mix565(RGB(0, 0, 0), mix565(RGB(24, 40, 140), RGB(120, 112, 200), y * 3), a));
        cv_ptext(SCX + 116 - ptext_w("CRT-4", 4) / 2, SCY + 26, "CRT-4", 4, mix565(RGB(0, 0, 0), RGB(248, 248, 248), a));
        if (t > 1000u) {
            int sx = SCX + 60 + (int)((t - 1000u) / 6u) % 120;
            cv_rect(sx, SCY + 24, 1, 7, RGB(255, 255, 255));
            cv_rect(sx - 3, SCY + 27, 7, 1, RGB(255, 255, 255));
        }
        break;
    }
    default:
        boot3d_draw(t);                        /* the PlayStation's and the N64's are in 3D */
        break;
    }
}

/* the shelf: every console, as a cartridge (or a disc), the one ALGORITHM points at lifted out */
static void shelf_draw(void)
{
    const theme_t *T = th();
    int k;
    window(4, 28, 232, 92);
    for (k = 0; k < EN_NCON; k++) {
        int x = 12 + k * 24, sel = k == ui.shelf_pos, y = sel ? 40 : 48;
        const theme_t *K = &THEMES[k];
        cv_rect(x, y, 20, 34, K->bezel == K->scr ? K->line : K->panel);
        cv_rect(x + 2, y + 4, 16, 14, K->scr);
        cv_rect(x + 4, y + 8, 12, 2, K->trk[0]);
        cv_rect(x + 4, y + 12, 12, 2, K->trk[1]);
        if (sel)
            cv_rect(x, y + 36, 20, 2, T->winb);
    }
    {
        char b[24];
        caps(b, en_console(ui.shelf_pos)->name, sizeof b);
        ttext_c(120, 92, 1, b, T->winb);
    }
}

static void scene_boot(void)
{
    ui.boot_t = plat_ms();
    ui.booting = 1;
}

static void draw_scene(void)
{
    scn_t s;
    int i;
    uint32_t now = plat_ms();
    /* what moved: a track's note, a part's hit (counted by the engine; their times kept here) */
    for (i = 0; i < EN_NTRACK; i++)
        if (en_trk_on[i] != ui.seen_on[i]) {
            ui.seen_on[i] = en_trk_on[i];
            ui.note_t[i] = now;
            ui.ncount[i]++;
        }
    for (i = 0; i < EN_NPART; i++)
        if (en_part_on[i] != ui.seen_part[i]) {
            ui.seen_part[i] = en_part_on[i];
            ui.part_t[i] = now;
        }
    if (ui.frame % 2u)                           /* 30 frames a second */
        return;
    band(B1_Y, B1_H, th()->body);
    screen_glass();
    UI_MARK_(7);
    gsx = gsy = 2;
    if (ui.help) {
        draw_help();
    } else if (ui.shelf) {
        shelf_draw();
    } else if (ui.booting && now - ui.boot_t < BOOT_MS) {
        boot_draw(now - ui.boot_t);
        scanlines(th()->scan);
    } else {
        ui.booting = 0;
        memset(&s, 0, sizeof s);
        s.ms = now;
        s.frame = ui.frame / 2u;
        s.playing = en_playing;
        s.step = en_step;
        s.steps = en_steps;
        s.beat = (int)(en_steps / 4u);
        for (i = 0; i < EN_NPART; i++)
            s.ago[i] = now - ui.part_t[i] > 60000u ? 60000u : now - ui.part_t[i];
        for (i = 0; i < EN_NTRACK; i++) {
            s.nago[i] = now - ui.note_t[i] > 60000u ? 60000u : now - ui.note_t[i];
            s.ncount[i] = ui.ncount[i];
            s.note[i] = en_trk_note[i];
            s.acc[i] = en_trk_acc[i];
            s.lvl[i] = en_level[i];
        }
        s.gate = en_gates;
        s.chord = en_cfg.chords_n ? (en_chord_now == 0xFF ? en_cfg.chords[0] : en_chord_now) : -1;
        s.tempo = en_cfg.par[P_TEMPO];
        switch (en_con) {
        case CON_2600: scn_2600(&s); break;
        case CON_C64: scn_c64(&s); break;
        case CON_NES: scn_nes(&s); break;
        case CON_PCE: scn_pce(&s); break;
        case CON_GEN: scn_gen(&s); break;
        case CON_GB: scn_gb(&s); break;
        case CON_SNES: scn_snes(&s); break;
        case CON_PS1: scn_ps1(&s); break;
        default: scn_n64(&s); break;
        }
        UI_MARK_(8);
        scanlines(th()->scan);
        if (en_con == CON_GB)
            gb_ghost();
    }
    UI_MARK_(9);
    cv_blit(0, B1_Y);
}
