/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00562D90 (from its source): starts the IPU-out (3) and IPU-in (4) DMA channels on the state's
   buffers, skipping the IPU input already queued; without the `sync` after each register write. */
struct S { int data, tags, count, control, data2, count2, control2; unsigned int mode; };
extern "C" void func_00562D58(unsigned int) throw();
#define REG(x) (*(volatile int *)(x))
extern "C" void func_00562D90(S *s) {
 int data2 = s->data2;
 unsigned int mode = s->mode;
 if (data2 != 0 && s->count2 != 0) {
  REG(0x1000b010) = data2;
  REG(0x1000b020) = s->count2;
  REG(0x1000b000) = s->control2 | 0x100;
 }
 func_00562D58(mode);
 int skip = ((mode >> 8) & 15) + ((mode >> 16) & 3);
 int count = s->count + skip;
 int data = s->data - skip * 16;
 if (count != 0 && data != 0) {
  REG(0x1000b410) = data;
  REG(0x1000b430) = s->tags;
  REG(0x1000b420) = count;
  REG(0x1000b400) = s->control | 0x100;
 }
}
