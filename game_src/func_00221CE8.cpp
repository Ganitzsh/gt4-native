/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef int s32;

struct Blob { char b[0x258]; };

struct NetConf {
    s32 pad0;
    void *vt;
    char pad8[8];
    Blob data;
    NetConf(const Blob &src);
};

extern char mNetConf__vtable[];
extern "C" void hObject__structor_0(void *);

extern "C" int mNetConf__structor_1(NetConf *self, const Blob &src) {
    hObject__structor_0(self);
    self->vt = mNetConf__vtable;
    self->data = src;
    return *(const int *)(src.b + 0x240);
}
/* native correction of src/ui/mNetConf/mNetConf__structor_1.cpp: returns the low word of the struct copy's last 8-byte load (src+0x240), left in $v0 on the PS2 */
