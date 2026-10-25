/* Draws one object into a context: 0045B4D8 and 0045B548 bracket the work with the same 16-byte buffer (saving and restoring some state of
 * the context, inferred from the begin/end pairing), a virtual call through the owner's table entry at +0xD0 supplies a part for 00339FE0,
 * every item of the list at block+0x60 goes through 00339F38 (the do-while is the compiler's form of a guarded loop), and the two optional
 * parts at +0x35E8 and +0x3238 are drawn only when drawFlagA / drawFlagB are nonzero. floatParam is passed through unchanged; what the
 * calls draw is purpose unknown. */
#include "types.h"

struct VEnt { s16 off; s16 pad; void *(*fn)(void *, s32); };
struct VTab { char pad[0xD0]; VEnt e; };
struct List { u32 count; s32 unk4; char **items; };
struct B { char pad[0x60]; List *unk60; };
struct A {
    char pad[0x64 - 0];
    char *unk64;
    char pad2[4];
    B *unk6C;
};

extern char D_0069F2F8[];
extern "C" int func_0045B4D8(void *, void *, void *);
extern "C" void func_00339FE0(void *, void *, f32) throw();
extern "C" void func_00339F38(void *, char *, void *, s32, f32) throw();
extern "C" void func_003E5408(void *, void *);
extern "C" void func_003C2990(void *, void *);
extern "C" void func_0045B548(void *, void *);

extern "C" void func_0033A018(void *context, A *owner, s32 param2, s32 drawFlagA, s32 drawFlagB, f32 floatParam) {
    s32 savedState[4];
    u32 itemIndex = 0;
    func_0045B4D8(savedState, context, D_0069F2F8);
    VEnt *vcallEntry = (VEnt *)(owner->unk64 + 0xD0);
    B *block = owner->unk6C;
    func_00339FE0(context, vcallEntry->fn((char *)owner + vcallEntry->off, 0), floatParam);
    if (block->unk60->count != 0) {
        do {
            func_00339F38(context, block->unk60->items[itemIndex], owner, param2, floatParam);
            itemIndex++;
        } while (itemIndex < block->unk60->count);
    }
    if (drawFlagA != 0) {
        func_003E5408((char *)owner + 0x35E8, context);
    }
    if (drawFlagB != 0) {
        func_003C2990((char *)owner + 0x3238, context);
    }
    func_0045B548(savedState, context);
}
/* native correction of src/race/unit_00339910/func_0033A018.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00339F38 */
