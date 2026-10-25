/* GCC runtime (gcc 2000-10-03 snapshot, libgcc.a): a function of the C++ runtime in libgcc.a (cp/tinfo.cc, tinfo2.cc, exception.cc, new*.cc), in libgcc.a's code at 0x5ba060-0x5c1ce0.
 * licence: gcc-runtime (GPL with the GCC runtime exception, see THIRD_PARTY.md) */
extern int free(int);

int func_005C1628(int arg0, int unused0)
{
    if (arg0 != 0) {
        return free(arg0);
    }
    return 0; /* PS2: $v0 untouched (the caller's) */
}
/* native correction of src/runtime/bad_typeid_exception/func_005C1628.cpp: returns free's value (an explicit 0, see game_src/func_00575DA0.cpp) and 0 for a null pointer, where the PS2 leaves meaningless $v0 values */
