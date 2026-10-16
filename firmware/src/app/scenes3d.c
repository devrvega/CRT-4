/* SPDX-License-Identifier: GPL-3.0-only */
/* The 3D consoles' scenes, drawn as they drew: triangles sorted back to front (the PlayStation had no depth
 * buffer; the N64 did, but a painter's order does the same here), each flat, shaded across its corners, or
 * textured.
 *   PlayStation   corners snapped to whole pixels (no sub-pixel: the polygons wobble as they move), its
 *                 textures stepped straight across the screen (affine: they warp), colours dithered to
 *                 15 bits
 *   N64           textures stepped in depth (perspective-correct), fog with distance, and the whole frame
 *                 softened (its filtering and its anti-aliasing blur)
 * Part of scenes.c. */

static inline float fm_fmodf_(float x, float m) { return x - m * fm_floorf(x / m); }
typedef struct {
    float x, y, z;                     /* in the camera's space, then x, y on the screen */
    float u, v, l;                     /* the texture's place, the light */
} vtx_t;
typedef struct {
    vtx_t a, b, c;
    float depth;
    uint16_t col;                      /* flat colour (the texture's tint) */
    uint8_t tex;                       /* 0 none, else a texture */
} tri_t;
#define NTRI 160
static tri_t tris[NTRI] EN_POOL;
static int ntri;
static float cam_x, cam_y, cam_z, cam_yaw, cam_pitch, cam_f = 140.0f;
static float cam_cy = 1.0f, cam_sy, cam_cp = 1.0f, cam_sp;   /* the turns' cosines and sines (set with the camera) */
static int ps1_mode;                   /* 1: the PlayStation's ways; 0: the N64's */
static int two_sided;                  /* draw faces turned away too (the tunnel, seen from inside) */
static uint16_t fog_c;
static float fog_near = 40.0f, fog_far = 120.0f;

/* the textures: made by a rule, 32 x 32 */
static uint16_t tex_px(int t, int u, int v)
{
    u &= 31;
    v &= 31;
    switch (t) {
    case 1:                            /* the tunnel's panels: a neon line, a dark plate, rivets */
        if (v < 2 || u < 2)
            return RGB(0, 230, 255);
        if ((u == 8 || u == 24) && (v == 8 || v == 24))
            return RGB(140, 140, 160);
        return ((u + v) & 8) ? RGB(30, 34, 60) : RGB(24, 26, 48);
    case 2:                            /* the track: chevrons */
        return (((u + (v >= 16 ? 32 - v : v)) & 15) < 6) ? RGB(255, 200, 40) : RGB(40, 40, 52);
    case 3:                            /* grass */
        return ((srnd((uint32_t)(u * 33 + v)) & 7u) < 2u) ? RGB(60, 150, 40) : RGB(76, 176, 52);
    case 4:                            /* sand */
        return ((u ^ v) & 4) ? RGB(232, 210, 150) : RGB(220, 196, 136);
    case 5:                            /* bark */
        return (u & 4) ? RGB(110, 70, 40) : RGB(90, 56, 30);
    default: return RGB(255, 0, 255);
    }
}

static uint16_t tex_tab[5][32 * 32] EN_POOL;     /* the textures, made once by the rule above */
static uint8_t tex_made;
static void tex_make(void)
{
    int t, u, v;
    for (t = 0; t < 5; t++)
        for (v = 0; v < 32; v++)
            for (u = 0; u < 32; u++)
                tex_tab[t][v * 32 + u] = tex_px(t + 1, u, v);
    tex_made = 1;
}

