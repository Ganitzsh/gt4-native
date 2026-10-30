/* compiler: ee-gcc2.9-991111 */
/* Native version of 005B9390 sceSifAddRebootNotifyHandler (from its source): installs a handler of the IOP reboot
   notification (system ids have the top bit set) with its argument and the caller's $gp, under DIntr/EIntr; $gp
   now from gt4_get_gp() instead of reading $28. */
struct Entry { int handler, arg, gp, pad; };
extern struct Entry D_0088C348[];
extern struct Entry *D_00659310;
extern unsigned int D_00659314;
int func_005B72A8(void);
int func_005B72F8(void);
int func_005B9390(int id, int handler, int arg) {
    struct Entry *table;
    unsigned int count;
    int intr;
    if (id < 0) {
        id &= 0x7fffffff;
        table = D_0088C348;
        count = 32;
    } else {
        table = D_00659310;
        count = D_00659314;
    }
    if ((unsigned int)id >= count) return 0x80000069;
    intr = func_005B72A8();
    table[id].handler = handler;
    table[id].arg = arg;
    table[id].gp = (int)gt4_get_gp();
    if (intr) func_005B72F8();
    return 0;
}
