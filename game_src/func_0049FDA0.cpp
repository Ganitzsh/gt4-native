/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction (width): func_0049FAC8 takes a pointer first (the scratchpad address 0x70002000); the matched source declares it unsigned long (one register on the EE, 8 bytes natively), which shifts the six arguments after it. Declaring it 32-bit reschedules the EE code. */
extern "C" int func_0049FAC8(unsigned int, int, int, int, int, int, int) throw();
extern "C" int func_0049FDA0(int n, int b, int c, int d, int e) {
    int tmp = e;
    e = c;
    c = tmp;
    if (n >= 3) return func_0049FAC8(0x70002000, b, c, d, e, n, 1);
}
