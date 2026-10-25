#include "types.h"
char *func_0034C190(void *a, s32 b);
void DynamicsConductorLicense__ProcessLap0ControlLine(void *, s32, s32, s32, f32);
void DynamicsConductorMission__ProcessLap0ControlLine(void *arg0, s32 arg1, s32 arg2, s32 arg3, f32 fparg0) {
    char *temp_s2;
    temp_s2 = func_0034C190(arg0, arg1) + 0x104;
    DynamicsConductorLicense__ProcessLap0ControlLine(arg0, arg1, arg2, arg3, fparg0);
    *(s32 *)(temp_s2 + 0x614) = 0;
}
/* native correction of src/race/DynamicsConductorMission_DynamicsConductorTraining/DynamicsConductorMission__ProcessLap0ControlLine.c (gt4-decomp tools/argcheck.py fix --kind classes --native): classes DynamicsConductorLicense__ProcessLap0ControlLine */
