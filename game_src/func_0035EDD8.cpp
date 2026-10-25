extern "C" long long fptodp(float f);
extern "C" int func_00556CA0(void);
extern "C" int func_00556E48(int a, long long *buf);
extern "C" int func_0035EDD8(char *arg0) {
    long long buf[6];
    buf[0] = fptodp(*(float *)(arg0 + 0x4));
    buf[1] = fptodp(*(float *)(arg0 + 0x0));
    buf[2] = fptodp(*(float *)(arg0 + 0x8));
    buf[3] = fptodp(*(float *)(arg0 + 0xC));
    buf[4] = fptodp(*(float *)(arg0 + 0x10));
    buf[5] = fptodp(*(float *)(arg0 + 0x14));
    return func_00556E48(func_00556CA0(), buf);
}
/* native correction of src/race/unit_0034E9A0/func_0035EDD8.cpp: returns the int of its last call func_00556E48 (left in $v0; its declaration made int). */
