/* compiler: ee-gcc2.96 */
/* Native code for 0033E6E0 (unmatched; from the decomp's draft drafts/0033e6e0/0033e6e0_85536c52d4fc07c5.cpp,
 * corrected for the native calling convention): sets up a view (003993D0/00399490/003994C0) and a
 * half-height copy buffer (001052E0), then draws either with 00399550 (when *arg1 == arg3->a) or with
 * 00399998, and releases the buffers.
 * 00399998 takes (o, tex, view, state, copy, int interlace, float alpha, float r, float g, float b). The
 * original's jal at 0x33E7E0 passes interlace in $t1 = 0 (0x33E7AC), alpha in $f12 = 0.0 (0x33E7C8) and
 * r, g, b in $f13 = $f14 = $f15 = 1.0 (0x33E798, 0x33E7E4, 0x33E7B4). The draft declared three floats and
 * no int, which the EE's separate float registers allow: natively interlace read the 0.0f, alpha 1.0f
 * (should be 0.0), and g and b came from the stack after the arguments (garbage). */
typedef int s32;

extern "C" void func_00105280(void *, s32);
extern "C" s32 func_001052E0(void *, s32, s32, s32);
extern "C" void func_003993D0(void *);
extern "C" void func_00399490(void *, float, float);
extern "C" void func_003994C0(void *);
extern "C" void func_00399550(void *, void *, void *, void *, void *, float, float, float, s32, float);
extern "C" void func_00399998(void *, void *, void *, void *, void *, s32, float, float, float, float);
extern "C" void func_004A1638(s32);
extern "C" void func_004AB040(s32);

struct Dim { s32 a; s32 b; s32 w; s32 h; };

extern "C" void func_0033E6E0(void *arg0, s32 *arg1, void *arg2, Dim *arg3, float fp0, float fp1, float fp2) {
    char sp[0xC0];
    char obj[0x40];
    func_003993D0(sp);
    func_004A1638(5);
    func_004AB040(6);
    func_001052E0(obj, 1, arg3->w, arg3->h / 2);
    func_00399490(sp, fp0, 0x1.553f7c0000000p+0f);
    *(float *)(sp + 8) = fp1;
    *(float *)(sp + 0x10) = fp2;
    func_003994C0(sp);
    if (*arg1 == arg3->a) {
        func_00399550(sp, arg1, arg3, arg2, obj, 1.0f, 1.0f, 1.0f, 0, 1.0f);
    } else {
        func_00399998(sp, arg1, arg3, arg2, obj, 0, 0.0f, 1.0f, 1.0f, 1.0f);
    }
    func_00105280(obj, 2);
    func_00105280(sp + 0x2C, 2);
}
