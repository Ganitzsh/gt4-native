/* compiler: ee-gcc2.96-no-strict-aliasing */
extern "C" void *func_005A48D8(void *, int, unsigned int);
extern "C" int CameraBase__structor_0(void *);
extern char D_0067A498[];
struct Vec { float v[3]; };
struct Elem { float a,b,c; Vec v0; float d,e,f; Vec v1,v2,v3,v4,v5,v6; };
struct Cam { void *vt; Elem el[2]; char pad[0x168-4-2*sizeof(Elem)]; unsigned long flags; };
extern "C" int func_00378AE0(Cam *self) {
 CameraBase__structor_0(self);
 self->vt = D_0067A498;
 for(int i=0;i<2;i++) {
  self->el[i].a=1.0f; self->el[i].b=1.0f; self->el[i].c=70.0f;
  func_005A48D8(&self->el[i].v0,0,12);
  self->el[i].d=1.0f; self->el[i].e=1.0f; self->el[i].f=1.0f;
  func_005A48D8(&self->el[i].v1,0,12);
  func_005A48D8(&self->el[i].v2,0,12);
  func_005A48D8(&self->el[i].v3,0,12);
  func_005A48D8(&self->el[i].v4,0,12);
  func_005A48D8(&self->el[i].v5,0,12);
  func_005A48D8(&self->el[i].v6,0,12);
 }
 self->flags &= 0xFFFFFFFF00FFFFFFUL;
 return 0x00FFFFFF;
}
/* native correction of src/race/unit_00378778/func_00378AE0.cpp: returns 0x00FFFFFF, the low word of the mask 0xFFFFFFFF00FFFFFF left in $v0 by the flags update. */
