/* compiler: ee-gcc2.96-nogcse */
struct Vec {float x,y,z;};
// Provisional body layout: only position and shape index are accessed here.
struct Self {char pad[0x10];Vec pos;char pad1[0x44-0x1c];unsigned char index;};
// Negative back links close a polygon by indexing relative to its next vertex.
struct Point {float x,y,z;short back;unsigned short flags;};
struct Params {char pad[0x90];float radius;};
extern char* D_006D6054;
extern "C" void func_003E4708(Self *, const Vec *, float, float);
inline float inverseRoot(float a,float b){float r;__asm__("rsqrt.s %0,%1,%2":"=f"(r):"f"(a),"f"(b));return r;}
extern "C" int func_003E44E8(Self*s,Point*start,const Vec*old,const Vec*points){
 if(start->flags&2)return 0;
 {
 Point*end=start+1;
 if(start->back<0)end+=start->back;
 float sx=start->x,sz=start->z;
 float ex=end->x,ez=end->z;
 float px=s->pos.x,pz=s->pos.z;
 float dx=ex-sx,dz=ez-sz;
 float rx=px-sx,rz=pz-sz;
 float len=dx*dx+dz*dz;
 float t=(dx*rx+dz*rz)/len;
 Params*p=(Params*)(D_006D6054+s->index*160+32);
 // First resolve overlap with the interior of the edge.
 if(t>0 && t<1) {
 float rad=p->radius;
 float nx=t*dx-rx,nz=t*dz-rz;
 if(nx*nx+nz*nz<rad*rad) {
 float scale=inverseRoot(rad,len);
 s->pos.x=px+(nx+dz*scale);s->pos.z=pz+(nz-dx*scale);
 func_003E4708(s, points, dx, dz);return 1;
 }
 }
 // Otherwise test whether the motion from old to current crossed this edge.
 float side=dx*rz-dz*rx;
 if(side<0)return 0;
 float ox=old->x,oz=old->z;
 float other=dx*(oz-sz)-dz*(ox-sx);
 if(other>0)return 0;
 Vec delta;
 delta.x=px-ox;delta.z=pz-oz;delta.y=s->pos.y-old->y;
 float py=s->pos.y;
 float startX=sx-px, startZ=sz-pz;
 if(delta.x*startZ-delta.z*startX<0)return 0;
 float endX=end->x-px, endZ=end->z-pz;
 if(delta.x*endZ-delta.z*endX>0)return 0;
 float q=side/(side-other);
 s->pos.x=px-q*delta.x;s->pos.y=py-q*delta.y;s->pos.z=pz-q*delta.z;
 func_003E4708(s, points, dx, dz);return 1;
 }

}
/* native correction of src/race/unit_003E20A8/func_003E44E8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003E4708 */
