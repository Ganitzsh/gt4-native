/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 003E3ED0 (unmatched; from the decomp's draft, corrected against the original): computes
 * and applies a collision impulse to a body at a contact point (relative velocity from the body's spin and
 * the other object's motion, normal from D_00621D28, inertia from the body table at D_006D6054), returning
 * the impulse.  The draft's default restitution was 0.05f (0x3D4CCCCD); the original loads 0x3D4CCCCC
 * (lui 0x3D4C / ori 0xCCCC at 0x3E40A8), one ulp lower. */
typedef unsigned int u128 __attribute__((mode(TI)));
struct V { float x,y,z,w; };
struct Body { char pad[28]; float x,y,z; V angular; char pad2[12]; unsigned char index; char pad3[15]; float impulse; };
extern char *D_006D6054;
extern "C" void func_003E2140(void*,void*,void*) throw();
extern "C" float func_003E21A8(void*,void*) throw();
extern "C" void func_003E21D8(void*,void*) throw();
extern "C" void func_003E2220(void*,void*) throw();
extern V D_00621D28[];
extern "C" int func_003E43B8(Body*,int,V*);
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
 u128 r0,r1,r2,r3,t0,t1,t2,t3;
 __asm__ volatile("vmove.xyzw $vf24, $vf20");
 __asm__ volatile("vmove.xyzw $vf25, $vf21");
 __asm__ volatile("vmove.xyzw $vf26, $vf22");
 __asm__ volatile("vmove.xyzw $vf27, $vf23");
 __asm__ volatile("qmfc2.ni %0, $vf24":"=r"(r0));
 __asm__ volatile("qmfc2.ni %0, $vf25":"=r"(r1));
 __asm__ volatile("qmfc2.ni %0, $vf26":"=r"(r2));
 __asm__ volatile("qmfc2.ni %0, $vf27":"=r"(r3));
 __asm__ volatile("pextlw %0, %1, %2":"=r"(t0):"r"(r1),"r"(r0));
 __asm__ volatile("pextuw %0, %1, %2":"=r"(t1):"r"(r1),"r"(r0));
 __asm__ volatile("pextlw %0, %1, %2":"=r"(t2):"r"(r3),"r"(r2));
 __asm__ volatile("pextuw %0, %1, %2":"=r"(t3):"r"(r3),"r"(r2));
 __asm__ volatile("pcpyld %0, %1, %2":"=r"(r0):"r"(t2),"r"(t0));
 __asm__ volatile("pcpyud %0, %1, %2":"=r"(r1):"r"(t0),"r"(t2));
 __asm__ volatile("pcpyld %0, %1, %2":"=r"(r2):"r"(t3),"r"(t1));
 __asm__ volatile("pcpyud %0, %1, %2":"=r"(r3):"r"(t1),"r"(t3));
 __asm__ volatile("qmtc2.ni %0, $vf24"::"r"(r0));
 __asm__ volatile("qmtc2.ni %0, $vf25"::"r"(r1));
 __asm__ volatile("qmtc2.ni %0, $vf26"::"r"(r2));
 __asm__ volatile("qmtc2.ni %0, $vf27"::"r"(r3));
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
