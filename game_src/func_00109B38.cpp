/* The menu game object's update (00109B38, matched in gt4-decomp src/startup/unit_00101400/func_00109B38.cpp): runs
 * the current menu project's update once a frame (60 Hz, measured: docs/modern/spikes/M0-menu-flow.md 7), under the
 * object's lock. gt4-native adds one call after it: the settings pages' frame (runtime/menu/menu_host.h,
 * MENU_DESIGN.md M5), on this thread, inside GT4's flow, and the menu sound it asks for, played by name as GT4's own
 * widgets play theirs (mRootWindow::onKeyPress: func_0023F8C8, which ignores its handle). When the hub that stood in
 * for GT4's Options closes (M6), the project is ended here as GT4's own exit from Options ends it: the next project is
 * the last one (MGame's set-next, 0042FC50: last = next, next = name; measured in M0) and the project's event loop is
 * left (MUpdateContext::leaveEventLoop's body, 00251E80), so startProject returns with the result it cleared. */
extern "C" void func_0021DF28(void);
extern "C" void func_00576788(void *);
extern "C" void func_005767C0(void *);
extern "C" void func_0023F8C8(int, const char *);
extern "C" int gt4_menu_tick(unsigned int project);
extern "C" void func_005A609C(char *, const char *);
extern "C" void func_0042FC50(char *, const char *);
extern "C" void func_00251E80(void *);
extern char *D_00622F4C;

struct VEntry {
    short delta;
    short index;
    void (*fn)(void *);
};

struct Sub {
    int pad0;
    char *vt;
};

struct Obj {
    char pad0[0x70];
    Sub *sub;
    char lock[8];
};

extern "C" void func_00109B38(Obj *self)
{
    static const char *const sounds[] = {0, "cursor", "ok", "cancel", "disable"};
    int sound;
    char last[0x20];
    func_00576788(self->lock);
    if (self->sub != 0) {
        func_0021DF28();
        Sub *s = self->sub;
        VEntry *e = (VEntry *)(s->vt + 0x200);
        e->fn((char *)s + e->delta);
    }
    sound = gt4_menu_tick((unsigned int)self->sub);
    if ((sound & 0x100) && self->sub != 0) {   /* menu_host.h kLeaveProject */
        char *g = D_00622F4C + 0x3A368;          /* MGame: last project at + 0x34, next at + 0x54 */
        func_005A609C(last, g + 0x34);
        func_0042FC50(g, last);
        func_00251E80(self->sub);
    }
    sound &= 0xFF;
    func_005767C0(self->lock);
    if (sound > 0 && sound <= 4)
        func_0023F8C8(0, sounds[sound]);
}
