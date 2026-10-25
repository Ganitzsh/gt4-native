/* compiler: ee-gcc2.96-stl */
#include <stl_algobase.h>
#include <stl_alloc.h>
#include <stl_construct.h>
#include <stl_uninitialized.h>
#include <stl_vector.h>

extern "C" void *func_00326750(int size, int align, const char *name);
extern "C" void func_00326798(void *p, int size, int align, const char *name);
extern "C" void *HObject__tf(void);
extern char D_0069E280[];

struct TypeInfo {
    const char *name;
};

template <class T> struct TypeTag;

template <class T>
class GameAlloc {
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T *pointer;
    typedef const T *const_pointer;
    typedef T &reference;
    typedef const T &const_reference;
    typedef T value_type;
    template <class U> struct rebind { typedef GameAlloc<U> other; };
    GameAlloc() throw() {}
    GameAlloc(const GameAlloc &) throw() {}
    template <class U> GameAlloc(const GameAlloc<U> &) throw() {}
    ~GameAlloc() throw() {}
    T *allocate(size_type n, const void * = 0) {
        return (T *)func_00326750(n * sizeof(T), 4, ((TypeInfo *)TypeTag<T>::tf())->name);
    }
    void deallocate(T *p, size_type n) {
        func_00326798(p, n * sizeof(T), 4, ((TypeInfo *)TypeTag<T>::tf())->name);
    }
    size_type max_size() const throw() { return size_t(-1) / sizeof(T); }
    void construct(T *p, const T &v) { new (p) T(v); }
    void destroy(T *p) { p->~T(); }
};

struct RefCounter;
extern "C" void func_003285A8(RefCounter *p);
extern "C" void func_003285F8(RefCounter *p);
extern "C" int func_002F47F0(RefCounter *p);
extern "C" void *func_005D84B0(void *first, size_t n, const void *x, int);
extern char D_0069E278[];

struct RefCounter {
    char pad[0x24];
    int n1, n2;
    int pad2C;
    void *data;
};

struct HObject {
    RefCounter *p;
    HObject() __asm__("func_0030BB18");
    HObject(const HObject &);
    ~HObject() __asm__("func_00309378");
    HObject &operator=(const HObject &o) {
        if (this != &o) {
            RefCounter *n = o.p;
            if (n) func_003285A8(n);
            if (p) func_003285F8(p);
            p = n;
        }
        return *this;
    }
};
template <> struct TypeTag<HObject> { static void *tf() { return HObject__tf(); } };
template <> inline HObject *__uninitialized_fill_n_aux(HObject *first, size_t n, const HObject &x, __false_type) {
    return (HObject *)func_005D84B0(first, n, &x, 0);
}

struct HStack {
    vector<HObject, GameAlloc<HObject> > a;
    vector<HObject, GameAlloc<HObject> > b;
    int n;
    HStack(int n1, int n2) : a(n1), b(n2), n(0) {}
    static void *operator new(size_t size) { return func_00326750(size, 4, D_0069E280); }
};

struct HFrame {
    virtual ~HFrame();
    static void *operator new(size_t size) { return func_00326750(size, 4, D_0069E278); }
};

struct HCodeFrame : HFrame {
    int code;
    RefCounter *ref;
    void *data;
    HStack *stack;
    HCodeFrame(int c, RefCounter *r, void *d, HStack *s) : code(c), ref(r), data(d), stack(s) {}
    virtual ~HCodeFrame();
};

struct hThread {
    int f0;
    void *vt;
    int f8, fC;
    int frames[4];
    int code;
    RefCounter *ref;
    void *data;
    HStack *stack;
    HObject handle;
    int beginCodeFrame(const HObject &c) __asm__("func_00318B68");
};

extern "C" int func_00318E68(hThread *t, HFrame *f);

int hThread::beginCodeFrame(const HObject &c) {
    HCodeFrame *f = new HCodeFrame(code, ref, data, stack);
    code = func_002F47F0(c.p);
    ref = c.p;
    func_003285A8(ref);
    data = ref ? ref->data : 0;
    stack = new HStack(ref->n1, ref->n2);
    handle = HObject();
    return func_00318E68(this, f);
}
/* native correction of src/adhoc/hThread/HCodeFrame__structor_0.cpp: beginCodeFrame returns the int (frame index) of its last call func_00318E68, which hThread__execCode reads from $v0. */
