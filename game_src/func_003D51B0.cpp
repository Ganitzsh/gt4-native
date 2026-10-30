/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003D51B0 (unmatched; from the decomp's draft, corrected against the original): draws up to four
 * light/glow parts of an object. The current matrix moved to (x,y,z) and turned by angle (func_004A7844,
 * func_004A79B0) gives its view depth (-z of shared vector 31); the projection matrix's first row (shared vector 28
 * while func_004A5348(1) selects it) over that depth is its on-screen scale, and above 0.025 each enabled part gets
 * its colour (intensity*0.1), ambient 0.079 and func_003D49D0.
 * Native correction kept from the earlier native copy: the draft wrote the ambient constant as 0.079f (0x3DA1CAC1);
 * the original loads 0x3DA1CAC0 (lui 0x3DA1 / ori 0xCAC0 at 0x3D52EC). */
#include "gt4_vec.h"
struct Part { char pad[0x2c]; int enabled; char tail[0x44]; };
struct Obj { Part parts[4]; int data[1]; };
struct Table { char *ptr; };
extern "C" void func_003D49D0(void *, int, int, int, int, int *) throw();
extern "C" void func_003D4AA8(void *, float *, float *) throw();
extern "C" void func_004A5348(int);
extern "C" void func_004A53F8();
extern "C" void func_004A5400();
extern "C" void func_004A60E0(float,float,float,float);
extern "C" void func_004A6100(float,float,float,float);
extern "C" void func_004A7844(float,float,float);
extern "C" void func_004A79B0(float);
extern "C" void func_003D51B0(Obj *self,int index,int a,Table *table,int b,int c,float x,float y,float z,float angle) {
    float intensity, other;
    if (table && table->ptr) {
        func_004A53F8();
        func_004A7844(x,y,z);
        func_004A79B0(angle*57.324841f);   /* 0x42654CA3 */
        float depth=-gt4_vec(31)[2];
        if(depth!=0.0f) {
            func_004A5348(1);
            float scale=gt4_vec(28)[0];
            func_004A5348(0);
            scale=__builtin_fabsf(scale/depth);
            if(scale>0.024999999f) {   /* 0x3CCCCCCC */
                int offset=index*16;
                Part *part=self->parts;
                for(int i=0;i<4;i++,part++,offset+=4) {
                    if(part->enabled) {
                        func_004A53F8();
                        func_003D4AA8(part,&intensity,&other);
                        int value=*(int *)(table->ptr+offset+12);
                        intensity*=0.099999994f;   /* 0x3DCCCCCC */
                        func_004A60E0(intensity,intensity,intensity,1.0f);
                        func_004A6100(0x1.43958p-4f,0x1.43958p-4f,0x1.43958p-4f,0.0f);
                        func_003D49D0(part,a,value,b,c,self->data);
                        func_004A5400();
                    }
                }
            }
        }
        func_004A5400();
    }
}
