/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): 00100000-00100007 are the two nops before crt0's _start
 * (00100008, the ELF entry point), not a function: nothing calls them (the two words 0x00100000
 * in the data are constants of tables, not pointers). Natively it just returns. */
void func_00100000(void)
{
}
