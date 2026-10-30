/* compiler: ee-gcc2.96-no-strict-aliasing */
#include "types.h"
#include "m2c_macros.h"
#include "gt4_gfx.h"
extern "C" void func_0049E480(void *) throw();
extern "C" void func_0049E548(void *, s32, f32) throw();
extern "C" void func_0049E5F8(void *, s32) throw();
extern "C" void func_0049F938(void *, s32, s32) throw();
extern "C" void func_0049F9C8(void *, s32, s32) throw();
extern "C" void func_0049FA20(void *, s32, s32) throw();
extern "C" s32 func_0049FAC8(void *arg0, s32 arg1, M2C_UNK arg2, s32 arg3, s32 arg4, s32 arg5, u8 arg6, long unusedw0) {
    s32 temp_a2;
    s32 temp_v1;
    s32 var_s3;
    u32 var_t0;
    u16 temp_v0;
    void *temp_a1_2;
    void *temp_a3_2;
    void *temp_v0_2;
    void *temp_v1_2;

    if (arg5 < 0x41) {
        var_s3 = M2C_FIELD(arg0, s32 *, 0x14);
        if (var_s3 != 0) {
            temp_a2 = arg5 - 2;
            M2C_FIELD(arg0, s32 *, 0x1020) += arg5;
            M2C_FIELD(arg0, s32 *, 0x1024) += temp_a2;
            M2C_FIELD(arg0, s32 *, 0x1028) += 1;
            if (M2C_FIELD(arg0, u8 *, 0) != 0) {
                func_0049E480(arg0);
            }
            if (arg6 & 0xFF) {
                var_t0 = M2C_FIELD(arg0, u16 *, 0x4A) | 5;
            } else {
                var_t0 = M2C_FIELD(arg0, u16 *, 0x4A) | 4;
            }
            if (arg4 != 0) {
                var_t0 = var_t0 | 0x10;
            } else {
                var_t0 = var_t0 & ~0x10;
            }
            temp_v1 = (arg5 + 3) & ~3;
            {
                /* the output packet's GIF tag (qword 0xC0): NLOOP arg5, EOP, PRE with the primitive, ST RGBAQ XYZF2 */
                u32 giftag[4];
                giftag[0] = arg5 | 0x8000;
                giftag[1] = (var_t0 << 0xF) | 0x30004000;
                giftag[2] = 0x412;
                giftag[3] = 0;
                gt4_gfx_batch_write(arg0, 0xC0, giftag, 1, 0xF);
                /* the positions (V3-32) from arg1 */
                gt4_gfx_batch_unpack(arg0, 0, 0x68, temp_v1, 1, 4, 4, M2C_FIELD(arg0, u32 *, 0x40), 0,
                                     (const void *)(arg1 & 0x0FFFFFFF));
                /* the strip word into qwords 0x40, 0x41 (S-8: its two low bytes, 1 and the strip length) */
                u32 words[1];
                words[0] = (((arg5 * 3) - 6) << 8) | 1;
                gt4_gfx_batch_unpack(arg0, 0x40, 0x62, 2, 1, 4, 4, M2C_FIELD(arg0, u32 *, 0x40), 0, words);
            }
            if (arg4 != 0) {
                func_0049FA20(arg0, arg4, arg5);
            }
            if (arg3 != 0) {
                func_0049F9C8(arg0, arg3, arg5);
            } else {
                func_0049F938(arg0, arg5, arg4);
            }
            if ((M2C_FIELD(arg0, u8 *, 3) == 0) || (M2C_FIELD(arg0, f32 *, 0x20) != 0x0.0p+0f)) {
                var_s3 -= 1;
                func_0049E548(arg0, 0xC1, 0.0f);
                M2C_FIELD(arg0, u8 *, 3) = 1U;
            }
            if (M2C_FIELD(arg0, s64 *, 0x1D8) & M2C_FIELD(arg0, s64 *, 0x1E8)) {
                func_0049E5F8(arg0, 0xD9);
                var_s3 -= 5;
            }
            gt4_gfx_batch_run(arg0, (u32)var_s3 & 0xFFFF, GT4_KEEP_ITOP);
            return var_s3 | 0x14000000;
        }
        return 1;
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_0049FAC8.cpp: returns var_s3 | 0x14000000 on the main path, 1 when field 0x14 is 0 and 0 when arg5 >= 0x41, as $v0 held on the PS2.
 * Natively the batch (the output tag, the positions, the strip word, then 0049FA20/0049F9C8/0049F938's
 * attributes) goes as batch records (compat/game/gt4_gfx.h) instead of DMA tags and VIF codes. */
