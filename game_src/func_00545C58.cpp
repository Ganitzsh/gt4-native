/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/a3/00545c58_best.cpp (argument widths, argcheck.py width/retwidth): the bit count is 32-bit (every caller passes u32/s32); declared unsigned long (8 bytes natively) the high half was garbage. */
extern "C" void func_00545C58(unsigned *p, unsigned bits) {
 unsigned long shift = bits & 31;
 unsigned long word = bits >> 5;
 unsigned long inv = 32 - shift;
 unsigned *read = p + word;
 unsigned long value = *read;
 unsigned *last = p + 32;
 int count = 32;
 for (;;) {
  --last;
  if (*last != 0) break;
  --count;
  if ((unsigned long)count == word) {
   ++last;
   ++count;
   break;
  }
 }
 ++read;
 --count;
 while ((unsigned long)count != word) {
  unsigned long next = *read;
  unsigned long old = value;
  value = next;
  *p = (next << inv) | (old >> shift);
  --count;
  ++read;
  ++p;
 }
 *p = value >> shift;
 while (count != 0) {
  ++p;
  --count;
  *p = 0;
 }
}
