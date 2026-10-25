/* compiled by modern mingw gcc: long long is the 64-bit type */
#include <stdio.h>
typedef unsigned long long UL; typedef long long SL;
UL u_add(UL,UL); UL u_sub(UL,UL); UL u_mul(UL,UL); UL u_div(UL,UL); UL u_mod(UL,UL);
SL s_div(SL,SL); SL s_mod(SL,SL); UL u_shl(UL,int); UL u_shr(UL,int); SL s_shr(SL,int);
UL u_shl40(UL); UL u_shr33(UL); SL s_shr63(SL); int u_lt(UL,UL); int s_lt(SL,SL); int u_ge(UL,UL);
int s_gt(SL,SL); int eq(UL,UL); SL from_int(int); UL from_uint(unsigned); unsigned to_uint(UL);
UL bits(UL,UL); double to_double(SL); float to_float(SL); SL from_double(double); UL neg(UL);
int size_long(void); int off_l(void); UL sum_arr(UL*,int); UL va_sum(int, ...);
static int fails, checks;
#define CK(e, want) do { UL g_ = (UL)(e), w_ = (UL)(want); checks++; if (g_ != w_) { fails++; printf("FAIL %s: got %llx want %llx\n", #e, g_, w_); } } while (0)
int main(void) {
  UL v[] = {0, 1, 2, 3, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x100000000ULL, 0x123456789abcdef0ULL,
            0x8000000000000000ULL, 0xffffffffffffffffULL, 0xfedcba9876543210ULL, 1000000007ULL, 0x00000001fffffffeULL};
  int n = sizeof v / sizeof v[0];
  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
    UL a = v[i], b = v[j]; SL sa = (SL)a, sb = (SL)b;
    CK(u_add(a,b), a+b); CK(u_sub(a,b), a-b); CK(u_mul(a,b), a*b);
    if (b) { CK(u_div(a,b), a/b); CK(u_mod(a,b), a%b); }
    if (sb && !(sa == (SL)0x8000000000000000ULL && sb == -1)) { CK(s_div(sa,sb), sa/sb); CK(s_mod(sa,sb), sa%sb); }
    CK(u_lt(a,b), a<b); CK(s_lt(sa,sb), sa<sb); CK(u_ge(a,b), a>=b); CK(s_gt(sa,sb), sa>sb); CK(eq(a,b), a==b);
    CK(bits(a,b), (a&b)^(a|~b));
  }
  for (int i = 0; i < n; i++) { UL a = v[i]; SL sa = (SL)a;
    for (int s = 0; s < 64; s += 7) { CK(u_shl(a,s), a<<s); CK(u_shr(a,s), a>>s); CK(s_shr(sa,s), sa>>s); }
    CK(u_shl40(a), a<<40); CK(u_shr33(a), a>>33); CK(s_shr63(sa), sa>>63); CK(to_uint(a), (unsigned)a); CK(neg(a), -a);
    CK(to_double(sa) == (double)sa, 1); CK(to_float(sa) == (float)sa, 1);
  }
  CK(from_int(-5), (SL)-5); CK(from_uint(0xffffffffu), 0xffffffffULL); CK(from_double(-1e15), (SL)-1e15);
  CK(from_double(123456789012.7), 123456789012LL);
  CK(size_long(), 8); CK(off_l(), 8);
  UL arr[3] = {1ULL<<40, 5, 0xffffffffULL}; CK(sum_arr(arr,3), (arr[0]+arr[1]+arr[2])*3);
  CK(va_sum(3, (UL)1<<35, (UL)7, (UL)0xffffffff), ((UL)1<<35)+7+0xffffffffULL);
  printf("%d checks, %d failures\n", checks, fails);
  return fails != 0;
}
