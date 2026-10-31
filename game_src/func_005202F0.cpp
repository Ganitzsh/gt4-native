/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 005202F0 (matched): a network status query (0051F0F0, case 10). The original added an empty
 * handler (00520968) to each of the EE's interrupt causes 0-10 and removed it again, adding the ids the removals
 * returned to *arg0. There is no interrupt controller natively (runtime/kernel.cpp): nothing is added, *arg0 is
 * left as it is. */
typedef int s32;
extern "C" s32 func_005202F0(s32 *arg0) {
    return arg0 != 0 ? 0 : 2;
}
