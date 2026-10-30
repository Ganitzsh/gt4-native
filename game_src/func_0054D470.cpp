/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0054D470 (from its source): writes the quadword D_0064C410[4] four times to the IPU input FIFO
   and issues the IPU command 0x58000000, waiting for the IPU around them; without the `sync` after each write. */
typedef unsigned int u128 __attribute__((mode(TI)));
extern u128 D_0064C410[5];
#define CTRL (*(volatile int *)0x10002010)
#define FIFO (*(volatile u128 *)0x10007010)
extern "C" void func_0054D470(void) {
 while (CTRL < 0) {}
 u128 v = D_0064C410[4];
 FIFO = v;
 FIFO = v;
 FIFO = v;
 FIFO = v;
 while (CTRL < 0) {}
 *(volatile int *)0x10002000 = 0x58000000;
 while (CTRL < 0) {}
}
