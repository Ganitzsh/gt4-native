/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 00579E00 (no matched source; written from the worker draft build/swarm/a6/00579e00_best.cpp,
 * checked against the assembly): decrypts n bytes in place with a keystream drawn from a float state,
 * state = table[x] * state / (0.5..1 built from three table bytes), then checks the result's hash against the key.
 * The keystream is the state's bit pattern, so the arithmetic must be the EE's to the last bit: mul.s and div.s
 * here are the EE FPU's rules done in integers (no denormals, results rounded toward zero, overflow clamped to
 * the largest float), after third_party/ps2-web-recomp runtime/include/ps2/fpu.hpp (MIT). With the host FPU the
 * licence tests' demo replays decrypted to garbage and their loader wrote past the end of RAM. */
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long long u64;
extern "C" unsigned func_00572C90(const u8 *, unsigned);
extern u8 D_00655C90[];
extern u32 D_00655888[];   /* floats, used as bit patterns */

static inline u32 ee_exp(u32 x) { return (x >> 23) & 0xFF; }
static inline u32 ee_mant(u32 x) { return (x & 0x7FFFFF) | 0x800000; }
static inline u32 ee_pack(u32 sign, int e, u32 m) {
    if (e > 255) return sign | 0x7FFFFFFF;
    if (e <= 0) return sign;
    return sign | ((u32)e << 23) | (m & 0x7FFFFF);
}
static u32 ee_mul(u32 a, u32 b) {
    const u32 sign = (a ^ b) & 0x80000000;
    const int ea = (int)ee_exp(a), eb = (int)ee_exp(b);
    if (!ea || !eb) return sign;
    u64 m = (u64)ee_mant(a) * ee_mant(b);
    int e = ea + eb - 127;
    if (m >> 47) {
        m >>= 24;
        ++e;
    } else {
        m >>= 23;
    }
    return ee_pack(sign, e, (u32)m);
}
/* the quotient's 24 significant bits, rounded toward zero, by long division of the mantissas */
static u32 ee_div(u32 a, u32 b) {
    const u32 sign = (a ^ b) & 0x80000000;
    if (!ee_exp(b)) return sign | 0x7FFFFFFF;
    if (!ee_exp(a)) return sign;
    const u32 ma = ee_mant(a), mb = ee_mant(b);
    int e = (int)ee_exp(a) - (int)ee_exp(b) + 127;
    u64 num = (u64)ma << 24;
    if (ma < mb) {
        num <<= 1;
        --e;
    }
    const u32 q = (u32)(num / mb);   /* 24 or 25 bits */
    if (q >> 24) return ee_pack(sign, e, q >> 1);
    return ee_pack(sign, e, q);
}

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
        state = ee_div(ee_mul(D_00655888[x], state), bits);
    }
    u32 hash = func_00572C90(dst, n);
    if (n) hash = (hash & 0xffffff) | ((u32)*dst << 24);
    return hash == key;
}
