/* Native version of 00539718 (from its source): seeds the network code's Mersenne Twister (00539340, state D_0064B720)
   from the entropy words 00539688 collects, the argument and the four EE timer counts. The counts come from
   gt4_timer_count (runtime/eeops.cpp: each timer's ticks of the clock its Tn_MODE selects, from the runtime's clock)
   instead of loads of T0..T3_COUNT (0x10000000, 0x10000800, 0x10001000, 0x10001800), which nothing updates natively. */
struct Data { char pad[0x34]; int value,a,b,c,d; char tail[0xB8]; };
extern "C" void func_00539688(Data *) throw();
extern "C" void func_00539340(Data *, int) throw();
extern "C" void func_00539718(int a) {
    Data data;
    func_00539688(&data);
    data.value = a;
    data.a = gt4_timer_count(0);
    data.b = gt4_timer_count(1);
    data.c = gt4_timer_count(2);
    data.d = gt4_timer_count(3);
    func_00539340(&data, 0x40);
}
