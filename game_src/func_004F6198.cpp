/* compiler: ee-gcc2.96-no-strict-aliasing */
extern "C" int func_004F0C88(void *) throw();
extern "C" int func_004F0CD0(void *,int) throw();
extern "C" void *func_005A48D8(void *,int,unsigned int);
extern "C" void func_005A6AB0(void *,void *,int) throw();
struct Blob {char bytes[64];};
#define I(p,o) (*(int *)((p)+(o)))
extern "C" int func_004F6198(char *self,int id,char *data) {
 if(func_004F0C88(self)) {
  func_005A48D8(*(void **)(self+0x5a8),0,0xc4);
  char *p=*(char **)(self+0x5a8);
  func_005A6AB0(p+0x15,self+0xaf8,0x11);
  I(p,0x28)=id;
  I(p,0x2c)=0;
  I(p,0x50)=0;
  *(Blob *)(p+0x54)=*(Blob *)(self+0xcc);
  if(data)func_005A6AB0(p+0x30,data,0x20);
  else p[0x30]=0;
  I(self,0x608)=id;
  return func_004F0CD0(self,0x2f);
 }
 return 0;
}
/* native correction of src/network/unit_00494578/func_004F6198.cpp: returns 0 when func_004F0C88 fails and func_004F0CD0's result (1) otherwise, as $v0 holds on the PS2 */
