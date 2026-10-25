/* compiler: ee-gcc2.96-stl */
/* Native correction (width): func_0043AFB0 takes (char *, s64); the matched source declares the second int (one register on the EE), natively the callee would read 8 bytes. Declaring it 64-bit adds a conversion on the EE (no longer matches). */
#include <stl_algobase.h>
#include <stl_alloc.h>
#include <stl_construct.h>
#include <stl_uninitialized.h>
#include <stl_vector.h>

typedef int s32;
typedef unsigned char u8;
typedef unsigned int u32;
extern "C" void func_00575DA0(int p) throw();
extern "C" void *func_00575E60(int heap, int size);

template <class T>
class PlainAlloc40 {
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T *pointer;
    typedef const T *const_pointer;
    typedef T &reference;
    typedef const T &const_reference;
    typedef T value_type;
    template <class U> struct rebind { typedef PlainAlloc40<U> other; };
    PlainAlloc40() throw() {}
    PlainAlloc40(const PlainAlloc40 &) throw() {}
    template <class U> PlainAlloc40(const PlainAlloc40<U> &) throw() {}
    ~PlainAlloc40() throw() {}
    T *allocate(size_type n, const void * = 0) {
        return n == 0 ? 0 : (T *)func_00575E60(0x40, n * sizeof(T));
    }
    void deallocate(T *p, size_type n) {
        func_00575DA0((int)p);
    }
    size_type max_size() const throw() { return size_t(-1) / sizeof(T); }
    void construct(T *p, const T &v) { new (p) T(v); }
    void destroy(T *p) { p->~T(); }
};


struct Result { int value; };
static inline int status(const Result *const x) { return x->value; }
typedef unsigned long long u64;
typedef vector<char, PlainAlloc40<char> > Buf;
struct Strm;
extern "C" Result func_001D42E0(Buf *, Strm *, const char *) throw();
extern "C" const char *func_001D4598(void) throw();
extern "C" int func_0057F238(const char *, const char *) throw();
extern "C" void func_0043AFB0(char *, long long);
extern char *D_00622F4C;
extern char D_0068BB20[];
extern char D_00695508[];

extern "C" int func_001D49C8(Strm *st) {
    Buf v;
    Result res = func_001D42E0(&v, st, func_001D4598());
    if (status(&res)) return res.value;
    u64 val = *(u64 *)(v.begin() + 0x50);
    val = ~val;
    if (val == 0) return 6;
    if (func_0057F238(D_0068BB20, D_00695508) == 0) val /= 100;
    if (val > 100000) val = 100000;
    func_0043AFB0(D_00622F4C + 0x348, (int)val);
    return 0;
}
