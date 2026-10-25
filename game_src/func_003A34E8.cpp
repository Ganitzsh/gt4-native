#include "gt4/RaceDisplayDiffEvent.h"
extern "C" void *RaceDisplayEventBase__structor_0(void *arg0);
extern "C" char RaceDisplayDiffEvent__vtable[];

extern "C" void *RaceDisplayDiffEvent__structor_0(struct RaceDisplayDiffEvent *arg0)
{
    void *r = RaceDisplayEventBase__structor_0(arg0);
    arg0->unkC = 0;
    arg0->unk8 = 0;
    arg0->unk4 = RaceDisplayDiffEvent__vtable;
    return r;
}
/* native correction of src/race/unit_003A34E8/RaceDisplayDiffEvent__structor_0.cpp: returns the $v0 of RaceDisplayEventBase__structor_0 (-1 on the PS2), which the derived constructors return. */
