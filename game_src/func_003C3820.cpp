/* compiler: ee-gcc2.96-no-strict-aliasing */
/* RacePause::update (003C3820, matched in gt4-decomp src/race/RacePause/RacePause__update.cpp): one frame of the race
 * pause, called through the pause object's vtable by RaceBasic::updatePause and RaceFreeRun::updatePause, which act on
 * its answer: 1 open, 2 the time limit ran out, 3 continue, 4 exit, 5 retry. RacePause is the pause of every race mode
 * but RaceSolitaire and RaceSinglePlayer (SimplePause, not hooked) and no class derives from it, so this one copy
 * covers them all (MENU_DESIGN.md 3.2, M7). gt4-native adds the native pause page (runtime/menu/menu_host.h):
 * gt4_pause_tick answers -1 with ui.pause = gt4, and the function is then GT4's as it was. With ui.pause = native the
 * page answers instead of the strobe (pause.strb), which is never started, so RacePause::render draws nothing; the
 * page is given the items this pause enables (retry +0x28, confirm exit +0x2C, screen settings +0x30) and the
 * brightness and contrast the strobe's SETVAL writes (+0x34 picks which set). GT4's own ends of a pause, the race's
 * events and the time limit, end the page with it (gt4_pause_end). */
typedef int s32;

struct Inner { char pad[0x18]; s32 f18; };
struct Self {
    char pad0[0xC];
    s32 fC;
    char pad10[4];
    void *f14;
    Inner *f18;
    s32 f1C;
    s32 f20;
    s32 f24;
    s32 f28;
    s32 f2C;
    s32 f30;
    s32 f34;
};

extern char *D_00622F4C;
extern "C" s32 RaceEventQueue__get(void *, s32, s32, s32);
extern "C" s32 PauseBase__checkTimeLimit(void *);
extern "C" void PauseBase__clearTimeCount(void *);
extern "C" s32 func_003EBF40(void *, void *, s32);
extern "C" void func_004751D0(void *, s32);
extern "C" void func_00475488(void *, float);
extern "C" int gt4_pause_tick(unsigned int pause, int items, int *screen);
extern "C" void func_004857C8(const char *);
extern "C" void gt4_pause_end(unsigned int pause);

extern "C" s32 RacePause__update(Self *self, s32 arg1, char *arg2) {
    if (arg2 != 0) {
        char *t = arg2 + 0xF4;
        if (RaceEventQueue__get(t, 0x16, 0, 1) != 0) {
            self->f20 = 1;
        }
        if (RaceEventQueue__get(t, 0x17, 0, 1) != 0) {
            self->f24 = 1;
        }
    }
    if (self->f20 != 0) {
        gt4_pause_end((unsigned int)self);
        return 3;
    }
    if (self->f24 != 0) {
        gt4_pause_end((unsigned int)self);
        return 4;
    }
    {
        /* menu_host.h kPauseRetry 1, kPauseConfirmExit 2, kPauseScreen 4 */
        int items = (self->f28 != 0 ? 1 : 0) | (self->f2C != 0 ? 2 : 0) | (self->f30 != 0 ? 4 : 0);
        int *screen = (int *)(D_00622F4C + (self->f34 != 0 ? 0x39E44 : 0x39E34));
        s32 k = gt4_pause_tick((unsigned int)self, items, screen);
        if (k >= 0) {
            static const char *const sounds[] = {0, "cursor", "ok", "cancel", "disable"};
            int sound = (k >> 8) & 0xF;   /* menu_host.h MenuSound, played by name as the strobe's PLAY_SOUND does */
            k &= 0xFF;
            if (sound > 0 && sound <= 4)
                func_004857C8(sounds[sound]);
            self->fC = 0;   /* the strobe stays stopped: RacePause::render draws nothing */
            if (k >= 2) {
                self->f1C = k;
                PauseBase__clearTimeCount(self);
                return k;
            }
            self->f1C = 1;
            if (k == 1) {
                PauseBase__clearTimeCount(self);
            } else if (PauseBase__checkTimeLimit(self) != 0) {
                gt4_pause_end((unsigned int)self);
                return 2;
            }
            return 1;
        }
    }
    if (self->fC == 0) {
        self->fC = 1;
        if (self->f14 != 0) {
            if (self->f18 != 0) {
                func_004751D0(self->f14, 0);
            }
        }
    }
    self->f1C = 1;
    s32 s2 = 0;
    if (self->f14 != 0) {
        func_00475488(self->f14, 0x1.111110p-6f);
    }
    s32 r;
    Inner *q = self->f18;
    if (q != 0) {
        s2 = func_003EBF40(q, self->f14, arg1);
        q = self->f18;
        r = self->f1C;
        if (q != 0 && q->f18 != 0) {
            PauseBase__clearTimeCount(self);
            self->fC = 0;
            return 3;
        }
    }
    else {
        r = self->f1C;
    }
    if (s2 == 0) {
        if (PauseBase__checkTimeLimit(self) != 0) {
            self->fC = 0;
            return 2;
        }
    } else {
        PauseBase__clearTimeCount(self);
    }
    return r;
}
