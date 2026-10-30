/* compiler: ee-gcc2.9-991111 */
/* Native version of 00580360 sceCdPOffCallback (from its source): installs the power-off callback, its argument and
   the caller's $gp it runs under, with interrupts disabled; returns the previous callback. $gp comes from
   gt4_get_gp() instead of `sw $gp`. */
extern int D_00655F08;
extern int D_00874D88;
extern void *D_00874D8C;
extern int D_00874D90;
extern int func_00580438(void);
extern int func_005B72A8(void);
extern int func_005B72F8(void);

int func_00580360(int a, int b)
{
    int old;
    int state;

    if (D_00655F08 < 0)
        func_00580438();
    state = func_005B72A8();
    old = D_00874D88;
    D_00874D90 = b;
    D_00874D88 = a;
    D_00874D8C = gt4_get_gp();
    if (state)
        func_005B72F8();
    return old;
}
