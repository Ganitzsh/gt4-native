/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00365790, DynamicsConductor::ControlEnemySpeed (matched): GT4's catch-up ("slow car boost"). Each
 * race step the conductor pairs cars along the running order (from the player outward, or each car with the player,
 * by the event's boost type) and this sets the second car's time scale (+0x444 of its state: its physics runs at
 * that many times real time, 003658A0): a car behind is sped up by up to the event's boost percentage as its gap
 * grows from the boost start to the boost end distance, and a car ahead is slowed down the same way by the restrain
 * percentage. The factors multiply along the chain (capped at 1.3). The percentages and distances come from the
 * event (conductor +0xF818, loaded by 00365480); many events leave them at zero.
 *
 * The host's gameplay.ai_catchup option (runtime: gt4_game_option(0)) changes how much of it applies: 0 = original,
 * 1 = reduced (half of each step's speed-up or slow-down), 2 = off (every car at the event's base speed). 00365688 is
 * ComputeSlowEnemyBoost, 00365718 ComputeFastEnemyRestrain, 00365910 DynamicsConductor::ComputeDifference. */
struct Fade {float amount, start, end;};
struct Rec {char pad[0x444]; float spd; char pad2[0x4B2-0x448]; unsigned char flag;};
struct Self {char pad[0x7C]; Fade fade; float f88; char pad2[4]; float f90; float f94;};
extern "C" float func_00365688(Fade *, Rec *, int, float) throw();
extern "C" float func_00365718(Fade *, float, Rec *, int) throw();
extern "C" void func_00365910(Self *, void *, Rec *, int *, float *);
extern "C" int gt4_game_option(int id); /* runtime/sdk.cpp: the host's gameplay options */
extern "C" float func_00365790(Self *self, void *a1, Rec *rec, float mul){
    int d;
    float t;
    float r = 1.0f;
    const int catchup = gt4_game_option(0);
    if (rec->flag == 0 && catchup != 2) {
        func_00365910(self, a1, rec, &d, &t);
        int pos;
        if (d > 0) pos = 1;
        else if (d < 0) pos = 0;
        else pos = t > 0.0f;
        if (pos) {
            if (self->f88 != 0.0f) r = func_00365688(&self->fade, rec, d, t);
        } else {
            if (self->fade.amount != 0.0f) r = func_00365718(&self->fade, -t, rec, -d);
        }
        if (catchup == 1) r = 1.0f + (r - 1.0f) * 0.5f;
    }
    r = r * mul;
    if (r > 1.3f) r = 1.3f;
    rec->spd = r * self->f94;
    return r;
}
