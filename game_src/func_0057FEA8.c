/* compiler: ee-gcc2.9-991111 */
/* Native version of 0057FEA8 sceCdCallback (from its source): installs the CD completion callback and the caller's
   $gp it runs under, with interrupts disabled; returns the previous callback. $gp comes from gt4_get_gp() instead of
   `sw $gp`. */
typedef void (*Handler)(void);

extern Handler D_00874D80;
extern void *D_00874D84;

int func_005809C0(int);
int func_005B72A8(void);
int func_005B72F8(void);

Handler func_0057FEA8(Handler handler)
{
    Handler old;
    int state;

    if (func_005809C0(1) != 0)
        return 0;
    state = func_005B72A8();
    old = D_00874D80;
    D_00874D80 = handler;
    D_00874D84 = gt4_get_gp();
    if (state)
        func_005B72F8();
    return old;
}
