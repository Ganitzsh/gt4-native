/* compiler: ee-gcc2.96-no-strict-aliasing */
struct S { char pad0[8]; int count; char padC[12]; int w,h; char pad20[20]; float f34; };
extern "C" void func_00497020(float,float,float,float) throw();
extern "C" int func_00497298(S *,int,int,int,int) throw();
extern "C" void func_004976D0(S *,int,int) throw();
extern "C" void func_00497778(S *, int, int, float) throw();
extern "C" void func_004A1638(int);
extern "C" void func_004A2930(int,int,int,int);
extern "C" void func_004A29A8(int);
extern "C" void pglPushAttrib(int);
extern "C" void func_004A5688();
extern "C" void func_004A6140(int,int,int,int);
extern "C" void func_004AA168(int);
extern "C" void func_004AA1D8(float,float,float,float);
extern "C" void func_004AB040(int);
extern "C" int func_00497C48(S *s, unsigned char flag, float x,float y,float size,float strength) {
 int n=(int)size;
 y+=0.5f; x+=0.5f;
 float nf=(float)n;
 float half=nf*0.5f;
 float yy=(float)(int)y;
 float xx=(float)(int)x;
 int top=(int)(yy-half);
 int left=(int)(xx-half);
 int right=left+n;
 int bottom=(int)((float)top+nf);
 if(right>=0) {
  if(s->w<left || bottom<0) return 0;
  if(s->h>=top) goto draw;
 }
 return 0;
draw:
   if(right-left<3)right=left+2;
   if(bottom-top<3)bottom=top+2;
   func_004A2930(0,0,0,1);func_004A29A8(0);func_004AA168(0);
   func_004AB040(5);func_004AB040(6);
   func_004A6140(left,top,right+2,bottom+2);
   func_004A1638(5);func_004AA1D8(1.0f,1.0f,1.0f,2.0f);
   if(flag) {func_00497020(xx,yy,half,s->f34);strength*=1.2732406f;}
   else func_004A6140(left,top,right,bottom);
   if(s->w-1<right)right=s->w-1;
   if(s->h-1<bottom)bottom=s->h-1;
   if(left<0)left=0;
   if(top<0)top=0;
   pglPushAttrib(0x887);func_004A2930(0,0,0,1);
   func_00497298(s,left,top,right+1,bottom+1);
   int width=right-left;int height=bottom-top;
   func_004976D0(s,width,height);
   func_004A2930(1,1,1,1);
   func_00497778(s, width, height, strength);
   func_004A5688();func_004A29A8(1);
   s->count++;
   return 1;
}
/* native correction of src/network/unit_00494578/func_00497C48.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00497778 */
