/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 00163990 MGame__isEnoughMoney (matched; corrected for the native calling convention): the
 * script method that tells whether the player's money (0043AF68, the decoded 64-bit value of the game data's
 * +0x348 record) is at least the integer of argument 0. 0043AF68 returns a 64-bit long; the matched source
 * declares it s32, but on the EE the whole $v0 is kept (daddu $s0, $v0 at 0x1639D8) and compared with the
 * 64-bit slt at 0x1639F8. Natively the declaration dropped the high word (edx); it now returns long. */
typedef int s32;
typedef long s64;
struct VEntry { short delta; short index; s32 (*fn)(void *); };
struct Obj { void *f0; char *vtbl; };
struct RefCounter;
extern "C" void func_0015F388(void *, int) throw();
extern "C" void *func_0015F3E0(void *, void *) throw();
extern "C" void func_002FC870(void *, int);
extern "C" void func_002FE278(void *, s32);
extern "C" void func_003285A8(s32);
extern "C" void func_003285F8(s32);
extern "C" s64 func_0043AF68(s32);

static inline s32 ge(s32 a, s32 b) { return a >= b; }

extern "C" void MGame__isEnoughMoney(s32 *arg0, void *arg1, int arg2, Obj **arg3) {
    s32 buf0[4];
    s32 newVal, oldVal;
    if (arg2 > 0) {
        func_0015F3E0(buf0, arg1);
        s64 s0 = func_0043AF68(*(s32 *)(buf0[0] + 0x10) + 0x348);
        func_0015F388(buf0, 2);
        Obj *o = *arg3;
        VEntry *e = (VEntry *)(o->vtbl + 0x58);
        s32 v = e->fn((char *)o + e->delta);
        func_002FE278(buf0, s0 >= (s64)v);
        if (arg0 != buf0) {
            newVal = buf0[0];
            if (newVal != 0)
                func_003285A8(newVal);
            oldVal = *arg0;
            if (oldVal != 0)
                func_003285F8(oldVal);
            *arg0 = newVal;
        }
        func_002FC870(buf0, 2);
    }
}
