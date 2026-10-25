extern int RaceDisplayEventBase__vtable;
struct S { int a,v; };
extern "C" int func_003A3130(S *s) { s->v=(int)&RaceDisplayEventBase__vtable; s->a=-1; return -1; }
/* native correction of src/race/unit_003A3130/RaceDisplayEventBase__structor_0.cpp: a plain function (a constructor cannot return a value) returning -1, the value $v0 holds at its return on the PS2, which RaceDisplayDiffEvent__structor_0 passes on. */
