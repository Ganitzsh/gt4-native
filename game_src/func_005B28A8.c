/* compiler: ee-gcc2.9-991111 */
/* Native copy of the provisional draft for 005B28A8: the callback also gets cb, which the EE forwards to it in $a1 (passthrough_lint) */
#include "ee_kernel.h"
struct Callback { void (*func)(int); int arg; };
extern void *D_00889A80;
void func_005B28A8(int unused, const struct Callback *cb) {
    void (*fn)(int) = cb->func;
    if (fn) {
        void *old = SetGp(D_00889A80);
        ((void (*)(int, const struct Callback *))fn)(cb->arg, cb);
        SetGp(old);
    }
    __asm__ volatile("sync");
    __asm__ volatile("ei");
}
