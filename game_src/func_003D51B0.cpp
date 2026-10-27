/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 003D51B0 (unmatched; from the decomp's draft, corrected against the original): draws up to four light/glow parts of an object: projects (x,y,z) through VU0, and if the on-screen scale exceeds 0.025, sets per-part colour (intensity*0.1) and ambient 0.079 and calls func_003D49D0 for each enabled part.
 * The draft wrote the ambient constant as 0.079f (0x3DA1CAC1); the original loads 0x3DA1CAC0 (lui 0x3DA1 / ori 0xCAC0 at 0x3D52EC). */
typedef unsigned int u128 __attribute__((mode(TI)));
struct Packed { u128 v; } __attribute__((packed));
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
    float pos[4], proj[4], intensity, other;
    if (table && table->ptr) {
        func_004A53F8();
        func_004A7844(x,y,z);
        func_004A79B0(angle*57.324841f);
        u128 v;
        __asm__ volatile("vnop");
        __asm__ volatile("qmfc2.ni %0, $vf31" : "=r"(v));
        ((Packed *)pos)->v=v;
        float depth=-pos[2];
        if(depth!=0.0f) {
            func_004A5348(1);
            __asm__ volatile("vnop");
            __asm__ volatile("qmfc2.ni %0, $vf28" : "=r"(v));
            ((Packed *)proj)->v=v;
            float scale=proj[0];
            func_004A5348(0);
            scale=__builtin_fabsf(scale/depth);
            if(scale>0.024999999f) {
                int offset=index*16;
                Part *part=self->parts;
                for(int i=0;i<4;i++,part++,offset+=4) {
                    if(part->enabled) {
                        func_004A53F8();
                        func_003D4AA8(part,&intensity,&other);
                        int value=*(int *)(table->ptr+offset+12);
                        intensity*=0.099999994f;
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
