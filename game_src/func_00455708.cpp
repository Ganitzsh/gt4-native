/* compiler: ee-gcc2.96-as2004 */
/* Native version of 00455708 (draft with VU0 asm): draws model `index` of a set by running its display program
 * (one opcode byte, then operands; 91 opcodes: meshes, textures, render state, branches on clipping, LOD and the
 * environment's hooks). Clipping (clip below) runs points (three floats, any alignment) through the view in shared
 * vectors 24..27 (w = 1) and VU0's CLIP test against w, for the point and for the point scaled by shared vector 23.
 * Everything but clip is the draft's, unchanged except for a dropped prefetch. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
struct Packed { u64 xy; int z; } __attribute__((packed));
struct Word { union { u32 i; float f; }; } __attribute__((packed));
struct Hook { virtual void unused(); virtual int select(int); virtual int lod(float); };
struct Env { int unk0; float *values; char pad8[12]; Hook *hook; };
struct Model { u8 flags, count; char pad2[2]; Packed *points; char pad8[16]; u8 *code; char pad1c[12]; };
struct Texture { char pad[24]; int id; };
struct Set { char pad0[22]; unsigned short count; char pad18[6]; unsigned short levels; char pad20[24]; Model *models; int *meshes; int unk40; Texture ***textures; char pad48[52]; Env *env; };
extern int D_00623890,D_00623894,D_00623898,D_006238A8,D_006238B0,D_006238B4,D_006238B8,D_006238BC;
extern float D_0062389C,D_006238A0,D_008465E4,D_008465E8;
extern int D_008465F0[];
extern "C" float func_00454F40(void *) throw();
extern "C" int func_00454FB0(float) throw() __attribute__((const));
extern "C" int func_00455040(void *) throw();
extern "C" void func_00455598(void *,int,int) throw();
extern "C" void func_00457278(void *,int,int,void *);
extern "C" int func_00499728(void *);
extern "C" int func_0049D308(int);
extern "C" int func_004A4050(unsigned);
extern "C" int func_004A26F0(int);
extern "C" int func_004A7C00();
extern "C" int func_004A7CD0();
extern "C" void func_00456850(float);
extern "C" void func_0049CE10(int);
extern "C" void func_0049CE58(int);
extern "C" void func_0049CE88(int);
extern "C" void func_0049E3E0();
extern "C" void func_0049F3C0(int,float);
extern "C" void func_0049F7A0(float,float,float);
extern "C" void func_004A1638(int);
extern "C" void func_004A19B0(int);
extern "C" void func_004A27C8(int);
extern "C" void func_004A2858(int,float);
extern "C" void func_004A28B8(int,int);
extern "C" void func_004A2908(int,int);
extern "C" void func_004A2980(int);
extern "C" void func_004A29A8(int);
extern "C" void func_004A3020(float);
extern "C" void func_004A5348(int);
extern "C" void func_004A53F8();
extern "C" void func_004A5400();
extern "C" void func_004A60E0(float,float,float,float);
extern "C" void func_004A6100(float,float,float,float);
extern "C" void func_004A74B4(void *);
extern "C" void func_004A7698(void *);
extern "C" void func_004A75D8(float,float,float,float);
extern "C" void func_004A7844(float,float,float);
extern "C" void func_004A7890(float,float,float);
extern "C" void func_004A7988(float);
extern "C" void func_004A79B0(float);
extern "C" void func_004A79D8(float);
extern "C" void func_004A7B40(float);
extern "C" void func_004AB040(int);

static inline int read16(u8 *p) { return (p[1]<<8)|p[0]; }
static inline int signed16(u8 *p) { return ((signed char)p[1]<<8)|p[0]; }
static inline float readf(u8 *p) { return ((Word *)p)->f; }
static inline unsigned readw(u8 *p) { return ((Word *)p)->i; }
/* VU0's CLIP judgement of v against |w|: bit 0 x > |w|, 1 x < -|w|, 2 and 3 y, 4 and 5 z */
static inline int clip_flags(const float *v) {
    const float w = v[3] < 0.0f ? -v[3] : v[3];
    return (v[0] > w) | (v[0] < -w) << 1 | (v[1] > w) << 2 | (v[1] < -w) << 3 | (v[2] > w) << 4 | (v[2] < -w) << 5;
}
/* the sign bit of w (the original tests the stored word, so -0 counts as negative) */
static inline int sign_bit(float f) { u32 u; memcpy(&u, &f, 4); return (u >> 31) != 0; }
/* for a negative w, the original exchanges each pair of flags and inverts them */
static inline int flip(int f) { return (((f >> 1) | 42) ^ ((f << 1) | 21)) & 63; }
/* 0: every point outside one plane (or none); 1: all inside; 2: some outside (and, with D_00623890 set, none
 * outside the plane of bit 4 of the scaled points) */