static void cam_set(float x, float y, float z, float yaw, float pitch)
{
    cam_x = x;
    cam_y = y;
    cam_z = z;
    cam_yaw = yaw;
    cam_pitch = pitch;
    cam_cy = fm_cosf(yaw);
    cam_sy = fm_sinf(yaw);
    cam_cp = fm_cosf(pitch);
    cam_sp = fm_sinf(pitch);
}
/* a world point into the camera's space, then onto the screen (z > 0 ahead) */
static void to_cam(vtx_t *v, float wx, float wy, float wz)
{
    float cy = cam_cy, sy = cam_sy, cp = cam_cp, sp = cam_sp;
    float x = wx - cam_x, y = wy - cam_y, z = wz - cam_z, x1, z1, y1, z2;
    x1 = x * cy - z * sy;
    z1 = x * sy + z * cy;
    y1 = y * cp + z1 * sp;             /* (pitch > 0: looking down) */
    z2 = -y * sp + z1 * cp;
    v->x = x1;
    v->y = y1;
    v->z = z2;
}
static void project(vtx_t *v)
{
    float iz = 1.0f / v->z;
    v->x = (float)(SW / 2) + v->x * cam_f * iz;
    v->y = (float)(SH / 2) - v->y * cam_f * iz;
    if (ps1_mode) {                    /* no sub-pixel: the corners on whole pixels */
        v->x = (float)(int)(v->x + 0.5f);
        v->y = (float)(int)(v->y + 0.5f);
    }
}
/* a triangle in the world: in camera space, culled if behind, kept for drawing */
static void tri_add(const float *p0, const float *p1, const float *p2, const float *uv, uint16_t col, int tex, float light)
{
    tri_t *t;
    if (ntri >= NTRI)
        return;
    t = &tris[ntri];
    to_cam(&t->a, p0[0], p0[1], p0[2]);
    to_cam(&t->b, p1[0], p1[1], p1[2]);
    to_cam(&t->c, p2[0], p2[1], p2[2]);
    if (t->a.z < 2.0f || t->b.z < 2.0f || t->c.z < 2.0f)
        return;                        /* (too near, or behind: dropped, as the games did) */
    t->depth = t->a.z + t->b.z + t->c.z;
    t->a.u = uv ? uv[0] : 0.0f;
    t->a.v = uv ? uv[1] : 0.0f;
    t->b.u = uv ? uv[2] : 0.0f;
    t->b.v = uv ? uv[3] : 0.0f;
    t->c.u = uv ? uv[4] : 0.0f;
    t->c.v = uv ? uv[5] : 0.0f;
    t->a.l = t->b.l = t->c.l = light;
    project(&t->a);
    project(&t->b);
    project(&t->c);
    {   /* facing away: dropped */
        float cr = (t->b.x - t->a.x) * (t->c.y - t->a.y) - (t->b.y - t->a.y) * (t->c.x - t->a.x);
        if (cr >= 0.0f && !two_sided)
            return;
    }
    t->col = col;
    t->tex = (uint8_t)tex;
    ntri++;
}
static void quad_add(const float *p0, const float *p1, const float *p2, const float *p3, uint16_t col, int tex, float light, float ts)
{
    float uv1[6] = {0, 0, ts, 0, ts, ts}, uv2[6] = {0, 0, ts, ts, 0, ts};
    tri_add(p0, p1, p2, tex ? uv1 : 0, col, tex, light);
    tri_add(p0, p2, p3, tex ? uv2 : 0, col, tex, light);
}

static const int8_t BAYER[4][4] = {{-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}};

/* one triangle: scanline by scanline, its corners' values stepped across (affine; or, on the N64, the
 * texture divided by depth: perspective-correct). The light steps in fixed point; the fog is the
 * triangle's (its depth); a pixel is shaded in integers. */
