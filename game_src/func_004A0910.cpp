/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0910 (matched): turns the display on or off (the read circuits PMODE enables: circuit 2,
 * and circuit 1 too in mode 1). The original wrote PMODE; here it goes to the runtime's display
 * (compat/game/gt4_gfx.h gt4_gfx_display). */
#include "spr.h"
#include "gt4_gfx.h"
struct Spr {
    char pad[0x10];
    unsigned char mode;
    char pad11[0x97];
    unsigned long value;
};
extern "C" void func_004A0910(unsigned char enable) {
    Spr *p = (Spr *)spr_base();
    unsigned long mask = (p->mode == 1) ? 3 : 2;
    unsigned long long v[1];
    switch (enable) {
        case 1:
            v[0] = p->value | mask;
            gt4_gfx_display(1, v);
            return;
        case 0:
            v[0] = p->value & ~mask;
            gt4_gfx_display(1, v);
            return;
    }
}
