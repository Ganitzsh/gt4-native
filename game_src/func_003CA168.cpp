/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003ca168/003ca168_eb353a0fdbf1c35c.cpp (argument order, argcheck.py orders): func_004860E8 is (float *, int, float t) as defined; the draft put t first, the same EE call but other stack slots natively. */
extern "C" float func_004860E8(float *, int, float);
extern "C" void func_0049FD50(int, void *, int, void *, int, int);
extern "C" void func_004A1638(int);
extern "C" void func_004A2808(int, float);
extern "C" void *func_004A5B70(int);
extern "C" void func_004AA1D8(float, float, float, float);
extern "C" void func_004AB040(int);
struct V { float x,y,z; };
static inline float rsqrt(float x) { float r; asm("rsqrt.s %0, %1, %2" : "=f"(r) : "f"(1.0f), "f"(x)); return r; }
static inline void set(V *v,float x,float y,float z) { v->x=x; v->y=y; v->z=z; }
static inline void sub(float *x,float *y,float *z,V *a,V *b) { *x=a->x-b->x; *y=a->y-b->y; *z=a->z-b->z; }
static inline float norm(float x,float y,float z) { return x*x+y*y+z*z; }
static inline void scale(float *x,float *y,float *z,float s) { *x*=s; *y*=s; *z*=s; }
static inline void cross(float *x,float *y,float *z,float *ax,float *ay,float *az,float *bx,float *by,float *bz) { *x=*ay**bz-*az**by; *y=*az**bx-*ax**bz; *z=*ax**by-*ay**bx; }
static inline void plus(float *x,float *y,float *z,float *ax,float *ay,float *az,float s) { *x+=*ax*s; *y+=*ay*s; *z+=*az*s; }
static inline void minus(float *x,float *y,float *z,float *ax,float *ay,float *az,float s) { *x-=*ax*s; *y-=*ay*s; *z-=*az*s; }
static inline V *get(V **p) { return *p; }
extern "C" void func_003CA168(float *x,float *y,float *z,int n) {
 V u,b,d;
 func_004A1638(6); func_004AB040(15); func_004A2808(68,1.0f); func_004AA1D8(0,0,0,0.8f);
 struct { V *a,*c; unsigned *colors; } buf;
 buf.a=(V*)func_004A5B70(36); buf.c=(V*)func_004A5B70(36); buf.colors=(unsigned*)func_004A5B70(12);
 V *pc=buf.c,*pa=buf.a; unsigned *col=buf.colors; float t=0;
 int i;
 for(i=0;i<24;i++) {
  if(i==23)t=1;
  float xx=func_004860E8(x,n,t); float yy=func_004860E8(y,n,t); float tt=t; t+=0x1.642c84p-5f; float zz=func_004860E8(z,n,tt);
  pa[0].x=xx; pa[0].y=yy; pa[0].z=zz; pa[1].x=xx; pa[1].y=yy; pa[1].z=zz; pa+=2;
  pc[0].x=xx; pc[0].y=yy; pc[0].z=zz; pc[1].x=xx; pc[1].y=yy; pc[1].z=zz; pc+=2;
  col[0]=0x80000000; col[1]=0x80000000; col+=2;
 }
 float *uy=&u.y,*uz=&u.z;
 float *bx=&b.x,*by=&b.y,*bz=&b.z;
 float ux=1.0f; set(&u,ux,0,0); set(&b,0,0,-1);
 V *p=buf.a;
 for(i=0;i<24;i++,p+=2) {
  ux=u.x;
  if(i<23) {
   float *dx=&d.x,*dy=&d.y,*dz=&d.z;
   *dx=p[2].x-p[0].x; *dy=p[2].y-p[0].y; *dz=p[2].z-p[0].z; float len=norm(*dx,*dy,*dz); ux=u.x;
   if(len!=0) {
    scale(dx,dy,dz,rsqrt(len));
    if(*dx!=0) { ux=-*dy; u.x=ux; *uy=*dx; *uz=0; }
    else if(*dy!=0) { u.x=0; *uy=-*dz; *uz=*dy; }
    else { u.x=*dz; *uy=0; *uz=-*dx; }
    ux=u.x; float vy=u.y,vz=u.z; float r=rsqrt(norm(ux,vy,vz));
    ux*=r; vy*=r; vz*=r; set(&u,ux,vy,vz);
    *bx=*dy**uz-*dz**uy; b.y=*dz*ux-*dx**uz; b.z=*dx**uy-*dy*ux;
   }
  }
  V *q=(V*)(i*24+(int)buf.c);
  p[0].x+=ux*0.01f; p[0].y+=*uy*0.01f; p[0].z+=*uz*0.01f; p[1].x-=u.x*0.01f; p[1].y-=*uy*0.01f; p[1].z-=*uz*0.01f;
  plus(&q[0].x,&q[0].y,&q[0].z,bx,by,bz,0.01f); minus(&q[1].x,&q[1].y,&q[1].z,bx,by,bz,0.01f);
 }
 func_0049FD50(48,buf.a,0,buf.colors,0,0); func_0049FD50(48,buf.c,0,buf.colors,0,0); func_004A1638(15);
}
