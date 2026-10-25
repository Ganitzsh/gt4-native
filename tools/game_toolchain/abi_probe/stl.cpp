#include <stl_algobase.h>
#include <stl_alloc.h>
#include <stl_construct.h>
#include <stl_uninitialized.h>
#include <stl_vector.h>
#include <stl_function.h>
#include <stl_tree.h>
#include <stl_map.h>
template<class T> class GameAlloc {
public:
 typedef size_t size_type; typedef ptrdiff_t difference_type;
 typedef T* pointer; typedef const T* const_pointer; typedef T& reference; typedef const T& const_reference; typedef T value_type;
 template<class U> struct rebind {typedef GameAlloc<U> other;};
 GameAlloc() throw() {} GameAlloc(const GameAlloc&) throw() {}
 template<class U> GameAlloc(const GameAlloc<U>&) throw() {}
 ~GameAlloc() throw() {}
 T* allocate(size_type n,const void * = 0);
 void deallocate(T*,size_type);
 size_type max_size() const throw(){return size_t(-1)/sizeof(T);}
 void construct(T*p,const T&v){new(p) T(v);} void destroy(T*p){p->~T();}
};
typedef vector<int, GameAlloc<int> > V;
typedef map<int, long, less<int>, GameAlloc<pair<const int, long> > > M;
int sz_V = sizeof(V), sz_M = sizeof(M), sz_Vd = sizeof(vector<int>);
template class vector<int, GameAlloc<int> >;
void push(V &v, int x) { v.push_back(x); }
long look(M &m, int k) { return m[k]; }
