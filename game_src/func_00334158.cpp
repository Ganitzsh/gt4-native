/* compiler: ee-gcc2.96-stl */
/* Native code for 00334158 (unmatched; from the decomp's draft, corrected against the original): converts
 * the owner's YCbCr tile set back into one RGB image (chroma from the first image, luma from each tile,
 * clamped to 0..255), clears the tiles, swaps the result into self->image and calls func_00331520.
 * The draft computed the green channel with the textbook coefficient 0.34414 for Cb; the original multiplies
 * by the double at D_0069F130, which is 0.344 (0x3FD604189374BC6A: lui $at, 0x6A / ld $a1, -0xED0($at)
 * at 0x334478, passed to dpmul). */
/* SGI STL (include/stl/stl_vector.h) instantiated by tools/stl_vector.py: vector<signed char, PlainAlloc40> insert_fill */
#include <stl_algobase.h>
#include <stl_alloc.h>
#include <stl_construct.h>
#include <stl_uninitialized.h>
#include <stl_vector.h>

extern "C" void *func_00326750(int size, int align, const char *name);
extern "C" void func_00326798(void *p, int size, int align, const char *name);
extern "C" void *func_00575E60(int heap, int size);
extern "C" void free(void *p) throw();

/* gcc 2.96's type_info: the name first, the vtable pointer after it */
struct TypeInfo {
    const char *name;
};

/* typeid(T).name() without typeid: the game's own __tf getter */
template <class T> struct TypeTag;

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
        free(p);
    }
    size_type max_size() const throw() { return size_t(-1) / sizeof(T); }
    void construct(T *p, const T &v) { new (p) T(v); }
    void destroy(T *p) { p->~T(); }
};

typedef signed char Elem;

typedef vector<Elem, PlainAlloc40<Elem> > Vec;

template <> void Vec::insert(Vec::iterator, Vec::size_type, const Elem &);
extern "C" unsigned func_00575D00();
extern "C" void func_005CD968(void *);
extern "C" void func_00331520(void *,int,int) throw();
extern "C" void func_003355F0(void *);
extern "C" int D_006201A8;
extern double D_0069F130;
struct Img {
    Vec pixels;
    int width,height,cols,rows,channels;
    float aspect;
    unsigned size() { int n=width*channels; n*=height; return n*(cols*rows); }
    void init(int w,int h,int c,float a) {
        unsigned old=size();
        width=w; height=h; cols=1; rows=1; channels=c;
        unsigned n=w*c*h;
        unsigned available=func_00575D00();
        if(n!=old) { func_005CD968(this); if(available>=n) pixels.resize(n); }
        aspect=a;
    }
    Img(int w,int h,int c,float a) { init(w,h,c,a); }
    unsigned char *data() { return (unsigned char *)pixels.begin(); }
    unsigned char *pixel(int x,int y) { return data()+width*channels*y+x*channels; }
    void clear() { func_005CD968(this); width=0; height=0; }
    void swap(Img &other) {
        pixels.swap(other.pixels);
        ::swap(width,other.width); ::swap(height,other.height);
        ::swap(cols,other.cols); ::swap(rows,other.rows);
        ::swap(channels,other.channels); ::swap(aspect,other.aspect);
    }
};
struct Owner {
    int unknown,width,height,cols,rows;
    float aspect;
    int unknown18;
    Img image;
    char pad44[16];
    Img images[1];
};
extern "C" void func_00334158(Owner *self) {
    if(D_006201A8) { func_003355F0(self); return; }
    int count=self->cols*self->rows;
    Img out(self->width*self->cols,self->height*self->rows,3,self->aspect);
    Img *source=&self->image;
    unsigned char *p=source->data();
    for(int y=0;y<self->height;++y) {
        for(int x=0;x<self->width;++x) {
            int r=p[0],g=p[1],b=p[2];
            p+=3;
            float cr=r*0.5f-g*0x1.ad0e56p-2f-b*0x1.4bc6a6p-4f;
            float cb=r*-0x1.5a1cacp-3f-g*0x1.53f7cep-2f+b*0.5f;
            for(int j=0;j<self->rows;++j) {
                for(int i=0;i<self->cols;++i) {
                    int xx=x*self->cols+i;
                    int yy=y*self->rows+j;
                    unsigned char *dest=out.pixel(xx,yy);
                    int idx=j*self->cols+i;
                    Img *tile=(Img *)((char *)self+0x54+idx*0x28);
                    if(idx==count-1) { dest[0]=r;dest[1]=g;dest[2]=b; }
                    else {
                        float lum=*tile->pixel(x,y);
                        int rr=(int)(lum+cr*0x1.66e978p0f);
                        int gg=(int)((double)(lum-cr*0x1.6d9168p-1f)-(double)cb*0.344);
                        int bb=(int)(lum+cb*0x1.c5a1cap0f);
                        dest[0]=min(255,max(0,rr));
                        dest[1]=min(255,max(0,gg));
                        dest[2]=min(255,max(0,bb));
                    }
                }
            }
        }
    }
    for(int k=0;k<count-1;++k) self->images[k].clear();
    self->image.swap(out);
    func_00331520(source,self->cols,self->rows);
}
