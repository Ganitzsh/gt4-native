/* compiler: ee-gcc2.96 */
/* Native copy of 0043AF68 (matched: the player's money, the masked 64-bit value at +0xB8E8 + 0x81E8 of the
 * game data's +0x348 record, read through 004359E0) with a test option of the runtime: GT4_MONEY=N keeps the
 * money from reading below N (it is set back to N, through the game's own setter 004359B0, when it would), so
 * dealers, tuning shops and car purchases can be tested without playing for the credits. Without the option
 * this is the original function. */
extern "C" unsigned long func_004359E0(int arg0);
extern "C" void func_004359B0(int arg0, long arg1);
extern "C" long gt4_money_floor(void); /* runtime/sdk.cpp: GT4_MONEY, or -1 */

extern "C" long func_0043AF68(int arg0) {
    const long floor = gt4_money_floor();
    if (floor >= 0 && (long)func_004359E0(arg0 + 0xB8E8) < floor) func_004359B0(arg0 + 0xB8E8, floor);
    return func_004359E0(arg0 + 0xB8E8);
}
