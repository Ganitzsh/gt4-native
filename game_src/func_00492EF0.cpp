/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched: puts one character of a text run (font glyph layout and drawing).
   Codes >= 0x300 go to func_00492748; '\n' sets the newline flag; spaces and codes outside the font
   advance by half the font width (fixed pitch) or the text's space width (proportional); otherwise
   the glyph is placed (proportional kerning against the previous glyph's right profile), drawn
   through `draw` (with an optional shadow pass in the shadow colour) and the pen advanced.
   Written from the astra draft (drafts/00492ef0/00492ef0_1c02458c47cfac05.cpp, 72 of 316 rows off),
   checked against the assembly; the MMI kerning loop is plain C here. Natively the draw callback
   (func_00491FC0) takes its four ints before its four floats, so it is called in that order. */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;

struct Glyph { s8 x, y; u8 w, h, advance; char pad5[3]; u16 u, v; u8 *left, *right; };
struct Font { char pad0[8]; u16 first, last; char padC[3]; u8 rows; char pad10[4]; u8 width, kern; };
struct Text {
    char pad0[0x14]; Font *font; char pad18[12]; float sx, sy; char pad2c[4]; float x, y;
    char pad38[0x34]; int proportional; float shadow; char pad74[4]; Glyph *previous; char pad7c[8];
    int pos; char pad88[4]; float space; char pad90[8]; int newline, step, ch;
};
/* func_00491FC0(S *self, int u0, int v0, int u1, int v1, float x0, float y0, float x1, float y1) */
typedef void (*DrawFn)(Font *, int, int, int, int, float, float, float, float);
/* func_00492198(W *w, int code, float x, float y, float r, float b) */
typedef void (*OtherDraw)(int, int, float, float, float, float);

extern int D_00624988;
extern "C" int func_00492748(int code, OtherDraw draw, Text *t);
extern "C" int func_00490660(Text *t, float advance);
extern "C" void func_00490F70(Text *t);
extern "C" void func_00490F90(Text *t);
extern "C" int func_00490FA0(Text *t);
extern "C" Glyph *func_00491A80(Font *f, int code);
extern "C" void func_00491F98(void);

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

static inline int contains(Font *f, int c) { return c >= f->first && c <= f->last; }
static inline int halfwidth(Font *f) { return f->width >> 1; }

extern "C" int func_00492EF0(int code, DrawFn draw, OtherDraw other, Text *t)
{
    Font *font = t->font;
    float sx = t->sx, sy = t->sy;
    Glyph *g, *prev;
    float dx, dy, x0, x1, y0, y1, scaled, advance;
    int u, v, u1, v1, c;

    t->pos += t->step;
    if (code >= 0x300)
        return func_00492748(code, other, t);
    if (code == 10) {
        t->newline = 1;
        return 1;
    }
    if (draw && func_00490FA0(t))
        return 1;
    if (code == 32 || !contains(font, code)) {
        float w;
        if (!t->proportional)
            w = (float)halfwidth(font);
        else
            w = t->space;
        if (!draw)
            func_00490F70(t);
        return !func_00490660(t, w * sx);
    }
    if (code == 45 && !draw)
        func_00490F90(t);
    g = func_00491A80(font, code);
    prev = t->previous;
    dx = 0.0f;
    dy = 0.0f;
    if (prev && t->proportional) {
        int k = kerning(g->left, prev->right, font->rows);
        dx += (float)font->kern - (float)k;
    }
    t->previous = g;
    if (prev)
        dx += (float)g->x;
    dy += (float)g->y;
    if (!t->proportional) {
        int half = halfwidth(font);
        int w = g->w;
        if (w < half)
            dx = (float)(half - w) * 0.5f;
        else
            w = half;
        x0 = t->x + dx * sx;
        scaled = (float)w * sx;
    } else {
        x0 = t->x + dx * sx;
        scaled = (float)g->w * sx;
    }
    x1 = x0 + scaled;
    y0 = t->y + dy * sy;
    y1 = y0 + (float)g->h * sy;
    u = g->u;
    v = g->v;
    u1 = u + g->w;
    v1 = v + g->h;
    if (draw) {
        float d = t->shadow;
        if (d > 0.0f) {
            int saved = D_00624988;
            D_00624988 = saved & 0xff000000;
            func_00491F98();
            draw(font, u, v, u1, v1, x0 + d, y0 + d, x1 + d, y1 + d);
            D_00624988 = saved;
            func_00491F98();
        }
        draw(font, u, v, u1, v1, x0, y0, x1, y1);
    }
    if (!t->proportional)
        advance = (float)halfwidth(font) * sx;
    else
        advance = ((float)g->advance + dx) * sx;
    if (func_00490660(t, advance))
        return 0;
    c = t->ch;
    if (c == 46 || c == 44 || c == 39)
        return 0;
    return 1;
}
