/* The menu game object's update (00109B38, matched in gt4-decomp src/startup/unit_00101400/func_00109B38.cpp): runs
 * the current menu project's update once a frame (60 Hz, measured: docs/modern/spikes/M0-menu-flow.md 7), under the
 * object's lock. gt4-native adds one call after it: the settings pages' frame (runtime/menu/menu_host.h,
 * MENU_DESIGN.md M5), on this thread, inside GT4's flow, and the menu sound it asks for, played by name as GT4's own
 * widgets play theirs (mRootWindow::onKeyPress: func_0023F8C8, which ignores its handle). */
extern "C" void func_0021DF28(void);
extern "C" void func_00576788(void *);
extern "C" void func_005767C0(void *);
extern "C" void func_0023F8C8(int, const char *);
extern "C" int gt4_menu_tick(unsigned int project);

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
    func_00576788(self->lock);
    if (self->sub != 0) {
        func_0021DF28();
        Sub *s = self->sub;
        VEntry *e = (VEntry *)(s->vt + 0x200);
        e->fn((char *)s + e->delta);
    }
    sound = gt4_menu_tick((unsigned int)self->sub);
    func_005767C0(self->lock);
    if (sound > 0 && sound <= 4)
        func_0023F8C8(0, sounds[sound]);
}
