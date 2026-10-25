/* compiler: ee-gcc2.96-no-strict-aliasing */
extern "C" void *func_005A48D8(void *, int, unsigned int);
extern "C" int func_004A0160(char *p) {
    float *color = (float *)(p + 0xA60);
    func_005A48D8(color, 0, 0x50);
    *color = 0x1.999998p-3f;
    *(float *)(p + 0xA64) = 0x1.999998p-3f;
    *(float *)(p + 0xA68) = 0x1.999998p-3f;
    *(float *)(p + 0xA6C) = 1.0f;
    *(float *)(p + 0xA70) = 0x1.999998p-1f;
    *(float *)(p + 0xA74) = 0x1.999998p-1f;
    *(float *)(p + 0xA78) = 0x1.999998p-1f;
    *(float *)(p + 0xA7C) = 1.0f;
    *(float *)(p + 0xA8C) = 1.0f;
    *(float *)(p + 0xA9C) = 1.0f;
    *(int *)(p + 0xC28) = 2;
    *(int *)(p + 0xC2C) = 3;
    p[6] = 0;
    *(float *)(p + 0xBA0) = 1.0f;
    *(float *)(p + 0xBA4) = 1.0f;
    *(float *)(p + 0xBA8) = 1.0f;
    *(float *)(p + 0xBAC) = 1.0f;
    *(float *)(p + 0xBB0) = 1.0f;
    *(float *)(p + 0xBB4) = 1.0f;
    *(float *)(p + 0xBB8) = 1.0f;
    *(float *)(p + 0xBBC) = 1.0f;
    *(int *)(p + 0xBC0) = 0;
    *(int *)(p + 0xBC4) = 0;
    *(int *)(p + 0xBC8) = 0;
    *(int *)(p + 0xBCC) = 0;
    return 3;
}
/* native correction of src/network/unit_00494578/func_004A0160.cpp: returns 3, the value $v0 holds at jr $ra (the constant stored to +0xC2C) */
