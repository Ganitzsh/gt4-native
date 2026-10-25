/* compiler: ee-gcc2.96-nsa-nosib */
typedef int s32;
struct Db { char p[0x1E0]; int f1E0; int f1E4; };
extern Db D_006235A8;
extern "C" int func_00443DC0(void *, int);
extern "C" int func_00449CD8(int, int);

extern "C" long long func_00447D18(int arg0, long unusedw0) {
    if (func_00443DC0(&D_006235A8, 0x2A) != 0) {
        return func_00449CD8(D_006235A8.f1E4, arg0);
    }
    return func_00449CD8(D_006235A8.f1E0, arg0);
}
/* native correction of src/race/unit_004271E8/func_00447D18.cpp: returns the result of func_00449CD8 (sign-extended, as the caller func_004541C0 reads it as s64) that the PS2 left in $v0 */