static void tri_draw(const tri_t *t)
{
    const vtx_t *v[3] = {&t->a, &t->b, &t->c}, *tmp;
    int y, y0, y1, fog = 0, fr = 0, fg = 0, fb = 0;
    const uint16_t *tx = t->tex >= 1 && t->tex <= 5 ? tex_tab[t->tex - 1] : 0;
    uint32_t cr = (uint32_t)(t->col >> 11) << 3, cg = (uint32_t)((t->col >> 5) & 63u) << 2, cb = (uint32_t)(t->col & 31u) << 3;
    /* sorted by y */
    if (v[1]->y < v[0]->y) { tmp = v[0]; v[0] = v[1]; v[1] = tmp; }
    if (v[2]->y < v[0]->y) { tmp = v[0]; v[0] = v[2]; v[2] = tmp; }
    if (v[2]->y < v[1]->y) { tmp = v[1]; v[1] = v[2]; v[2] = tmp; }
    if (v[2]->y - v[0]->y < 0.001f)
        return;
    {
        float depth = t->depth * (1.0f / 3.0f);
        if (!ps1_mode && depth > fog_near) {   /* the N64's fog */
            fog = (int)((depth - fog_near) * 256.0f / (fog_far - fog_near));
            if (fog > 256)
                fog = 256;
            fr = (int)(fog_c >> 11) << 3;
            fg = (int)((fog_c >> 5) & 63u) << 2;
            fb = (int)(fog_c & 31u) << 3;
        }
    }
    y0 = (int)fm_maxf(0.0f, fm_floorf(v[0]->y));
    y1 = (int)fm_minf((float)SH - 1.0f, fm_floorf(v[2]->y));
    {   /* each corner's values as they are stepped: u, v (over depth, on the N64), the depth's inverse */
        int persp = !ps1_mode && t->tex, k;
        float h02 = 1.0f / (v[2]->y - v[0]->y), h01 = v[1]->y - v[0]->y, h12 = v[2]->y - v[1]->y;
        float cu[3], cv[3], cz[3];
        h01 = h01 < 0.001f ? 0.0f : 1.0f / h01;
        h12 = h12 < 0.001f ? 0.0f : 1.0f / h12;
        for (k = 0; k < 3; k++) {
            float iz = persp ? 1.0f / v[k]->z : 1.0f;
            cu[k] = v[k]->u * iz;
            cv[k] = v[k]->v * iz;
            cz[k] = iz;
        }
    for (y = y0; y <= y1; y++) {
        float fy = (float)y + 0.5f, xa, xb, ua, ub, va, vb, la, lb, za, zb;
        int p, q;
        float tl, ts;
        /* the long edge (0-2) and the short one (0-1 or 1-2) */
        tl = (fy - v[0]->y) * h02;
        if (tl < 0.0f || tl > 1.0f)
            continue;
        if (fy < v[1]->y) {
            p = 0;
            q = 1;
            ts = (fy - v[0]->y) * h01;
        } else {
            p = 1;
            q = 2;
            ts = (fy - v[1]->y) * h12;
        }
        ts = fm_clampf(ts, 0.0f, 1.0f);
        xa = v[0]->x + (v[2]->x - v[0]->x) * tl;
        xb = v[p]->x + (v[q]->x - v[p]->x) * ts;
        ua = cu[0] + (cu[2] - cu[0]) * tl;
        ub = cu[p] + (cu[q] - cu[p]) * ts;
        va = cv[0] + (cv[2] - cv[0]) * tl;
        vb = cv[p] + (cv[q] - cv[p]) * ts;
        za = cz[0] + (cz[2] - cz[0]) * tl;
        zb = cz[p] + (cz[q] - cz[p]) * ts;
        la = v[0]->l + (v[2]->l - v[0]->l) * tl;
        lb = v[p]->l + (v[q]->l - v[p]->l) * ts;
        if (xa > xb) {
            float s0;
            s0 = xa; xa = xb; xb = s0;
            s0 = ua; ua = ub; ub = s0;
            s0 = va; va = vb; vb = s0;
            s0 = la; la = lb; lb = s0;
            s0 = za; za = zb; zb = s0;
        }
        {
            int x, x0 = (int)fm_maxf(0.0f, fm_floorf(xa + 0.5f)), x1 = (int)fm_minf((float)SW - 1.0f, fm_floorf(xb - 0.5f));
            float w = xb - xa > 0.001f ? 1.0f / (xb - xa) : 0.0f, f0;
            int32_t li, dli, ui = 0, dui = 0, vi = 0, dvi = 0;
            float u = 0.0f, du = 0.0f, vv = 0.0f, dv = 0.0f, iz = 1.0f, diz = 0.0f;
            const int8_t *dith = BAYER[y & 3];
            uint16_t *row = srow(y);
            if (x0 > x1)
                continue;
            f0 = ((float)x0 + 0.5f - xa) * w;
            li = (int32_t)((la + (lb - la) * f0) * 65536.0f);       /* the light, x 256 in 8.8 more */
            dli = (int32_t)((lb - la) * w * 65536.0f);
            if (tx) {
                u = (ua + (ub - ua) * f0) * 32.0f;
                du = (ub - ua) * w * 32.0f;
                vv = (va + (vb - va) * f0) * 32.0f;
                dv = (vb - va) * w * 32.0f;
                if (ps1_mode) {                    /* affine: the texture's place in 16.16 */
                    ui = (int32_t)(u * 65536.0f);
                    dui = (int32_t)(du * 65536.0f);
                    vi = (int32_t)(vv * 65536.0f);
                    dvi = (int32_t)(dv * 65536.0f);
                } else {
                    iz = za + (zb - za) * f0;
                    diz = (zb - za) * w;
                }
            }
            for (x = x0; x <= x1; x++) {
                int l = li < 0 ? 0 : li >> 8, r, g, b;
                uint32_t c;
                if (tx) {
                    if (ps1_mode) {
                        c = tx[((uint32_t)(vi >> 16) & 31u) * 32u + ((uint32_t)(ui >> 16) & 31u)];
                        ui += dui;
                        vi += dvi;
                    } else {                       /* exact every 8 pixels, stepped straight between */
                        if (((x - x0) & 7) == 0) {
                            float r0 = 1.0f / iz, r1 = 1.0f / fm_maxf(iz + diz * 8.0f, iz * 0.25f);
                            ui = (int32_t)(u * r0 * 65536.0f);
                            vi = (int32_t)(vv * r0 * 65536.0f);
                            dui = (int32_t)(((u + du * 8.0f) * r1 - u * r0) * 8192.0f);
                            dvi = (int32_t)(((vv + dv * 8.0f) * r1 - vv * r0) * 8192.0f);
                            u += du * 8.0f;
                            vv += dv * 8.0f;
                            iz += diz * 8.0f;
                        }
                        c = tx[((uint32_t)(vi >> 16) & 31u) * 32u + ((uint32_t)(ui >> 16) & 31u)];
                        ui += dui;
                        vi += dvi;
                    }
                    r = (int)((((c >> 11) << 3) * (uint32_t)l) >> 8);
                    g = (int)(((((c >> 5) & 63u) << 2) * (uint32_t)l) >> 8);
                    b = (int)((((c & 31u) << 3) * (uint32_t)l) >> 8);
                } else {
                    r = (int)((cr * (uint32_t)l) >> 8);
                    g = (int)((cg * (uint32_t)l) >> 8);
                    b = (int)((cb * (uint32_t)l) >> 8);
                }
                li += dli;
                if (fog) {
                    r += ((fr - r) * fog) >> 8;
                    g += ((fg - g) * fog) >> 8;
                    b += ((fb - b) * fog) >> 8;
                }
                if (ps1_mode) {                    /* dithered to 15 bits, as the PlayStation's were */
                    int d = dith[x & 3];
                    r += d;
                    g += d;
                    b += d;
                    r = r < 0 ? 0 : r;
                    g = g < 0 ? 0 : g;
                    b = b < 0 ? 0 : b;
                }
                r = r > 255 ? 255 : r;
                g = g > 255 ? 255 : g;
                b = b > 255 ? 255 : b;
                row[x] = swap16(RGB((uint32_t)r, (uint32_t)g, (uint32_t)b));
            }
        }
    }
    }
}
static void tris_draw(void)
{
    uint8_t ord[NTRI];
    int i, j;
    for (i = 0; i < ntri; i++) {       /* back to front: their numbers sorted (an insertion sort), not they */
        uint8_t k = (uint8_t)i;
        float d = tris[i].depth;
        j = i - 1;
        while (j >= 0 && tris[ord[j]].depth < d) {
            ord[j + 1] = ord[j];
            j--;
        }
        ord[j + 1] = k;
    }
    if (!tex_made)
        tex_make();
    for (i = 0; i < ntri; i++)
        tri_draw(&tris[ord[i]]);
    ntri = 0;
}
static void n64_soften(void)          /* the N64's softness: each pixel with its neighbours on the line */
{
    int x, y;
    for (y = 0; y < SH; y++) {
        uint16_t *r = srow(y);
        uint32_t p = spread565(swap16(r[0])), c = spread565(swap16(r[1]));
        for (x = 1; x < SW - 1; x++) {
            uint32_t n = spread565(swap16(r[x + 1]));
            r[x] = swap16(pack565((c * 20u + p * 6u + n * 6u) >> 5));
            p = c;
            c = n;
        }
    }
}

