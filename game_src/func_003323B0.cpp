/* compiler: ee-gcc2.96-stl */
#include <stl_algobase.h>
#include <stl_alloc.h>
#include <stl_construct.h>
#include <stl_uninitialized.h>
#include <stl_vector.h>

typedef float f32;
typedef int s32;

extern "C" void *func_00575E60(int heap, int size);
extern "C" void free(void *) throw();

template <class T>
class Alloc {
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T *pointer;
    typedef const T *const_pointer;
    typedef T &reference;
    typedef const T &const_reference;
    typedef T value_type;
    template <class U> struct rebind { typedef Alloc<U> other; };
    Alloc() throw() {}
    Alloc(const Alloc &) throw() {}
    template <class U> Alloc(const Alloc<U> &) throw() {}
    ~Alloc() throw() {}
    T *allocate(size_type n, const void * = 0) {
        return n == 0 ? 0 : (T *)func_00575E60(0x10, n * sizeof(T));
    }
    void deallocate(T *p, size_type n) { free(p); }
    size_type max_size() const throw() { return size_t(-1) / sizeof(T); }
    void construct(T *p, const T &v) { new (p) T(v); }
    void destroy(T *p) { p->~T(); }
};

struct V4 { f32 x, y, z, w; };
typedef vector<V4, Alloc<V4> > Vec;

template <> void Vec::insert(Vec::iterator, Vec::size_type, const V4 &) __asm__("func_005F2C48");

struct Rng { char d[0x9D0]; };
extern "C" void func_00579420(Rng *, s32);
extern "C" f32 func_00579548(Rng *);
extern "C" f32 func_005795A0(f32, f32, Rng *);

extern "C" void func_003323B0(Vec *arg0, unsigned int n) {
    arg0->resize(n);
    Rng r;
    func_00579420(&r, 0x12345678);
    for (s32 i = 0; i < (s32)n; i++) {
        V4 *p = &(*arg0)[i];
        f32 x, y;
        do {
            x = func_005795A0(-1.0f, 1.0f, &r);
            y = func_005795A0(-1.0f, 1.0f, &r);
        } while (x * x + y * y > 1.0f);
        p->y = y;
        p->x = x;
        p->z = func_00579548(&r);
        p->w = func_00579548(&r);
    }
}
/* native correction of src/race/unit_00330800/func_003323B0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_005795A0 */
