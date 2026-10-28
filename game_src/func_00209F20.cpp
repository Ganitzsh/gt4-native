/* compiler: ee-gcc2.96 */
/* Native code for 00209F20 (matched; src/ui/unit_00209200/func_00209F20.cpp, corrected for the native
 * calling convention): looks a handle up with 002098B8 and returns a Ref to it (or a null Ref).
 * 002098B8 is Handle (Obj *o, key, x) with its result through a hidden pointer: it finds key in o's map
 * (005D5878). The source called it with only the result pointer; the original's jal at 0x209F30 sets
 * only $a0 (the local handle) and leaves its own $a1-$a3 (the three arguments after its hidden result
 * pointer) for o, key and x. Natively o and key came from the stack past the pointer, garbage.
 * 0020A8D0 is Ref (List *l, int i) with a hidden result pointer: it returns item i of the list's vector
 * (+0x14/+0x18), or a null Ref when i is out of range. The source called it as a constructor Ref(p) with
 * no index; the original's jal at 0x209F68 has $a0 = the result ($s0), $a1 = the handle's pointer
 * (delay slot) and $a2 = 0, set in the delay slot of the bnez at 0x209F44 (0x209F48), so it asks for
 * item 0. Natively i was the stack word after the arguments: any value but 0 gave another node or a
 * null Ref. */
extern "C" int func_002098B8(void *h, int o, int key, int x);
extern "C" void func_0020A4E0(void *h, int in_chrg);

struct Handle {
    void *p;
    int pad[3];
    Handle(int o, int key, int x) { func_002098B8(this, o, key, x); }
    ~Handle() { func_0020A4E0(this, 2); }
};

struct Ref {
    Ref(const int &v, int flags) __asm__("func_00208EE8");
    Ref(void *p, int i) __asm__("func_0020A8D0");
    ~Ref();
    void *p;
    int pad[3];
};

Ref func_00209F20(int unused0, int unused1, int unused2)
{
    Handle h(unused0, unused1, unused2);
    if (h.p == 0)
        return Ref(0, 0);
    return Ref(h.p, 0);
}
