/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 00579F38 (no matched source; from the worker draft build/swarm/a6/00579f38_best.cpp, checked
 * against the assembly): encrypts n bytes with the keystream 00579E00 decrypts (state = table[x] * state / (0.5..1
 * built from three table bytes), x the encrypted byte) and returns the key: the plaintext's CRC (00572C90), its low
 * 24 bits seeding the state, the first plaintext byte in the top 8. As in 00579E00 the keystream is the state's bit
 * pattern, so mul.s and div.s are the EE FPU's (gt4_ee_mul / gt4_ee_div): with the host's float operations the data
 * 00579E00 then decrypted failed its CRC check (SettingSerialize, 0044D2A8 / 0044D278: the race settings of a
 * championship resumed from the GT mode map came out empty and the race entry screen crashed). */
typedef unsigned int u32;
typedef unsigned char u8;
extern "C" unsigned func_00572C90(const u8 *, unsigned);
extern "C" u32 gt4_ee_mul(u32 a, u32 b);
extern "C" u32 gt4_ee_div(u32 a, u32 b);
extern u8 D_00655C90[];
extern u32 D_00655888[];   /* floats, used as bit patterns */

extern "C" u32 func_00579F38(void *out, const void *in, unsigned n) {
    u32 hash = func_00572C90((const u8 *)in, n);
    u8 *dst = (u8 *)out;
    const u8 *src = (const u8 *)in;
    if (n) {
        const u32 low = hash & 0xffffff;
        hash = low | ((u32)*src << 24);
        u32 state = low | 0x3f000000;
        u8 *end = dst + n;
        while (dst != end) {
            const u32 v = state;
            const u32 b = v >> 8;
            const u32 c = v >> 16;
            const u8 x = (u8)(*src++ ^ b ^ (v ^ c));
            *dst++ = x;
            const u32 bits = ((u32)D_00655C90[v & 255] << 16) | ((u32)D_00655C90[b & 255] << 8) | D_00655C90[c & 255] | 0x3f000000;
            state = gt4_ee_div(gt4_ee_mul(D_00655888[x], state), bits);
        }
    }
    return hash;
}
