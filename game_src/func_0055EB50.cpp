/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0055EB50 (matched): a thread that runs 00547B58 and 00568080 once a frame while D_00654A38 is
 * set, woken by its frame callback 0055EB08 (semaphore D_00654A40, frames counted in D_00654A3C, which the thread
 * takes back with interrupts disabled). The original installed 0055EB08 as a VBlank interrupt handler
 * (AddIntcHandler 2, last); natively it is a frame callback of the runtime's clock. */
extern char D_00654A3C[], D_00654A40[];
struct Flag { int v; };
extern volatile Flag D_00654A38;
static inline int active(volatile Flag *const p) { return p->v; }
extern "C" void func_0055EB08(unsigned int);
extern "C" void func_00547B58(void);
extern "C" void func_00568080(void);
extern "C" int func_005782E8(int,int);
extern "C" void func_005783A0(int);
extern "C" void func_00578500(int);
extern "C" int func_005B72A8(void);
extern "C" void func_0055EB50(void) {
 { int *q = (int *)D_00654A40; *q = func_005782E8(0,255); }
 int tick = gt4_frame_tick_add(func_0055EB08, 0, 0);
 while (active(&D_00654A38)) {
  func_00578500(*(int *)D_00654A40);
  int enabled = func_005B72A8();
  (*(int *)D_00654A3C)--;
  if (enabled) gt4_interrupts_enable();
  func_00547B58();
  func_00568080();
 }
 int *q = (int *)D_00654A40;
 func_00578500(*q);
 int enabled = func_005B72A8();
 (*(int *)D_00654A3C)--;
 if (enabled) gt4_interrupts_enable();
 gt4_frame_tick_remove(tick);
 func_005783A0(*q);
}
