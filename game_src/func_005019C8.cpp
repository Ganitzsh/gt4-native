struct S {char pad[472];char a[12],b[12],c[12];};
extern "C" int func_005012A8(void*,int);
extern "C" int func_005019C8(S* s,int value) {
 int r;
 if(!(r=func_005012A8(s->a,value)) && !(r=func_005012A8(s->b,value)))r=func_005012A8(s->c,value);
 return r;
}
/* native correction of src/network/unit_00494578/func_005019C8.cpp: returns the result of the last func_005012A8 evaluated */
