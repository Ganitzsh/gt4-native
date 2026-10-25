extern int func_004F0DB0(void*);extern void func_00578B10(int);
extern "C" int func_004F0D08(void*p){int r;while(!(r=func_004F0DB0(p)))func_00578B10(10000);return r;}
/* native correction of src/network/unit_00494578/func_004F0D08.cpp: returns the last (nonzero) func_004F0DB0 result, which $v0 holds at jr $ra */
