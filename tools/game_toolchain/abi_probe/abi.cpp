/* ABI probe: compiled by ee-gcc 2.96 and by i386-gt4-gcc, outputs compared by abi_compare.py. */
#define OFF(T, f) ((int)&((T *)256)->f - 256)
#define SZ(T) int sz_##T = sizeof(T); int al_##T = __alignof__(T);

/* scalars */
typedef long Long; typedef unsigned long ULong; typedef long long LL; typedef long double LD;
typedef __WCHAR_TYPE__ WC; typedef bool Bool;
SZ(Long) SZ(ULong) SZ(LL) SZ(LD) SZ(WC) SZ(Bool)
enum E1 { E1a, E1b = 300 }; enum E2 { E2a = -1, E2b = 0x7fffffff }; SZ(E1) SZ(E2)
typedef void (*Fp)(); SZ(Fp)

/* mixed struct */
struct Mix { char c; long l; char d; double x; short s; long long ll; float f; char e; int i; };
SZ(Mix)
int o_Mix_l = OFF(Mix, l), o_Mix_d = OFF(Mix, d), o_Mix_x = OFF(Mix, x), o_Mix_s = OFF(Mix, s),
    o_Mix_ll = OFF(Mix, ll), o_Mix_f = OFF(Mix, f), o_Mix_e = OFF(Mix, e), o_Mix_i = OFF(Mix, i);
struct Tail { double d; char c; }; SZ(Tail)
struct Tail2 { long l; int i; }; SZ(Tail2)
struct Arr { char c; long a[3]; short s; }; SZ(Arr)
int o_Arr_a = OFF(Arr, a), o_Arr_s = OFF(Arr, s);
union U { char c[5]; long l; float f; }; SZ(U)
struct Packed { char c; int i; long l; } __attribute__((packed)); SZ(Packed)
struct Al16 { int a; int b __attribute__((aligned(16))); }; SZ(Al16)
int o_Al16_b = OFF(Al16, b);

/* bitfields */
struct Bf1 { unsigned a : 3; unsigned b : 7; unsigned c : 25; char d; }; SZ(Bf1)
int o_Bf1_d = OFF(Bf1, d);
struct Bf2 { char a; int b : 4; short c : 9; long long d : 40; char e; }; SZ(Bf2)
int o_Bf2_e = OFF(Bf2, e);
struct Bf3 { long a : 33; int b : 3; }; SZ(Bf3)
struct Bf4 { char a; unsigned long b : 7; }; SZ(Bf4)
struct Bf5 { short a; int : 0; char b; }; SZ(Bf5)
int o_Bf5_b = OFF(Bf5, b);
/* bit positions: set one field in a zeroed object and read back the word */
unsigned bf1_bits() { union { Bf1 s; unsigned w[3]; } u; u.w[0] = u.w[1] = u.w[2] = 0; u.s.b = 1; return u.w[0]; }

/* classes: vptr after the first polymorphic class's data */
class Base { public: int a; char b; Base(); virtual ~Base(); virtual int f(); virtual int g(int); };
class Plain { public: int p; };
class Der : public Base { public: long l; int c; Der(); ~Der(); int g(int); virtual void h(); };
class Der2 : public Plain, public Der { public: char z; Der2(); int f(); };
class Pre { public: double d; };
class PolyLate : public Pre { public: int q; virtual int k(); PolyLate(); };
SZ(Base) SZ(Der) SZ(Der2) SZ(PolyLate)
int o_Base_b = OFF(Base, b), o_Der_l = OFF(Der, l), o_Der_c = OFF(Der, c);
int o_Der2_p = OFF(Der2, p), o_Der2_z = OFF(Der2, z), o_Der2_a = OFF(Der2, a), o_PolyLate_q = OFF(PolyLate, q);
Base::Base() : a(1), b(2) {}
Base::~Base() {}
int Base::f() { return a; }
int Base::g(int x) { return x + b; }
Der::Der() : l(3), c(4) {}
Der::~Der() {}
int Der::g(int x) { return x * c; }
void Der::h() {}
Der2::Der2() : z(5) {}
int Der2::f() { return z; }
int PolyLate::k() { return q; }
PolyLate::PolyLate() : q(7) {}

/* virtual inheritance */
class VB { public: int v; virtual ~VB(); };
class VD : public virtual VB { public: int w; VD(); };
VB::~VB() {}
VD::VD() : w(9) {}
SZ(VB) SZ(VD)

/* calls: virtual call, delete through base (in-charge), stack object */
int vcall(Base *b) { return b->g(11); }
void vdel(Base *b) { delete b; }
int stackobj() { Der d; return d.c; }
void arrdel(Der *d) { delete[] d; }
/* pointer to member function */
typedef int (Base::*PMF)(int); SZ(PMF)
PMF pmf_g = &Base::g;
int callpmf(Base *b, PMF p) { return (b->*p)(5); }
typedef int Base::*PMD; SZ(PMD)

/* templates and mangling */
template <class T> struct Box { T t; T get() const { return t; } static int n; };
template <class T> int Box<T>::n = 1;
template struct Box<long>;
template struct Box<unsigned long long>;
namespace NS { struct In { int x; void m(const char *, long, unsigned long, signed char, wchar_t, long double, ...); }; }
void NS::In::m(const char *, long, unsigned long, signed char, wchar_t, long double, ...) {}
void over(int, Base &, const Der *, int Base::*) {}
extern "C" void cfun(long) {}
static int st_counter;
int &ref_counter() { static int local = 3; st_counter++; return local; }
