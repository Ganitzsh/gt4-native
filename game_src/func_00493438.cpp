struct S{unsigned char*p;};extern signed char D_006B0C80[];extern int func_004B95D0(S*,int,int);int func_00493438(S*s,int*out){int n=D_006B0C80[s->p[0]];*out=n;return func_004B95D0(s,1,n);}
/* native correction of src/engine/unit_0046A050/func_00493438.cpp: returns the result of the tail call to func_004B95D0, left in $v0 on the PS2 */
