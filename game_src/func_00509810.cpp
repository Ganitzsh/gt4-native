/* Native correction of src/network/unit_00494578/func_00509810.cpp: the 100-byte struct parameter
 * reaches the EE by invisible reference (a pointer in $a0; the callee copies it), and its
 * callers pass that pointer. Natively a struct by value is copied onto the stack, so the
 * parameter is the pointer here. Does not match on the EE (the original copies the struct). */
struct S { char data[100]; };
struct T { S text; short length; };
extern "C" int func_0057F260(const char *) throw();
extern "C" int func_0050FCE8(T *, int *);
extern "C" int func_00509810(const S *s, int *out) {
    T t;
    t.text = *s;
    t.length = func_0057F260(s->data);
    *out = 0;
    return func_0050FCE8(&t, out) == 0 ? 0 : -13;
}
