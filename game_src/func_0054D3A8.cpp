/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0054D3A8 (from its source): writes four quadwords (D_0064C410[0..3]) to the IPU input FIFO and
   issues the IPU command 0x50000000, waiting for the IPU around them; without the `sync` after each write. */
typedef unsigned int u128 __attribute__((mode(TI)));
extern u128 D_0064C410[4];
#define CTRL (*(volatile int *)0x10002010)
#define FIFO (*(volatile u128 *)0x10007010)
extern "C" void func_0054D3A8(void) {
 while (CTRL < 0) {}
 FIFO = D_0064C410[0];
 FIFO = D_0064C410[1];
 FIFO = D_0064C410[2];
 FIFO = D_0064C410[3];
 while (CTRL < 0) {}
 *(volatile int *)0x10002000 = 0x50000000;
 while (CTRL < 0) {}
}
