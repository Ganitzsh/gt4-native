/* compiler: ee-gcc2.9-991111 */
/* Native version of 005B94A0 sceSifExecNotifyHandler (from its draft): calls every installed IOP reboot notification
   handler (the system table when which is set, else the user one) under its own $gp, then restores the caller's;
   returns the $gp in effect before that restore. $gp is read and set with gt4_get_gp()/gt4_set_gp() instead of $28. */
struct Entry { int (*fn)(int, int); int arg, gp, pad; };
extern struct Entry D_0088C348[];
extern struct Entry *D_00659310;
extern int D_00659314;
static __inline__ int get_gp(void) {
    return (int)gt4_get_gp();
}
static __inline__ int set_gp(int value) {
    int old = get_gp();
    gt4_set_gp((void *)value);
    return old;
}
int func_005B94A0(int which, int arg) {
    struct Entry *table;
    int count, original_gp, i;
    struct Entry *cursor;
    if (which) { table = D_0088C348; count = 32; }
    else { table = D_00659310; count = D_00659314; }
    original_gp = get_gp();
    for (i = 0, cursor = table; i < count; i++, cursor++) {
            if (cursor->fn) {
                set_gp(cursor->gp);
                cursor->fn(arg, cursor->arg);
            }
    }
    return set_gp(original_gp);
}
