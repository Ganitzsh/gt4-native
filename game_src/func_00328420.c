#include "types.h"
#include "gt4/RefCounter.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

extern char RefCounter__vtable[];
s32 RefCounter__structor_0(struct RefCounter *arg0) {
    arg0->unk0 = 0;
    arg0->unk4 = (s32)RefCounter__vtable;
    return (s32)arg0;
}
/* native correction of src/adhoc/RefCounter/RefCounter__structor_0.c: returns arg0 (this); on the PS2 it never writes $v0, which still holds the pointer its callers allocated and passed as this. */
