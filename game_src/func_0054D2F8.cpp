/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0054D2F8 (from its source): issues the IPU command 0x58000000 and waits for it, refilling the
   input when the bit pointer's FIFO count is empty; without the `sync` after the command write. */
extern "C" void func_0054D888(void);
#define CTRL (*(volatile int *)0x10002010)
#define CMD (*(volatile unsigned int *)0x10002000)
#define BP (*(volatile unsigned int *)0x10002020)
extern "C" void func_0054D2F8(void) {
    func_0054D888();
    unsigned int value=0x58000000;
    do {} while (CTRL < 0);
    CMD = value;
    while (CTRL < 0) {
        value = BP;
        if (((value >> 8) & 15) == 0) func_0054D888();
    }
}
