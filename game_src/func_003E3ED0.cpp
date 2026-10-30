/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003E3ED0 (unmatched; from the decomp's draft, corrected against the original): computes
 * and applies a collision impulse to a body at a contact point (relative velocity from the body's spin and
 * the other object's motion, normal from D_00621D28, inertia from the body table at D_006D6054), returning
 * the impulse. Leaves the transpose of the body's matrix (shared vectors 20..23) in 24..27, which func_003E2220
 * uses to take the normal into body space.
 * Native correction kept from the earlier native copy: the draft's default restitution was 0.05f (0x3D4CCCCD);
 * the original loads 0x3D4CCCCC (lui 0x3D4C / ori 0xCCCC at 0x3E40A8), one ulp lower. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
struct V { float x,y,z,w; };
struct Body { char pad[28]; float x,y,z; V angular; char pad2[12]; unsigned char index; char pad3[15]; float impulse; };
extern char *D_006D6054;
extern "C" void func_003E2140(void*,void*,void*) throw();
extern "C" float func_003E21A8(void*,void*) throw();
extern "C" void func_003E21D8(void*,void*) throw();
extern "C" void func_003E2220(void*,void*) throw();
extern V D_00621D28[];
extern "C" int func_003E43B8(Body*,int,V*);

/* shared vectors 24..27 = the transpose of 20..23 (vmove, then qmfc2/pextlw/pextuw/pcpyld/pcpyud/qmtc2): every
 * lane, copied as bits */
static inline void transpose_20_to_24() {
    unsigned int m[4][4], t[4][4];
    memcpy(m, gt4_vec(20), 64);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) t[k][r] = m[r][k];
    memcpy(gt4_vec(24), t, 64);
}

extern "C" float func_003E3ED0(Body *body,int index,int side,V *hit,char *other) {
 V a,b,c,d,e,f,g;
 float *q=(float*)(other+0x454);
 float x=hit->x-q[0];
 float spin=*(float*)(other+0x480);
 float z=hit->z+q[1];
 char *p=D_006D6054+body->index*160+32;
 V *point=(V*)(p+index*16+16);
 float inv=1.0f/ *(float*)(p+148);
 c.x=q[3]+z*spin; c.z=-q[4]-x*spin; c.y=q[5];
 a.x=x; a.y=hit->y-q[2]; a.z=z;
 func_003E2140(&a,&body->angular,point);
 func_003E21D8(&d,&a);
 d.x+=body->x; d.y+=body->y; d.z+=body->z;
 e.x=d.x-c.x; e.y=d.y-c.y; e.z=d.z-c.z;
 V *normal=&D_00621D28[side*5+func_003E43B8(body,side,hit)];
 transpose_20_to_24();
 float bounce=0x1.999998p-5f; /* 0x3D4CCCCC */
 func_003E2220(&f,normal); func_003E2140(&g,point,&f);
 float ix=*(float*)(p+4)*1.5f;
 float iy=*(float*)(p+8)*1.5f;
 float iz=*(float*)(p+12)*1.5f;
 b.x=g.x/ix; b.y=g.y/iy; b.z=g.z/iz;
 func_003E2140(&a,&b,point); func_003E21D8(&a,&a);
 int flags=((unsigned char*)p)[2];
 if((flags&1)==0) bounce=0.01f;
 float denom=inv+func_003E21A8(normal,&a);
 float impulse=(-(bounce+1.0f)*func_003E21A8(normal,&e))/denom;
 float scale=impulse*inv;
 body->x+=scale*normal->x; body->y+=scale*normal->y; body->z+=scale*normal->z;
 body->angular.x+=impulse*g.x/ix;
 body->angular.y+=impulse*g.y/iy;
 body->angular.z+=impulse*g.z/iz;
 return impulse;
}
