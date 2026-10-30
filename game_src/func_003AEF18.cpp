/* Native version of 003AEF18 (from its earlier native copy): its PLZCW (the power of two at or above the texture's
   width and height) in plain C. */
// Reconstructed void rendering contract follows audited concrete direct and virtual callers.
// The spelling of the original stripped return typedef is not claimed.
struct TextureState { char physicalPrefix[32];unsigned long clamp; };
struct Texture { char physicalPrefix[24];TextureState*state; };
extern "C" int func_00499728(Texture*);
extern "C" void func_0049CC48(TextureState*);
extern "C" float func_003AFD18(unsigned, float, float);
extern "C" void func_003AF6E0(float,float,float,float,float,float,float,float);
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
inline unsigned extentPower(unsigned n){unsigned x=n-1;if((int)x<0)x=~x;unsigned b=0;while(x){b++;x>>=1;}return 1U<<b;}
// Drawing helpers' incidental results are not consumed by this physical view.
extern "C" void func_003AEF18(Texture*t,float x,float y,unsigned xf,unsigned yf){
 if(t){func_00499728(t);func_0049CC48(t->state);
 unsigned long bits=t->state->clamp;unsigned width=(bits>>14&1023)+1;unsigned height=(bits>>34&1023)+1;
 float w=(int)width;float u=(float)width/(float)extentPower(width);
 float h=(int)height;float v=(float)height/(float)extentPower(height);
 float left=func_003AFD18(xf, x, w),top=func_003AFD18(yf, y, h);float right=left+w,bottom=top+h;
 func_003AF6E0(left,top,right,bottom,0,0,u,v);
 }
}
/* native correction of src/race/unit_003AECB0/func_003AEF18.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003AFD18 */