/* a box, its faces lit from above and the side */
static void box_add(float x, float y, float z, float w, float h, float d, uint16_t col, int tex)
{
    float p[8][3];
    int i;
    for (i = 0; i < 8; i++) {
        p[i][0] = x + ((i & 1) ? w : -w);
        p[i][1] = y + ((i & 2) ? h : 0.0f);
        p[i][2] = z + ((i & 4) ? d : -d);
    }
    quad_add(p[2], p[3], p[7], p[6], col, tex, 1.0f, 1.0f);     /* top */
    quad_add(p[0], p[1], p[3], p[2], col, tex, 0.8f, 1.0f);     /* front */
    quad_add(p[5], p[4], p[6], p[7], col, tex, 0.6f, 1.0f);     /* back */
    quad_add(p[4], p[0], p[2], p[6], col, tex, 0.7f, 1.0f);     /* left */
    quad_add(p[1], p[5], p[7], p[3], col, tex, 0.9f, 1.0f);     /* right */
}
/* Pip in polygons: a head of eight sides (two rings), eyes, a body, legs */
static void pip3d(float x, float y, float z, float yaw, uint16_t col, float hop)
{
    int k;
    float r = 3.0f, hy = y + 9.0f + hop;
    for (k = 0; k < 8; k++) {
        float a0 = (float)k * 0.785398f + yaw, a1 = a0 + 0.785398f;
        float top[3] = {x, hy + r, z}, bot[3] = {x, hy - r, z};
        float m0[3] = {x + r * fm_cosf(a0), hy, z + r * fm_sinf(a0)}, m1[3] = {x + r * fm_cosf(a1), hy, z + r * fm_sinf(a1)};
        float l = 0.65f + 0.35f * fm_cosf(a0 - 2.4f);
        tri_add(top, m1, m0, 0, col, 0, l);
        tri_add(bot, m0, m1, 0, col, 0, l * 0.8f);
    }
    {   /* the eyes, facing the camera's way */
        float ex = fm_cosf(yaw - 1.5708f) * (r + 0.1f), ez = fm_sinf(yaw - 1.5708f) * (r + 0.1f);
        float sx = fm_cosf(yaw) * 0.9f, sz = fm_sinf(yaw) * 0.9f;
        box_add(x + ex - sx, hy + 0.2f, z + ez - sz, 0.35f, 0.9f, 0.35f, RGB(16, 16, 16), 0);
        box_add(x + ex + sx, hy + 0.2f, z + ez + sz, 0.35f, 0.9f, 0.35f, RGB(16, 16, 16), 0);
    }
    box_add(x, y + 3.0f + hop, z, 2.2f, 3.4f, 1.6f, col, 0);    /* the body */
    box_add(x - 1.1f, y + hop * 0.5f, z, 0.6f, 3.0f, 0.6f, RGB(30, 30, 30), 0);   /* the legs */
    box_add(x + 1.1f, y + hop * 0.5f, z, 0.6f, 3.0f, 0.6f, RGB(30, 30, 30), 0);
}

