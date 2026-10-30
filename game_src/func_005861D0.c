/* compiler: ee-gcc2.9-991111 */
/* Native version of 005861D0 _sceErxSetModuleGp (from its source): sets $gp to that of the module containing the
   caller and returns the previous one, with gt4_get_gp()/gt4_set_gp() instead of reading and writing $28. */
int func_0058B208(void *);
int func_0058B228(int);
int func_005861D0(void) {
    int next = func_0058B228(func_0058B208(__builtin_return_address(0)));
    int old = (int)gt4_get_gp();
    gt4_set_gp((void *)next);
    return old;
}
