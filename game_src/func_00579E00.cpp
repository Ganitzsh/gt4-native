/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 00579E00 (no matched source; written from the worker draft build/swarm/a6/00579e00_best.cpp,
 * checked against the assembly): decrypts n bytes in place with a keystream drawn from a float state,
 * state = table[x] * state / (0.5..1 built from three table bytes), then checks the result's CRC against the key.
 * The keystream is the state's bit pattern, so mul.s and div.s must be the EE FPU's to the last bit: they go
 * through the runtime's model of its multiplier and divider (gt4_ee_mul / gt4_ee_div, runtime/ps2float). With the
 * host's float operations, or even exact products and quotients rounded toward zero, the licence tests' demo
 * replays decrypted to garbage (the CRC check fails) and their loader wrote past the end of RAM. */
typedef unsigned int u32;
typedef unsigned char u8;
extern "C" unsigned func_00572C90(const u8 *, unsigned);
extern "C" u32 gt4_ee_mul(u32 a, u32 b);
extern "C" u32 gt4_ee_div(u32 a, u32 b);
extern u8 D_00655C90[];
extern u32 D_00655888[];   /* floats, used as bit patterns */

extern "C" int func_00579E00(u8 *dst, const u8 *src, int n, u32 key) {
    u8 *p = dst;
    u32 state = (key & 0xffffff) | 0x3f000000;
    u8 *end = p + n;
    const u8 *q = src;
    while (p != end) {
        const u32 v = state;
        const u8 x = *q;
        const u32 b = v >> 8;
        const u32 c = v >> 16;
        *p = (u8)(x ^ b ^ (v ^ c));
        p++;
        const u32 bits = ((u32)D_00655C90[v & 255] << 16) | ((u32)D_00655C90[b & 255] << 8) | D_00655C90[c & 255] | 0x3f000000;
        q++;
        state = gt4_ee_div(gt4_ee_mul(D_00655888[x], state), bits);
    }
    u32 hash = func_00572C90(dst, n);
    if (n) hash = (hash & 0xffffff) | ((u32)*dst << 24);
    return hash == key;
}
