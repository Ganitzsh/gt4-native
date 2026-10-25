/* Native correction of src/network/unit_00494578/func_00509F80.cpp: the 100-byte struct parameter
 * reaches the EE by invisible reference (a pointer in $a0; the callee copies it), and its
 * callers pass that pointer. Natively a struct by value is copied onto the stack, so the
 * parameter is the pointer here. Does not match on the EE (the original copies the struct). */
struct S { char data[100]; };
extern int *D_0064A220;
extern unsigned int D_0064A228;
extern "C" int func_0057F238(const char *, const char *) throw();
extern "C" void func_00509F80(const S *s, int *out) {
    for (unsigned int i = 0; i < D_0064A228; i++) {
        if (func_0057F238(s->data, (char *)D_0064A220[i]) == 0) {
            *out = D_0064A220[i];
        }
    }
}
