/* compiler: ee-gcc2.96-no-strict-aliasing */
/* RaceBasic::updatePlayCheckPause (0038B0E0, matched in gt4-decomp src/race/unit_0038B0E0/
 * RaceBasic__updatePlayCheckPause.cpp): the race's Start, once a frame while it isn't paused. RaceBasic::updatePlay calls
 * it last; every single-player race class uses it as it is (GT mode, arcade, championship, free practice, license,
 * machine test, mission, free run). Start (RaceInput bit 0x40) exits the auto demo or a replay, else, with no end
 * delay, pauses the race through pauseStart(+0xE400, the mode's RacePause); bit 0x80 pauses B-spec and replays through
 * the SimplePause (+0xE404).
 * gt4-native adds the host's command 6 in a race (MENU_DESIGN.md 3.2 point 5, M9d2): gt4_race_pause_command
 * (runtime/menu/menu_host.cpp) is told the race's state each frame and answers 1 when a command 6 waits and the state is
 * one where GT4's own Start branch would reach pauseStart(+0xE400): then this frame acts as if Start were down, and
 * GT4's own code pauses exactly where Start would. With no command waiting it answers 0 and the function is GT4's as
 * it was. The state (gt4_race_pause_command's `state`): 0 Start would pause now (play, no end delay, past the start);
 * 1 before the start; 2 a B-spec race (its pause is the SimplePause, M9d3); 3 a replay (B-spec's too), the demo, or
 * +0xCDC set; 4 the end delay. */
typedef int s32;
typedef unsigned int u32;
typedef short s16;

struct VE { s16 delta; s16 idx; void *fn; };
struct Cam { char pad[0xBC]; s32 fBC; char padC0[0xD4 - 0xC0]; float fD4; };

struct RaceBasicLayout {
    char pad0[0x64];
    VE *vt;
    char pad68[4];
    Cam *u6C;
    char pad70[0xCC8 - 0x70];
    u32 cc8;
    char padCCC[4];
    s32 cd0;
    char padCD4[8];
    s32 cdc;
    char padCE0[0xD4C - 0xCE0];
    s32 d4c;
    char padD50[0xE400 - 0xD50];
    void *unkE400;
    void *unkE404;
};

extern "C" s32 RaceInput__getButtonDown(void *);
extern "C" int gt4_race_pause_command(unsigned int race, int state);

#define CALL0(o, slot) (((void *(*)(void *))(o)->vt[slot].fn)((char *)(o) + (o)->vt[slot].delta))
#define IS_POS(o) ((o)->u6C->fD4 >= 0.0f)

extern "C" void RaceBasic__updatePlayCheckPause(RaceBasicLayout *self) {
    if (self->cd0 == 0) {
        s32 down = RaceInput__getButtonDown(CALL0(self, 70));
        {
            /* gt4-native: the host's command 6 (M9d2). Past the start: the green light's flag, or no countdown left. */
            const u32 mode = self->cc8;
            const s32 started = self->d4c == 1 || self->u6C->fBC == 0;
            /* (run mode: 0 play, 1 B-spec, 2 replay, 3 B-spec replay, 4 the auto demo) */
            const int state = IS_POS(self) ? 4 : mode == 1 ? 2 : (mode != 0 || self->cdc != 0) ? 3 : !started ? 1 : 0;
            if (gt4_race_pause_command((unsigned int)self, state)) down |= 0x40;
        }
        if (down & 0x40) {
            if (CALL0(self, 152)) {
                VE *e = &self->vt[87];
                ((void (*)(void *, s32, float, float))e->fn)((char *)self + e->delta, 1, 0.5f, 0.5f);
            } else if (self->cdc != 0 || CALL0(self, 23)) {
                VE *e = &self->vt[87];
                ((void (*)(void *, s32, float, float))e->fn)((char *)self + e->delta, 2, 0.5f, 0.5f);
            } else if (!IS_POS(self)) {
                VE *e = &self->vt[150];
                ((void (*)(void *, void *))e->fn)((char *)self + e->delta, self->unkE400);
            }
        }
        if (RaceInput__getButtonDown(CALL0(self, 70)) & 0x80) {
            if (!IS_POS(self)) {
                u32 m = self->cc8;
                s32 big = m >= 2;
                if (big || (m == 1 || m == 3)) {
                    VE *e = &self->vt[150];
                    ((void (*)(void *, void *))e->fn)((char *)self + e->delta, self->unkE404);
                }
            }
        }
    }
}
