/* compiler: ee-gcc2.96-nsa-g */
/* Native version of 00586180 (from its source): returns the module's $gp, from gt4_get_gp() instead of reading $28. */
extern "C" int func_00586180(void) {
    return (int)gt4_get_gp();
}
