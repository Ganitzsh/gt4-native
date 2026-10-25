extern void func_00605850();extern bool func_00605638(void*,void*,int,int,void(*)());int func_00474980(char*s,int n,int*p){p++;return func_00605638(s,s+n,p[0],p[0]+p[1]-1,func_00605850);}
/* native correction of src/engine/unit_0046A050/func_00474980.cpp: returns the result of the tail call to func_00605638 (bool), left in $v0 on the PS2 */
