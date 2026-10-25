struct TextureState { unsigned long words[5]; };
struct Texture { char physicalPrefix[24];TextureState*state; };
extern "C" int func_00499728(Texture*);
extern "C" void func_0049CC48(TextureState*);
extern "C" void func_004A4550(unsigned,int);
extern "C" float func_003AFD18(unsigned, float, float);
extern "C" void func_003AF6E0(float,float,float,float,float,float,float,float);
// PLZCW is the EE packed leading-sign-count hardware operation.
inline unsigned leadingSigns(unsigned x){unsigned r;asm("plzcw %0, %1":"=r"(r):"r"(x));return r;}
inline unsigned extentPower(unsigned n){return 1U<<(31^(leadingSigns(n-1)&31));}
// Drawing helpers' incidental results are not consumed by this physical view.
extern "C" void func_003AF2F0(Texture*t,float x,float y,unsigned xf,unsigned yf,int filter,int blend){
 if(t){func_00499728(t);func_0049CC48(t->state);func_004A4550(2,filter);func_004A4550(3,blend);
 unsigned long bits=t->state->words[4];unsigned width=(bits>>14&1023)+1;unsigned height=(bits>>34&1023)+1;
 float w=(int)width,h=(int)height;float left=func_003AFD18(xf, x, w),top=func_003AFD18(yf, y, h);
 float right=left+w,bottom=top+h;float u=(float)width/(float)extentPower(width),v=(float)height/(float)extentPower(height);
 func_003AF6E0(left,top,right,bottom,0,0,u,v);
 }
}
/* native correction of src/race/unit_003AECB0/func_003AF2F0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003AFD18 */
