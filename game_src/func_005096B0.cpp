/* Native correction of src/network/unit_00494578/func_005096B0.cpp: the 100-byte struct parameter
 * reaches the EE by invisible reference (a pointer in $a0; the callee copies it), and its
 * callers pass that pointer. Natively a struct by value is copied onto the stack, so the
 * parameter is the pointer here. Does not match on the EE (the original copies the struct). */
struct Blob { char data[100]; };
struct Request { Blob blob; short length; };
extern "C" int func_0057F260(const char *) throw();
extern "C" int func_0050F5E8(Request *,int *);
extern "C" int func_005096B0(const Blob *copy,int *out) {
 Request req;
 req.blob=*copy;
 short n=func_0057F260(copy->data);
 *out=0;
 req.length=n;
 return func_0050F5E8(&req,out)==0?0:-13;
}
