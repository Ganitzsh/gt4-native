/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0042DE50 (from its matched source): its PLZCW (the power of two at or above the texture's width
   and height) in plain C. */
struct TextureState { char physicalPrefix[32];unsigned long clamp; };
struct Texture { char physicalPrefix[24];TextureState*state; };
extern "C" int func_00499728(Texture*);
extern "C" void func_0049CC48(TextureState*);
extern "C" void func_0042DD68(float,float,float,float,float,float,float,float) throw();
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
inline unsigned extentPower(unsigned n){unsigned x=n-1;if((int)x<0)x=~x;unsigned b=0;while(x){b++;x>>=1;}return 1U<<b;}
extern "C" void func_0042DE50(Texture*t,float x0,float y0,float x1,float y1){
 if(t){func_00499728(t);func_0049CC48(t->state);
 unsigned long bits=t->state->clamp;unsigned width=(bits>>14&1023)+1;unsigned height=(bits>>34&1023)+1;
 float u=(float)width/(float)extentPower(width);float v=(float)height/(float)extentPower(height);
 func_0042DD68(x0,y0,x1,y1,0,0,u,v);
 }
}