/* PLAYSTATION: a race through a neon tunnel. Rings of panels rush at the camera (faster with the tempo,
 * a surge on the kick), the track glows with chevrons, Pip's hover-craft sways to the lead and banks to the
 * bass; the colours turn with the chord */
static void scn_ps1(const scn_t *s)
{
    int k, i;
    float speed = (float)s->ms * 0.0006f * (float)s->tempo / 120.0f, z0 = 60.0f - fm_fmodf_(speed * 40.0f, 10.0f);
    float sway = (s->note[3] > 0 ? (float)(s->note[3] % 12) - 6.0f : 0.0f) * 0.6f, bank = 0.2f * fm_sinf((float)s->ncount[1] * 0.7f);
    ps1_mode = 1;
    two_sided = 1;
    cv_rect(SCX, SCY, SW, SH, RGB(4, 2, 16));
    cam_set(0.0f, 6.0f, -14.0f, 0.0f, 0.08f);
    for (k = 0; k < 7; k++) {                      /* the rings: eight panels each */
        float z = z0 - (float)k * 10.0f + 30.0f, z1 = z + 10.0f;
        for (i = 0; i < 8; i++) {
            float a0 = (float)i * 0.785398f + bank, a1 = a0 + 0.785398f, R = 16.0f;
            float p0[3] = {R * fm_cosf(a0), 6.0f + R * fm_sinf(a0), z}, p1[3] = {R * fm_cosf(a1), 6.0f + R * fm_sinf(a1), z};
            float p2[3] = {R * fm_cosf(a1), 6.0f + R * fm_sinf(a1), z1}, p3[3] = {R * fm_cosf(a0), 6.0f + R * fm_sinf(a0), z1};
            float l = 0.5f + 0.5f * (s->ago[0] < 150 ? 1.0f : 0.6f);
            if (i == 6 || i == 5)
                quad_add(p1, p0, p3, p2, RGB(255, 200, 40), 2, l + 0.2f, 1.0f);   /* the floor: the track */
            else
                quad_add(p1, p0, p3, p2, RGB(0, 230, 255), 1, l * (0.6f + 0.1f * (float)i * 0.5f), 1.0f);
        }
    }
    {   /* Pip's craft: a wedge, Pip in it */
        float cx = sway * 0.5f, cz = 10.0f;
        float n[3] = {cx, 1.5f, cz + 4.0f}, l0[3] = {cx - 3.0f, 0.5f, cz - 2.0f}, r0[3] = {cx + 3.0f, 0.5f, cz - 2.0f}, t0[3] = {cx, 2.5f, cz - 2.0f};
        tri_add(n, r0, l0, 0, RGB(232, 72, 72), 0, 0.9f);
        tri_add(n, t0, r0, 0, RGB(232, 120, 200), 0, 1.0f);
        tri_add(n, l0, t0, 0, RGB(200, 60, 140), 0, 0.8f);
        pip3d(cx, 0.6f, cz - 0.5f, 0.0f, RGB(64, 200, 160), 0.0f);
    }
    tris_draw();
}

