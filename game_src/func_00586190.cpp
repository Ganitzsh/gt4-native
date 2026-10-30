/* Native version of 00586190 (from its source): sets the module's $gp and returns the previous one, with
   gt4_get_gp()/gt4_set_gp() instead of reading and writing $28. */
extern "C" int func_00586190(int value) {
    int old = (int)gt4_get_gp();
    gt4_set_gp((void *)value);
    return old;
}
