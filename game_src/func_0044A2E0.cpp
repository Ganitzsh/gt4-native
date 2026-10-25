/* Native correction of src/race/unit_004271E8/func_0044A2E0.cpp: func_0044A048 takes the 64-bit key (long long); the matched source declares it unsigned (one 64-bit register on the EE either way), natively the 4-byte argument would shift the next one. */
#include "types.h"
extern "C" {
int SPEC_DATABASE__DatabaseStorage__getRow(void *, s64, void *);
long long func_0044A048(void *, long long, int);
extern char D_006235A8[];
int func_0044A2E0(void *self, unsigned kind, int unused0) {
    int r = -1;
    s64 id = func_0044A048(self, kind, unused0);
    if (id == -1) return r;
    u8 buf[0x90];
    switch (kind) {
    case 2:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf + 0x10)) r = *(u16 *)(buf + 0x10);
        break;
    case 3:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf + 0x20)) r = *(u16 *)(buf + 0x2C);
        break;
    case 10:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 0xC);
        break;
    case 11:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 0x20);
        break;
    case 13:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(int *)buf;
        break;
    case 14:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(int *)buf;
        break;
    case 1:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 4:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 5:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 21:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 22:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 23:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 24:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)buf;
        break;
    case 25:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 0x38);
        break;
    case 8:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 2);
        break;
    case 27:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 2);
        break;
    case 15:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 16:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 17:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 18:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 19:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 20:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 28:
        if (SPEC_DATABASE__DatabaseStorage__getRow(D_006235A8, id, buf)) r = *(u16 *)(buf + 4);
        break;
    case 31:
        r = 30000;
        break;
    case 6:
    case 7:
    case 9:
    case 12:
    case 26:
    case 29:
    case 30:
    default:
        break;
    }
    return r;
}
}
