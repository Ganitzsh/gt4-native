extern void func_00572930(void *ptr, int arg0);
extern char D_008744D0[];

int func_00575DA0(int arg0, int unused0)
{
    func_00572930(D_008744D0, arg0);
    return 0; /* PS2: whatever func_00572930 (the heap free) leaves in $v0: no meaningful value */
}
/* native correction of src/libstream/unit_005676B0/free.cpp: returns an explicit 0 where the PS2 returns free's leftover $v0, which has no meaning */
