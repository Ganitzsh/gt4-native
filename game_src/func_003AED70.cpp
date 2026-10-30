/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003AED70 (from its matched source): its PLZCW (the power of two at or above the texture's width
   and height) in plain C. */
struct TextureState { unsigned long words[5]; };
struct Texture { char prefix[24]; TextureState *state; };
extern "C" int func_00499728(Texture*);
extern "C" void func_0049CC48(TextureState*);
extern "C" void func_003AF6E0(float,float,float,float,float,float,float,float);
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
inline unsigned extentPower(unsigned n){unsigned x=n-1;if((int)x<0)x=~x;unsigned b=0;while(x){b++;x>>=1;}return 1U<<b;}
extern "C" void func_003AED70(Texture*t,float x,float y,float scale){
 if(t){func_00499728(t);func_0049CC48(t->state);
 scale*=0.5f;
 unsigned long bits=t->state->words[4];
 unsigned width=(bits>>14&1023)+1,height=(bits>>34&1023)+1;
 float w=(int)width,h=(int)height;
 float dx=w*scale,dy=h*scale;
 float u=(float)width/(float)extentPower(width),v=(float)height/(float)extentPower(height);
 func_003AF6E0(x-dx,y-dy,x+dx,y+dy,0,0,u,v);
 }
}
