extern int func_00448458(int, int, int, int);

int func_00448540(int arg0, int arg1, int arg2)
{
    return func_00448458(0, arg0, arg1, arg2);
}
/* native correction of src/race/unit_004271E8/func_00448540.cpp: returns the result of the tail call to func_00448458, left in $v0 on the PS2 */
