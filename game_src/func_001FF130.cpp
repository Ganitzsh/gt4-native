/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Rep { int len, cap, ref, sel; };
extern Rep D_00659FA8;
extern char mActor__vtable[];
extern "C" void *hObject__structor_0(void *);
extern "C" char *func_005C2560(Rep *);
struct S { int x0; char *vt; int x8, xc; char *name; };
static inline char *grab(Rep *r) {
 char *d = (char *)(r + 1);
 if (r->sel) d = func_005C2560(r);
 else { int n = r->ref + 1; r->ref = n; }
 return d;
}
static inline char *grab_v0(Rep *r, int *v0) {
 char *d = (char *)(r + 1);
 if (r->sel) { d = func_005C2560(r); *v0 = (int)d; }
 else { int n = r->ref + 1; r->ref = n; *v0 = n; }
 return d;
}
extern "C" int mActor__structor_0(S *s) {
 hObject__structor_0(s);
 s->vt = mActor__vtable;
 char **p = &s->name;
 int v0;
 *p = grab_v0(&D_00659FA8, &v0);
 return v0;
}
/* native correction of src/ui/mActor/mActor__structor_0.cpp: returns what $v0 holds at the end on the PS2: the string from func_005C2560 when the empty rep is shared (sel), else the rep's incremented reference count */
