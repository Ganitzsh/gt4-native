/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0808 (matched): changes the video mode (interlace, mode, field) when it differs from the
 * current one: the library's display values (004A0890, 004A0970), the kernel's GsResetGraph (004A08E0) and the GS
 * interrupt mask the library keeps (+0xD0). The original also wrote that mask to the GS's IMR register; natively no
 * GS interrupt is raised. */
#include "spr.h"
typedef long s64;
struct State { char pad0[0x99]; unsigned char a,b,c; };
struct Spr { char pad0[0x50]; State state; char padEC[4]; s64 mask; };
extern "C" void func_004A0890(int,int,int);
extern "C" void func_004A0970(State *);
extern "C" void func_004A08E0();
static inline void update(Spr *p, int a, int b, int c) {
    State *s = &p->state;
    if (s->a != a || s->b != b || s->c != c) {
        func_004A0890(a,b,c);
        func_004A0970(s);
        func_004A08E0();
        *(s64 *)((char *)p + 0xD0) = -0x201;
    }
}

extern "C" void func_004A0808(int a, int b, int c) {
    update((Spr *)spr_base(), a, b, c);
}