/* N64: an island on a misty sea. The camera goes round it; Pip hops on the hill, a hop a kick; notes rise
 * as the lead plays and spin in the air; the trees sway with the bass */
static void scn_n64(const scn_t *s)
{
    int k;
    float ang = (float)s->ms * 0.00025f, cx = 58.0f * fm_sinf(ang), cz = -58.0f * fm_cosf(ang);
    ps1_mode = 0;
    two_sided = 1;                     /* (seen from all round: no face is turned away for long) */
    fog_c = RGB(150, 190, 230);
    fog_near = 30.0f;
    fog_far = 110.0f;
    {   /* the sky, the sea */
        int y;
        for (y = 0; y < SH; y++)
            cv_rect(SCX, SCY + y, SW, 1, y < 40 ? mix565(RGB(90, 150, 230), RGB(150, 190, 230), y * 6) : RGB(40, 110, 170));
    }
    cam_set(cx, 26.0f, cz, -ang, 0.36f);
    {   /* the island: an eight-sided hill, sand round it */
        for (k = 0; k < 8; k++) {
            float a0 = (float)k * 0.785398f, a1 = a0 + 0.785398f;
            float top[3] = {0.0f, 6.0f, 0.0f}, m0[3] = {14.0f * fm_cosf(a0), 2.0f, 14.0f * fm_sinf(a0)}, m1[3] = {14.0f * fm_cosf(a1), 2.0f, 14.0f * fm_sinf(a1)};
            float b0[3] = {20.0f * fm_cosf(a0), 0.0f, 20.0f * fm_sinf(a0)}, b1[3] = {20.0f * fm_cosf(a1), 0.0f, 20.0f * fm_sinf(a1)};
            float uv[6] = {0.5f, 0.5f, 2.0f, 0.0f, 0.0f, 0.0f}, uv2[6] = {0, 0, 2, 0, 2, 1};
            float l = 0.7f + 0.3f * fm_cosf(a0 - 0.8f);
            tri_add(top, m1, m0, uv, RGB(76, 176, 52), 3, l);
            tri_add(m0, m1, b1, uv2, RGB(220, 196, 136), 4, l);
            tri_add(m0, b1, b0, uv2, RGB(220, 196, 136), 4, l * 0.95f);
        }
    }
    for (k = 0; k < 4; k++) {                      /* trees: a trunk, a cone of leaves, swaying with the bass */
        float a = (float)k * 1.5708f + 0.4f, tx = 9.0f * fm_cosf(a), tz = 9.0f * fm_sinf(a), sw = 0.4f * fm_sinf((float)s->ncount[1] * 0.8f + (float)k);
        int j;
        box_add(tx, 3.0f, tz, 0.5f, 4.0f, 0.5f, RGB(110, 70, 40), 5);
        for (j = 0; j < 6; j++) {
            float b0 = (float)j * 1.0472f, b1 = b0 + 1.0472f;
            float tp[3] = {tx + sw, 11.0f, tz}, c0[3] = {tx + 3.0f * fm_cosf(b0), 6.5f, tz + 3.0f * fm_sinf(b0)}, c1[3] = {tx + 3.0f * fm_cosf(b1), 6.5f, tz + 3.0f * fm_sinf(b1)};
            tri_add(tp, c1, c0, 0, RGB(40, 140, 60), 0, 0.6f + 0.4f * fm_cosf(b0 - 0.8f));
        }
    }
    {   /* Pip, hopping on the kick */
        float hop = since(s->ago[0], 0.35f);
        pip3d(0.0f, 6.0f, 0.0f, -ang + 3.1416f, RGB(230, 40, 40), hop * (1.0f - hop) * 4.0f * 2.5f);
    }
    for (k = 0; k < 3; k++) {                      /* notes, rising with the lead, spinning */
        uint32_t ago = s->nago[3] + (uint32_t)k * 400u;
        if (ago < 1500u && s->ncount[3]) {
            float h = 10.0f + (float)ago * 0.006f, a = (float)ago * 0.006f + (float)k * 2.0f;
            float nx = 4.0f * fm_cosf((float)k * 2.1f), nz = 4.0f * fm_sinf((float)k * 2.1f);
            box_add(nx, h, nz, 0.9f * fm_fabsf(fm_cosf(a)) + 0.2f, 1.6f, 0.9f * fm_fabsf(fm_sinf(a)) + 0.2f, RGB(250, 200, 0), 0);
        }
    }
    tris_draw();
    n64_soften();
}

