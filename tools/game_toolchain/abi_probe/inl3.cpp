inline int freei(int x) { return x * 3; }
int (*pf)(int) = freei;
inline int g2(int x) __asm__("func_00555550");
inline int g2(int x) { return x * 5; }
int callg(int y) { int (*q)(int) = g2; return q(y); }
inline int g3(int x) __asm__("func_00666660");
inline int g3(int x) { return x * 7; }
int (*pf3)(int) = &g3;
