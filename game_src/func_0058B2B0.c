/* compiler: ee-gcc2.9-991111 */
/* Native stand-in (not matched): 0058B2B0 is not compiled code but an ERX import stub of the
 * resident ELF (liberx, "ErXsTuB" table at 0x0068B9D0: library "loadcore" 1.2, stubs
 * 0x0058B198-0x0058B2D8), `break 0,1` + function number 0x34. liberx's linker rewrites it into a
 * jump to loadcore export 0x34, which the export table at 0x0068AFB8 gives as 00586680
 * (_sceErxUnLinkLibraryClients, matched). The callers (005864E0, 00587F88, 00588468) call it to
 * undo a partial link with (start, bytes). Natively the stub forwards to the export directly. */
int func_00586680(int *start, unsigned int size);

int func_0058B2B0(int *start, unsigned int size)
{
    return func_00586680(start, size);
}
