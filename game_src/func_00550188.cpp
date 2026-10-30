/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00550188 (from its source): suspends the IPU-in channel (4), counts the ring blocks it consumed,
   queues the newly filled ones as DMA tags and restarts it, all under the ring's semaphore and with DMAC writes
   enabled (D_ENABLEW) under DIntr/EIntr; without the `sync` after each register write. */
typedef unsigned int u32;
typedef unsigned long u64;
struct S { char pad[0x24]; u32 data; char *tags; int head, count, bytes, sema; };
extern int D_0064C4B4;
extern int D_0086F7C4, D_0086F7C8, D_0086F7CC, D_0086F7D0, D_0086F7D4;
extern "C" int func_005ADBD0(int);
extern "C" void func_005ADCC0(int);
extern "C" int func_005ADCE0(int);
extern "C" int func_005B72A8(void);
extern "C" int func_005B72F8(void);
#define REG(x) (*(volatile u32 *)(x))
extern "C" int func_00550188(S *s) {
 int thread = D_0064C4B4;
 if (thread != -1) func_005ADBD0(thread);
 func_005ADCE0(s->sema);
 int tag = 4;
 func_005B72A8();
 u32 saved = REG(0x1000f520);
 REG(0x1000f590) = saved | 0x10000;
 REG(0x1000b400) = tag;
 REG(0x1000f590) = saved;
 func_005B72F8();
 u32 control = REG(0x1000b400);
 char *tags = s->tags;
 u32 index;
 switch (REG(0x1000b410) == (((u32)tags + D_0086F7C4*16+16) & 0xfffffff)) {
 case 0: index = ((u32)*(int *)0x1000b410 - s->data) >> D_0086F7CC; break;
 default: index = 0; break;
 }
 int consumed = ((index - s->head) + D_0086F7C4) & D_0086F7C8;
 int count = s->count - consumed;
 int head = (s->head + consumed) & D_0086F7C8;
 int bytes = s->bytes;
 s->count = count;
 s->head = head;
 int blocks = bytes >> D_0086F7CC;
 s->bytes = bytes & D_0086F7D4;
 if (blocks > 0) {
  int end = head + count;
  index = (end + D_0086F7C4 - 1) & D_0086F7C8;
  tag = (D_0086F7D0 >> 4) & 0xffff;
  tag |= 0x30000000;
  *(u64 *)(tags + index * 16) = tag | ((u64)((s->data + (index << D_0086F7CC)) & 0xfffffff) << 32);
  index = end & D_0086F7C8;
  for (int k = 0; k < blocks - 1; k++) {
   *(u64 *)(s->tags + index * 16) = tag | ((u64)((s->data + (index << D_0086F7CC)) & 0xfffffff) << 32);
   index = (index + 1) & D_0086F7C8;
  }
  tag &= 0x8fffffff;
  *(u64 *)(s->tags + index * 16) = (u32)tag | ((u64)((s->data + (index << D_0086F7CC)) & 0xfffffff) << 32);
  s->count += blocks;
 }
 if (s->count != 0) {
  if (blocks > 0) control = (control & 0x8fffffff) | 0x30000000;
  func_005B72A8();
  saved = REG(0x1000f520);
  REG(0x1000f590) = saved | 0x10000;
  REG(0x1000b400) = control | 0x100;
  REG(0x1000f590) = saved;
  func_005B72F8();
 }
 func_005ADCC0(s->sema);
 return 0;
}
