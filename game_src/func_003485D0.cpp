/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003485d0/003485d0_107a8bd387b0d3ec.cpp (argument order, argcheck.py orders): its matched caller func_00348850 passes (self, float, int, float, int); the draft's order is the same EE function but natively read its arguments from other stack slots. */
extern float D_00843598[],D_008435A0[],D_008435A8[],D_008435B0[];
extern float D_008435B8[],D_008435C8[],D_008435D8[];
extern int D_008435E8[];
extern float D_008435F0[],D_008435F8[],D_00843600[],D_00843610[],D_00843620[];
#define F(p,n) (*(float *)((p)+(n)))
extern "C" void func_003485D0(char *p,float x,int a,float y,int b) {
 char *s=p+0x104;
 float cur=F(s,0x468);
 float speed=F(s,0x484);
 float force=(x-cur)*D_00843598[a]-speed*D_008435B0[a];
 float k=D_008435A8[a];
 float limit=D_008435B8[a*2+b];
 float dt=F(p,0x54C);
 float high=D_00843600[a*2+b];
 float spring=D_008435A0[a];
 if(k!=0.0f && ((cur<x && cur<y) || (x<cur && y<cur))) {
  force+=(y-F(s,0x468))*k;
  limit=D_008435D8[a*2+b];
  high=D_00843620[a*2+b];
 } else if((F(s,0x468)>0.0f && x<F(s,0x468)) || (F(s,0x468)<0.0f && F(s,0x468)<x)) {
  force-=F(s,0x468)*spring;
  limit=D_008435C8[a*2+b];
  high=D_00843610[a*2+b];
 }
 speed+=force*dt;
 if(D_008435E8[a]) {
  float v=F(s,0x53C);
  float lo=D_008435F0[a];
  if(lo<v) {
   float hi=D_008435F8[a];
   if(hi<=v) limit=high;
   else limit+=(high-limit)*(v-lo)/(hi-lo);
  }
 }
 if(limit<speed) speed=limit;
 else if(speed< -limit) speed= -limit;
 float next=F(s,0x468)+speed*dt;
 if((F(s,0x468)<=x && x<=next) || (x<=F(s,0x468) && next<=x)) {
  F(s,0x468)=x;
  F(s,0x484)=0.0f;
 } else {
  F(s,0x468)=next;
  F(s,0x484)=speed;
 }
}
