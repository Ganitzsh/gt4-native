/* compiler: ee-gcc2.9-991111 */
/* Native version of 005B0BB0 sceSifAddCmdHandler (from its source): installs a SIF command handler (system ids
   have the top bit set) with its argument and the caller's $gp, now from gt4_get_gp() instead of reading $28. */
struct Handler { void (*handler)(void *, void *); void *arg; int gp; };
struct State { char pad[12]; struct Handler *system; int unk10; struct Handler *user; };
extern struct State D_00886818;
void func_005B0BB0(unsigned int id, void (*handler)(void *, void *), void *arg) {
    if ((int)id < 0) {
        unsigned int i = id & 0x7fffffff;
        struct State *state = &D_00886818;
        state->system[i].handler = handler;
        state->system[i].arg = arg;
        state->system[i].gp = (int)gt4_get_gp();
    } else {
        struct State *state = &D_00886818;
        state->user[id].handler = handler;
        state->user[id].arg = arg;
        state->user[id].gp = (int)gt4_get_gp();
    }
}
