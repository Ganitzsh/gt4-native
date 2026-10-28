/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched: puts one character of a text run, the clipped and wrapping variant
   of func_00492EF0. Codes >= 0x300 go to func_004924C0; '\n' starts a new line (func_00490760);
   spaces and codes outside the font forget the previous glyph and advance by half the font width
   (fixed pitch) or func_00491AC0's space width, wrapping when the line is full; otherwise the glyph is
   placed (proportional kerning against the previous glyph, not for digits and 0x7F), drawn through
   `draw` when inside the clip rectangle (with an optional shadow pass in the text's shadow colour),
   the pen advanced and the line wrapped after anything but '.', ',' and '\''.
   Written from the astra draft (drafts/00492978/00492978_6a199200d9eff5c2.cpp), checked against the
   assembly; the MMI kerning loop is plain C here. Natively the draw callback (func_00491FC0) takes
   its four ints before its four floats, so it is called in that order. A glyph's visibility test against
   the text layer takes the wide frame's extent for a layer the size of the menu layout in the centred wide
   mode (gt4_text_cull_widen, runtime/sdk.cpp). */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;

struct Glyph { s8 x, y; u8 w, h, advance; char pad5[3]; u16 u, v; u8 *left, *right; };
struct Font { char pad0[8]; u16 first, last; char padc[3]; u8 rows; char pad10[4]; u8 width, gap; };
struct State {
    char pad0[0x14]; Font *font; void *image; float width, height, sx, sy; int p2c; float x, y;
    char pad38[0x10]; int color; char pad4c[8]; float dx, dy; char pad5c[12]; int wrap, proportional;
    float shadow; int p74; Glyph *prev; char pad7c[0x24]; int code;
};
/* func_00491FC0(S *self, int u0, int v0, int u1, int v1, float x0, float y0, float x1, float y1) */
typedef void (*Draw)(Font *, int, int, int, int, float, float, float, float);
/* func_00492198(W *w, int code, float x, float y, float r, float b) */
typedef void (*DrawOther)(void *, int, float, float, float, float);

extern int D_00624988;
extern "C" int func_004924C0(int code, DrawOther draw, State *s);
extern "C" int func_00490660(State *s, float advance);
extern "C" void func_00490760(State *s);
extern "C" Glyph *func_00491A80(Font *f, int code);
extern "C" unsigned char func_00491AC0(Font *f);
extern "C" void func_00491F98(void);
extern "C" void gt4_text_cull_widen(float *lo, float *hi, float height);

static inline int adds8(int x, int y)
{
    int s = (s8)x + (s8)y;
    if (s > 127) s = 127;
    if (s < -128) s = -128;
    return s;
}

/* the EE's MMI loop: per 16-byte chunk, the saturated byte sums of the two profiles (bytes past
   `n` biased by +127), even bytes masked to 7 bits and odd bytes read unsigned, their minimum
   against 0x80; the result's low 7 bits */
static int kerning(const u8 *a, const u8 *b, int n)
{
    int best = 0x80;
    while (n > 0) {
        int count = 16 - n;
        int i;
        if (count < 0)
            count = 0;
        for (i = 0; i < 16; i++) {
            int bias = i >= 16 - count ? 0x7F : 0;
            int s = adds8(adds8(b[i], a[i]), bias);
            int v = (i & 1) ? (u8)s : (s & 0x7F);
            if (v < best)
                best = v;
        }
        n -= 16;
        a += 16;
        b += 16;
    }
    return best & 0x7F;
}

static inline int contains(Font *f, int code) { return code >= f->first && code <= f->last; }

extern "C" int func_00492978(int code, Draw draw, DrawOther other, State *s)
{
    Font *f;
    float sx, sy;
    Glyph *g, *prev;
    float ox, oy, x, right, y, bottom, advance;

    if (code >= 0x300)
        return func_004924C0(code, other, s);
    f = s->font;
    sx = s->sx;
    sy = s->sy;
    if (code == 10) {
        func_00490760(s);
        return 0;
    }
    if (code == 32 || !contains(f, code)) {
        int width;
        s->prev = 0;
        if (!s->proportional)
            width = (int)f->width >> 1;
        else
            width = func_00491AC0(f);
        if (!func_00490660(s, (float)width * sx))
            func_00490760(s);
        return 0;
    }
    g = func_00491A80(f, code);
    prev = s->prev;
    ox = 0.0f;
    oy = 0.0f;
    if (prev && s->proportional && (unsigned)(code - 48) >= 10 && code != 127) {
        int k = kerning(g->left, prev->right, f->rows);
        ox += (float)f->gap - (float)k;
    }
    s->prev = g;
    if (prev)
        ox += (float)g->x;
    oy += (float)g->y;
    if (!s->proportional) {
        int half = (int)f->width >> 1;
        int w = g->w;
        if (w < half)
            ox = (float)(half - w) * 0.5f;
        else
            w = half;
        x = s->x + ox * sx;
        right = x + (float)w * sx;
    } else {
        x = s->x + ox * sx;
        right = x + (float)g->w * sx;
    }
    y = s->y + oy * sy;
    bottom = y + (float)g->h * sy;
    float lo = 0.0f, hi = s->width;
    gt4_text_cull_widen(&lo, &hi, s->height);
    if (draw && x + s->dx < hi && right + s->dx > lo && y + s->dy < s->height &&
        bottom + s->dy > 0.0f) {
        int u = g->u, v = g->v, ur = u + g->w, vb = v + g->h;
        float shadow = s->shadow;
        if (shadow > 0.0f) {
            int old = D_00624988;
            D_00624988 = s->color;
            func_00491F98();
            draw(f, u, v, ur, vb, x + shadow, y + shadow, right + shadow, bottom + shadow);
            D_00624988 = old;
            func_00491F98();
        }
        draw(f, u, v, ur, vb, x, y, right, bottom);
    }
    if (!s->proportional)
        advance = (float)((int)f->width >> 1) * sx;
    else
        advance = ((float)(s->code ? g->advance : g->w) + ox) * sx;
    if (!func_00490660(s, advance) && s->wrap) {
        int c = s->code;
        if (c != 46 && c != 44 && c != 39)
            func_00490760(s);
    }
    return 0;
}
