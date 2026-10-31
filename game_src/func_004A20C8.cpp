/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A20C8 (matched with VU0 asm): waits for the graphics library's two transfer channels
 * (004A0F68), then for VIF1, VU1 and the GIF to go idle, resetting each one that stays busy too long. None of
 * those three exists natively (the frame is sent whole by runtime/frame.cpp): only the channel waits are left. */
typedef int s32;
extern "C" s32 func_004A0F68(s32 ch);
extern "C" void func_004A20C8(void) {
    func_004A0F68(1);
    func_004A0F68(2);
}
