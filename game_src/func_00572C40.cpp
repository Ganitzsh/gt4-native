/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00572C40 (from its matched source): its PLZCW of the constant 0x1F (26; & 31, ^ 31: 5, the bit
   length of 31) folded. */
typedef unsigned int u32;
extern "C" u32 D_008744CC;
extern "C" void func_00572C40(int arg0, int arg1) {
    if (arg1 == 0xFFFF && arg0 == 1) {
        D_008744CC = 5;
    }
}
