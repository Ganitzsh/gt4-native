/* compiler: ee-gcc2.96 */
/* Native copy of 0042F4F0 (matched: the days elapsed in GT mode, today's day number at +0xB6C0 of the game data
 * less that of the calendar's first day, 005797A8(2005, 4, 2); 0042F530 divides it into weeks, the scripts' calendar
 * (MCalendar::getElapsedDate) reads it, and Arcade mode unlocks courses by it) with a test option of the runtime:
 * GT4_DAYS=N makes it read at least N, so Arcade's late courses (Tsukuba Wet at 337 days) can be tested without
 * that much GT mode progress. Nothing is written: the saved calendar is unchanged. Without the option this is
 * the original function. */
struct func_0042F4F0_arg0 {
    char pad0[0xB6C0];
    int unkB6C0;
};
extern int func_005797A8(int, int, int);
extern long gt4_days_floor(void); /* runtime/sdk.cpp: GT4_DAYS, or -1 */

int func_0042F4F0(struct func_0042F4F0_arg0 *arg0) {
    const int days = arg0->unkB6C0 - func_005797A8(0x7D5, 4, 2);
    const long floor = gt4_days_floor();
    return floor >= 0 && days < floor ? (int)floor : days;
}
