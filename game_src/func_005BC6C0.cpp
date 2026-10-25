/* GCC runtime (gcc 2000-10-03 snapshot, libgcc.a): func_005BC6E8? (libgcc2.c).
 * licence: gcc-runtime (GPL with the GCC runtime exception, see THIRD_PARTY.md) */
typedef int s32;
typedef s32 (*FnPtr)(void);

extern FnPtr get_eh_context;

extern "C" s32 func_005BC6C0(void)
{
    return get_eh_context();
}
/* native correction of src/runtime/unit_0059BE80/func_005BC6C0.cpp: returns get_eh_context()'s result */
