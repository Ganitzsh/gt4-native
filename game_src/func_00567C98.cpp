typedef int (*FnPtr)(void *arg0);

struct Obj00567C98 {
    char pad0[0x20];
    FnPtr unk20;
};

extern "C" int func_00567C98(struct Obj00567C98 *arg0) {
    return arg0->unk20(arg0);
}
/* native correction of src/libstream/unit_005676B0/func_00567C98.cpp: returns the result of the function pointer it calls */
