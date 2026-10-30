/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00129010 (from its draft, checked against the original): its PLZCW (the power of two at or
   above the texture's width and height) in plain C. */
struct Vec { float x,y; void setX(float a) { x=a; } void setY(float b) { y=b; } };
struct TextureState { char pad[32]; unsigned long bits; };
struct Texture { char pad[24]; TextureState *state; };
struct Obj { char pad[0xc4]; int active; void *view; Texture *tex; char padd0[0x10]; float x,y; };
struct Entry { short adjust; short pad; void (*fn)(void*,int,float*,float*,int); };
struct Render { int pad; Entry *vt; };
extern "C" int func_00499728(Texture*);
extern "C" void func_0049CC48(TextureState*);
extern "C" void func_003BEBA0(void*,float*,float*,float,float);
extern "C" void func_004A1638(int);
extern "C" void func_004A4810(int,unsigned,unsigned);
extern "C" void func_004A4550(int,int);
extern "C" void func_004AA1D8(float,float,float,float);
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
static inline unsigned power(unsigned n) {
 unsigned x=n-1;
 if((int)x<0) x=~x;
 unsigned b=0;
 while(x) { b++; x>>=1; }
 return 1U<<b;
}
extern "C" void func_00129010(Obj *self,Render *renderer,float x,float y) {
 float xy[2];
 Vec pos[4];
 float uv[8];
 if(self->active && self->tex) {
  TextureState *state=self->tex->state;
  func_00499728(self->tex);
  func_0049CC48(state);
  unsigned long bits=state->bits;
  unsigned w=((bits>>14)&1023)+1;
  unsigned h=((bits>>34)&1023)+1;
  func_003BEBA0(self->view,&xy[0],&xy[1],self->x,self->y);
  xy[0]+=x-(float)((int)w>>1);
  xy[1]+=y-(float)((int)h>>1);
  float fw=(float)(int)w; float fh=(float)(int)h;
  float right=xy[0]+fw;
  float bottom=xy[1]+fh;
  Vec *vertices=pos;
  vertices[0].setX(xy[0]); vertices[0].setY(xy[1]); vertices[1].setX(right); vertices[1].setY(xy[1]);
  vertices[2].setX(xy[0]); vertices[2].setY(bottom); vertices[3].setX(right); vertices[3].setY(bottom);
  float u=(float)w/(float)power(w);
  float v=(float)h/(float)power(h);
  float *coords=uv;
  coords[0]=0;coords[1]=0;coords[2]=u;coords[3]=0;coords[4]=0;coords[5]=v;coords[6]=u;coords[7]=v;
  func_004A1638(2);
  func_004A4810(0,0,w-1);func_004A4810(1,0,h-1);
  func_004A4550(4,2);func_004A4550(5,2);
  func_004AA1D8(1,1,1,1);
  Entry *e=(Entry*)((char*)renderer->vt+0x1b0);
  e->fn((char*)renderer+e->adjust,4,(float*)vertices,coords,0);
 }
}