static inline int clip(Packed *p, unsigned n) {
    func_0049E3E0();
    int outside=63, partial=0;
    const float *ra = gt4_vec(24), *rb = gt4_vec(25), *rc = gt4_vec(26), *rd = gt4_vec(27), *sc = gt4_vec(23);
    for(unsigned i=0;i<n;i++) {
        float v[3], a[4], b[4];
        memcpy(v, p + i, 12);
        gt4_rows4(a, ra, rb, rc, rd, v, 1.0f, 4);
        for (int k = 0; k < 4; ++k) b[k] = a[k] * sc[k];
        int c = clip_flags(a);
        if (sign_bit(a[3])) c = flip(c);
        outside &= c;
        int bits = clip_flags(b);
        if (sign_bit(b[3])) bits = flip(bits);
        partial |= bits;
    }
    int result=0;
    if(!outside && (!D_00623890 || !(partial&16))) result=partial?2:1;
    return result;
}
extern "C" int func_00455708(Set *self,int index,Env *env) {
    float color[4];
    if(index>=self->count)return 0;
    Model *model=self->models+index;
    u8 *p=model->code;
    if(!p)return 0;
    int evaluated=0, unclipped=0;
    if(model->count) {
        int c=clip(model->points,model->count);
        if(!c)return 0;
        if(c==1) { unclipped=1; func_004AB040(17); }
    }
    if(!env)env=self->env;
    int level=0;
    if(D_006238A8) {
        level=D_006238B0;
        if(level<0)level=func_00455040(model);
        if(level>=self->levels)level=self->levels-1;
    }
    int saved=func_004A7C00();
    float *values=env->values;
    for(;;) {
        unsigned op=*p++;
        switch(op) {
        case 0: { if(unclipped)func_004A1638(17); return 1; break; }
        case 2: { int n=*p++; func_00455598(self,n,0); break; }
        case 3: { int n=read16(p);p+=2; func_00455598(self,n,0); break; }
        case 4: { int n=*p++; func_0049D308(self->meshes[n]); D_006238BC++; break; }
        case 5: { int n=read16(p);p+=2; func_0049D308(self->meshes[n]); D_006238BC++; break; }
        case 53: { int base=read16(p);p+=2; int n=read16(p);p+=2; float f=D_008465E8*n; int i=(int)f; if(i>=n)i=n-1; func_004A7B40(f-i); func_0049D308(self->meshes[base+i]); D_006238BC++; break; }
        case 52: { float f=readf(p);p+=4; func_00456850(f); break; }
        case 10: { unsigned n=*p++; Packed *points=(Packed *)p; p+=n*12; int c=clip(points,n); if(c) { p+=2; if(c==1)func_004AB040(17); else func_004A1638(17); } else p+=signed16(p); break; }
        case 11: { func_004A1638(17); break; }
        case 43: { int n=*p++; Texture *t=self->textures[level][n]; if(t){func_00499728(t);func_0049CE88(t->id);} break; }
        case 44: { int n=read16(p);p+=2; Texture *t=self->textures[level][n]; if(t){func_00499728(t);func_0049CE88(t->id);} break; }
        case 33: { unsigned w=readw(p);p+=4;func_004A4050(w); break; }
        case 34: { saved=func_004A7C00(); break; }
        case 35: { func_004A4050(saved); break; }
        case 67: { if(!evaluated) { evaluated=1;func_00457278(self,2,index,env); } break; }
        case 8: { p+=(signed char)*p; break; }
        case 6: { int key=signed16(p); p+=2; unsigned n=*p++; unsigned k=env->hook?env->hook->select(key):0; if(k>=n)p+=n*2; else {p+=k*2;p+=signed16(p);} break; }
        case 89: { int key=*p++; if(env->hook)env->hook->select(key); break; }
        case 90: { int key=signed16(p);p+=2;if(env->hook)env->hook->select(key); break; }
        case 7: { float f; if(!D_006238B4)f=0;else {float scale=((Word *)(p+12))->f; float dist=func_00454F40(p);f=(D_008465E4*scale)/dist;} int k=env->hook?env->hook->lod(f):func_00454FB0(f);p+=16;int n=*p++;if(k>=n)k=n-1;p+=k*2;p+=signed16(p); break; }
        case 9: { p+=signed16(p); break; }
        case 12: { func_004A53F8(); break; }
        case 13: { func_004A5400(); break; }
        case 14: { int n=*p++;func_004A5348(n); break; }
        case 15: { void *m=p;p+=64;func_004A74B4(m); break; }
        case 16: { void *m=p;p+=64;func_004A7698(m); break; }
        case 17: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4;func_004A7844(a,b,c); break; }
        case 18: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4;func_004A7890(a,b,c); break; }
        case 19: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4; float d=readf(p);p+=4;func_004A75D8(a,b,c,d); break; }
        case 20: { float a=readf(p);p+=4;func_004A7988(a); break; }
        case 21: { float a=readf(p);p+=4;func_004A79B0(a); break; }
        case 22: { float a=readf(p);p+=4;func_004A79D8(a); break; }
        case 23: { if(D_00623894)func_004A1638(5); break; }
        case 24: { func_004AB040(5); break; }
        case 25: { int n=*p++;func_004A26F0(n); break; }
        case 26: { func_004A1638(4); break; }
        case 27: { func_004AB040(4); break; }
        case 28: { int a=*p++;int b=*p++;func_004A28B8(a,b); break; }
        case 29: { func_004A1638(3); break; }
        case 30: { func_004AB040(3); break; }
        case 31: { int n=*p++;func_004A2858(n,0.5f); break; }
        case 32: { int a=*p++;int b=*p++;func_004A2908(a,b); break; }
        case 36: { unsigned w=readw(p);p+=4;if(D_006238B8)func_004A2980(~w); break; }
        case 37: { if(D_00623898)func_004A29A8(0); break; }
        case 38: { if(D_00623898)func_004A29A8(1); break; }
        case 39: { float a=readf(p);p+=4;func_004A3020(a); break; }
        case 41: { int n=*p++;func_0049CE58(n); break; }
        case 42: { int n=*p++;func_0049CE10(n); break; }
        case 45: { func_004A1638(15); break; }
        case 46: { func_004AB040(15); break; }
        case 78: { func_004A19B0(2); break; }
        case 79: { func_004A19B0(1); break; }
        case 47: { int n=*p++;func_004A27C8(n); break; }
        case 88: { func_004AB040(19);func_004AB040(14); break; }
        case 49: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4;func_0049F7A0(a,b,c); break; }
        case 51: { color[0]=readf(p);p+=4;color[1]=readf(p);p+=4;color[2]=readf(p);p+=4;color[3]=readf(p);p+=4; break; }
        case 50: { p+=4; break; }
        case 54: { p+=4; break; }
        case 40: { p+=16; break; }
        case 55: { p+=16; break; }
        case 64: { func_004A19B0(func_004A7CD0()==1?2:1); break; }
        case 68: { int n=read16(p);p+=2;func_004A7698(values+n); break; }
        case 69: { int n=read16(p);p+=2;float *v=values+n;func_004A7844(v[0],v[1],v[2]); break; }
        case 70: { int n=read16(p);p+=2;float *v=values+n;func_004A7890(v[0],v[1],v[2]); break; }
        case 72: { int n=read16(p);p+=2;float *v=values+n;func_004A75D8(v[0],v[1],v[2],v[3]); break; }
        case 73: { int n=read16(p);p+=2;float *v=values+n;func_004A7988(v[0]); break; }
        case 74: { int n=read16(p);p+=2;float *v=values+n;func_004A79B0(v[0]); break; }
        case 75: { int n=read16(p);p+=2;float *v=values+n;func_004A79D8(v[0]); break; }
        case 76: { int n=read16(p);p+=2;float *v=values+n;func_004A7B40(v[0]); break; }
        case 77: { int i=read16(p);p+=2;int n=*p++;int k=((int *)values)[i];if(k>=n || k<0)p+=n*2;else {p+=k*2;p+=signed16(p);} break; }
        case 65: { p+=3; break; }
        case 66: { p+=3; break; }
        case 80: { int t=D_008465F0[*p++];if(t)func_0049CE88(t); break; }
        case 81: { int n=signed16(p);p+=2;func_0049CE88(D_008465F0[n]); break; }
        case 82: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4; float d=readf(p);p+=4;func_004A60E0(a,b,c,d); break; }
        case 83: { float a=readf(p);p+=4; float b=readf(p);p+=4; float c=readf(p);p+=4; float d=readf(p);p+=4;func_004A6100(a,b,c,d); break; }
        case 84: { int a=read16(p);p+=2;int b=read16(p);p+=2;int c=read16(p);p+=2;int d=read16(p);p+=2;func_004A60E0(values[a],values[b],values[c],values[d]); break; }
        case 85: { int a=read16(p);p+=2;int b=read16(p);p+=2;int c=read16(p);p+=2;int d=read16(p);p+=2;func_004A6100(values[a],values[b],values[c],values[d]); break; }
        case 86: { func_0049F3C0(3,D_0062389C);func_0049F3C0(2,D_006238A0); break; }
        case 87: { func_0049F3C0(3,0);func_0049F3C0(2,1); break; }
        default: break;
        }
    }
}
