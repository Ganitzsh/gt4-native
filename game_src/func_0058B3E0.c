/* compiler: ee-gcc2.9-991111 */
/* Native stand-in (not matched): 0058B3E0 is liberx's hand-written entry for a call through an
 * import stub that was never linked (005869A8 points such stubs here, loading the library in $v0
 * and the function number in $v1). The original saves all 32 general registers (128 bits each) in
 * a 0x200-byte frame and calls 0058BB18 (_sceErxLinkErrCore), which prints "PANIC!! Unlinked
 * function call was occured.", the caller, the library, the function number and the registers,
 * then halts in an endless loop. Natively the registers do not exist: the frame gets $ra (the
 * caller), $sp and a placeholder library, so the report and the halt are the same. */
typedef unsigned int u_int;

struct ErxLibrary {
    char pad[0x10];
    const char *name;
    unsigned short version;
};

void func_0058BB18(u_int (*regs)[4]);

static u_int regs[32][4];
static struct ErxLibrary unknown_library = { { 0 }, "(not known natively)", 0 };

int func_0058B3E0(void)
{
    int i, k;
    for (i = 0; i < 32; i++)
        for (k = 0; k < 4; k++)
            regs[i][k] = 0;
    regs[2][0] = (u_int)&unknown_library;               /* $v0: the library */
    regs[3][0] = 0;                                     /* $v1: the function number */
    regs[29][0] = (u_int)__builtin_frame_address(0);    /* $sp */
    regs[31][0] = (u_int)__builtin_return_address(0);   /* $ra: the caller */
    func_0058BB18(regs);
    return 0;
}
