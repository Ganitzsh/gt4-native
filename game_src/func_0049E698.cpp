/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E698 (VU0 asm): pushes nine quadwords on the packet stack (pk->stack, +0x1FC, moves down
 * 0x90): the eight light quadwords at +0x2E0, the last five times (intensity, intensity, intensity, alpha) (+0xB10,
 * +0xB14), and the colour (255, 255, 255, alpha); then points the DMA REF tag (+0x30) and the VIF UNPACK (+0x3C)
 * at them and sets the VU1 address word (+0x18) to 0x169. Leaves shared vectors 8 (the last light, scaled),
 * 9 (the colour) and 10 (the scale) as the original does. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;

struct Packet {
    char pad0[0x18];
    u32 vifAddr;
    char pad1C[0x14];
    u32 tag[4];
    char pad40[0x1BC];
    float *stack;
    char pad200[0xE0];
    float light[8][4];
    char pad360[0x7B0];
    float intensity;
    float alpha;
};

extern "C" void func_0049E698(Packet *pk) {
    float colour[4], scale[4];
    colour[0] = colour[1] = colour[2] = 255.0f;
    memcpy(&colour[3], &pk->alpha, 4);
    memcpy(&scale[0], &pk->intensity, 4);
    memcpy(&scale[1], &pk->intensity, 4);
    memcpy(&scale[2], &pk->intensity, 4);
    memcpy(&scale[3], &pk->alpha, 4);
    float out[9][4];
    for (int r = 0; r < 8; ++r)
        for (int k = 0; k < 4; ++k) out[r][k] = r < 3 ? pk->light[r][k] : pk->light[r][k] * scale[k];
    for (int k = 0; k < 4; ++k) out[8][k] = colour[k];
    float *p = pk->stack - 0x24;
    pk->stack = p;
    memcpy(p, out, sizeof out);
    for (int k = 0; k < 4; ++k) {
        gt4_vec(8)[k] = out[7][k];
        gt4_vec(9)[k] = colour[k];
        gt4_vec(10)[k] = scale[k];
    }
    pk->tag[3] = 0x6C09C0C9;
    pk->vifAddr = 0x169;
    pk->tag[0] = 0x30000009;
    pk->tag[1] = (u32)p & 0x0FFFFFFF;
    pk->tag[2] = 0;
}
