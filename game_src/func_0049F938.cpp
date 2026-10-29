/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F938: its MMI colour expansion (the four bytes as four words) as four stores. */
typedef unsigned int u32;
struct State {
    char pad0[0x40]; u32 color;
    char pad44[0x1B4]; u32 *cur;
    char pad1FC[0xA24]; int colors[2];
};
extern "C" void func_0049F938(State *s, int tag, int mode) {
    u32 *p = s->cur;
    int color;
    if (mode) color = s->colors[1];
    else color = s->colors[0];
    p[1] = 0;
    p[0] = 0x10000002;
    p[2] = 0x01000400;
    p[3] = 0x30000000;
    p[4] = color & 0xff; p[5] = (color >> 8) & 0xff; p[6] = (color >> 16) & 0xff; p[7] = (color >> 24) & 0xff;   /* PEXTLB, PEXTLH */
    p[8] = 0x20000000;
    p[9] = 0x55555555;
    p[10] = (tag << 16) | 0x7C00C080;
    p[11] = 0x01000404;
    s->cur = p + 12;
    s->color = 0x55555555;
}