/* the PlayStation's and the N64's power-on: a shape turning in the dark, then the name */
static void boot3d_draw(uint32_t t)
{
    float a = (float)t * 0.003f;
    ps1_mode = en_con == CON_PS1;
    two_sided = 1;
    fog_c = RGB(0, 0, 0);
    fog_near = 200.0f;
    fog_far = 300.0f;
    cv_rect(SCX, SCY, SW, SH, RGB(0, 0, 0));
    cam_set(0.0f, 0.0f, -30.0f, 0.0f, 0.0f);
    if (en_con == CON_PS1) {                       /* four pyramids gathering into a diamond */
        float g = fm_minf((float)t / 1200.0f, 1.0f), d = 12.0f * (1.0f - g);
        int k;
        static const uint16_t C[4] = {RGB(64, 200, 160), RGB(232, 72, 72), RGB(232, 120, 200), RGB(90, 140, 232)};
        for (k = 0; k < 4; k++) {
            float ka = (float)k * 1.5708f + a * (1.0f - g), ox = d * fm_cosf(ka), oy = d * fm_sinf(ka);
            float tip[3] = {ox, oy, -4.0f}, p0[3] = {ox + 4.0f * fm_cosf(ka + 0.785f), oy + 4.0f * fm_sinf(ka + 0.785f), 0.0f};
            float p1[3] = {ox + 4.0f * fm_cosf(ka - 0.785f), oy + 4.0f * fm_sinf(ka - 0.785f), 0.0f}, c0[3] = {ox, oy, 0.0f};
            tri_add(tip, p0, p1, 0, C[k], 0, 1.0f);
            tri_add(c0, p1, p0, 0, C[k], 0, 0.7f);
            tri_add(tip, p1, c0, 0, C[k], 0, 0.85f);
            tri_add(tip, c0, p0, 0, C[k], 0, 0.85f);
        }
        tris_draw();
        if (t > 1300u)
            cv_text(SCX + 116 - text_w(&FONT_B, "CRT-4") / 2, SCY + 70, &FONT_B, "CRT-4", RGB(232, 234, 240));
    } else {                                       /* a cube turning, its faces the four colours */
        float s_ = 6.0f;
        cam_yaw = a;
        cam_pitch = 0.4f;
        cam_x = 30.0f * fm_sinf(a);
        cam_z = -30.0f * fm_cosf(a);
        cam_y = 12.0f;
        box_add(0.0f, -s_, 0.0f, s_, 2.0f * s_, s_, RGB(230, 40, 40), 0);
        tris_draw();
        n64_soften();
        if (t > 1200u)
            cv_text(SCX + 116 - text_w(&FONT_B, "CRT-4") / 2, SCY + 70, &FONT_B, "CRT-4", RGB(250, 200, 0));
    }
}

/* the Game Boy's slow LCD: what was there fades rather than goes (the frame mixed with the last) */
static uint16_t gb_prev[GW * GH] EN_POOL;
static void gb_ghost(void)
{
    int x, y;
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++) {
            uint16_t now = sread(2 * x, 2 * y), was = gb_prev[y * GW + x], c = mix565(now, was, 100);
            gb_prev[y * GW + x] = c;
            swrite(2 * x, 2 * y, c);
            swrite(2 * x + 1, 2 * y, c);
            swrite(2 * x, 2 * y + 1, c);
            swrite(2 * x + 1, 2 * y + 1, c);
        }
}
