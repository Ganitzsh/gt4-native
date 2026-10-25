struct A { int x; ~A() __asm__("func_00100010"); A() __asm__("func_00100020"); int m() __asm__("func_00100030"); };
void f(A *a) { a->~A(); }
int g() { A a; return a.m(); }
struct V { int y; virtual ~V() __asm__("func_00100040"); };
struct W : V { ~W() __asm__("func_00100050"); };
W::~W() { y = 1; }
void h(V *v) { delete v; }
