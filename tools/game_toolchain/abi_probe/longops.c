/* compiled by i386-gt4-gcc: long is 64-bit */
typedef unsigned long UL; typedef long SL;
UL u_add(UL a, UL b) { return a + b; }
UL u_sub(UL a, UL b) { return a - b; }
UL u_mul(UL a, UL b) { return a * b; }
UL u_div(UL a, UL b) { return a / b; }
UL u_mod(UL a, UL b) { return a % b; }
SL s_div(SL a, SL b) { return a / b; }
SL s_mod(SL a, SL b) { return a % b; }
UL u_shl(UL a, int n) { return a << n; }
UL u_shr(UL a, int n) { return a >> n; }
SL s_shr(SL a, int n) { return a >> n; }
UL u_shl40(UL a) { return a << 40; }
UL u_shr33(UL a) { return a >> 33; }
SL s_shr63(SL a) { return a >> 63; }
int u_lt(UL a, UL b) { return a < b; }
int s_lt(SL a, SL b) { return a < b; }
int u_ge(UL a, UL b) { return a >= b; }
int s_gt(SL a, SL b) { return a > b; }
int eq(UL a, UL b) { return a == b; }
SL from_int(int x) { return x; }
UL from_uint(unsigned x) { return x; }
unsigned to_uint(UL a) { return (unsigned)a; }
UL bits(UL a, UL b) { return (a & b) ^ (a | ~b); }
double to_double(SL a) { return (double)a; }
float to_float(SL a) { return (float)a; }
SL from_double(double d) { return (SL)d; }
UL neg(UL a) { return -a; }
int size_long(void) { return sizeof(long); }
struct S { char c; long l; int i; };
int off_l(void) { return (int)&((struct S *)0)->l; }
UL sum_arr(UL *p, int n) { UL s = 0; while (n--) s += *p++ * 3; return s; }
UL va_sum(int n, ...) { __builtin_va_list ap; UL s = 0; __builtin_va_start(ap, n); while (n--) s += __builtin_va_arg(ap, UL); __builtin_va_end(ap); return s; }
