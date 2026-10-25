/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned char u8;
typedef unsigned short u16;
extern "C" u16 *func_004683A8() throw();
extern "C" int func_00468418(float) throw() __attribute__((const));
extern "C" void *func_00468690(void *,int) throw();
extern "C" u8 *func_00433C10(void *,void *);
extern "C" int func_004336A0(void *,void *);
extern u8 *D_00622F4C;
extern "C" int func_00468860(u8 *src,int key,int mode,int doubled,float f) {
 u8 keybuf[16]; u8 output[8];
 u16 *base=func_004683A8();
 int pos=func_00468418(f);
 int i=0;
 int last=7;
 int maximum=65535;
 int off=pos;
 do {
  u16 *out=(u16 *)(off*2+(int)base);
  u16 *in=(u16 *)(i*16+(int)src);
  for(int j=0;j<8;j++,out+=8,in++) {
   if(pos!=0 || j!=last) {
    int val=*in,old=*out;
    int delta=val-old;
    if(doubled) {val=old+2*delta; if(val>maximum)val=maximum;}
    *out=val;
   }
  }
  i++;off+=64;
 }while(i<3);
 if(mode>=0) {
  u8 *obj=func_00433C10(D_00622F4C+0x14a30,func_00468690(keybuf,key));
  if(obj) {
   int count=2; if(mode==0) count=1;
   u8 *prior=obj+0x228;
   i=0;
   if(count!=0) {
   u16 *sum=base+0x78;
   u8 *row=src;
   int maxbyte=255;
   u8 *dest=output;
   int off=0;
   do {
    pos=0;
    u16 *in=(u16 *)(row+0x30);
    do {
     int index=off+pos;
     pos++;
     int val=*in++ >> 8;
     int old=prior[index];
     int delta=val-old;
     if(doubled) {delta*=2;val=old+delta;if(val>=256)val=maxbyte;delta=val-old;}
     *sum+=delta;
     dest[index]=val;
    } while(pos<4);
    i++;sum+=64;row+=8;off+=4;
   }while(i<count);
   }
   return func_004336A0(obj,output);
  }
 }
 return 0;
}
/* native correction of src/engine/unit_00463840/func_00468860.cpp: returns func_004336A0's result, or 0 when mode < 0 or no object is found, as $v0 held on the PS2 */
