/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003d03e0/003d03e0_1ba9466589197a0d.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. Likewise Pitmen__update (003CFA98) takes (self, team, cars, float dt), as its draft defines it. */
typedef int s32; typedef float f32; typedef char s8;
#define M2C_FIELD(p,t,o) (*(t)((char *)(p)+(o)))
struct Vec2 { float v[2]; operator float *() { return v; } float &operator[](int i) { return v[i]; } void add(const Vec2 &b) { v[0] += b.v[0]; v[1] += b.v[1]; } void subtract(const Vec2 &b) { v[0] -= b.v[0]; v[1] -= b.v[1]; } };
inline float &elem(float *p,int i) { return p[i]; }
inline void set2(float *p,float x,float y) { elem(p,0)=x; elem(p,1)=y; }
struct Frame { char pad[0x78dc]; float matrix[4][6]; };
template<int O> inline void transform(float *out,Frame *m,const float *v) {
    out[0] = m->matrix[(O-0x78dc)/24][0]*v[0] + m->matrix[(O-0x78dc)/24][1]*v[1] + m->matrix[(O-0x78dc)/24][2];
    out[1] = m->matrix[(O-0x78dc)/24][3]*v[0] + m->matrix[(O-0x78dc)/24][4]*v[1] + m->matrix[(O-0x78dc)/24][5];
}
extern "C" {
float fabsf(float);
float func_00344798(int,float); float func_003447F8(int,float);
float func_00344828(int,float); float func_003448B8(int,float);
int func_0034C190(char*,int); int func_0034C750(char*);
void func_003934A0(char*,int,int,float,float);
void func_00393528(char*,int);
int func_003C9F38(int) throw(); void func_003CAFE0(char*) throw();
int func_003CC7B0(char*) throw(); int func_003CC958(int) throw() __attribute__((const));
void func_003CD430(char*,int,char*) throw();
void func_003CF570(char*,char*,float) throw();
void func_003CFA98(char*,int,char*,float) throw();
void func_003D0D88(char*,int,char*); void func_003D3040(char*,int);
void func_003D3490(float*); void func_003D34A8(float*);
int func_003D34D8(float*,float*);
float func_003F3708(int); float func_003F3720(int); float func_003F3738(int);
void func_0057D8A0(float*,float*,float);
void func_003D03E0(char *arg0, char *arg1, f32 fparg0) {
    float sp0;
    float sp4;
    float bounds[8]; /* 003D34A8 clears 4 pairs (8 floats) */ float world[2]; Vec2 local; Vec2 delta; float circle[3];
    int inner, yoff, coff; char *carbase; int *stateptr;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 temp_f0_3;
    f32 temp_f0_4;
    f32 temp_f0_5;
    f32 temp_f12;
    f32 temp_f13;
    f32 temp_f14;
    f32 temp_f1;
    f32 temp_f20;
    f32 temp_f20_2;
    f32 temp_f21;
    f32 temp_f22;
    f32 temp_f2;
    f32 temp_f2_2;
    f32 temp_f2_3;
    f32 temp_f3;
    f32 temp_f3_2;
    f32 temp_f4;
    f32 temp_f4_2;
    f32 temp_f5;
    f32 temp_f5_2;
    f32 temp_f6;
    f32 temp_f6_2;
    f32 var_f15;
    f32 var_f4;
    s32 *var_a1;
    s32 *var_s0;
    s32 *var_s1_2;
    s32 *var_s2;
    s32 *var_v0_2;
    s32 temp_a0;
    s32 temp_a0_2;
    s32 temp_a0_3;
    s32 temp_a1;
    s32 temp_a1_2;
    s32 temp_s0;
    s32 temp_s2;
    s32 temp_s7;
    s32 temp_v0;
    s32 temp_v0_2;
    s32 temp_v0_3;
    s32 temp_v0_4;
    s32 temp_v1;
    s32 var_a0;
    s32 var_a1_2;
    s32 var_s3;
    s32 var_v0;
    char *temp_a3;
    char *var_a2;
    char *var_fp;
    char *var_s0_2;
    char *var_s1;

    if ((func_003CC7B0(arg0) != 0) && (func_0034C750(arg1) != 0)) {
        temp_s7 = M2C_FIELD(arg1, s32 *, 0xF8A0);
        M2C_FIELD(arg0, s32 *, 0x9670) = (s32) (M2C_FIELD(arg0, s32 *, 0x9670) ^ 1);
        M2C_FIELD(arg0, s32 *, 0x96D0) = (s32) (M2C_FIELD(arg0, s32 *, 0x96D0) + 1);
        var_s3 = 0;
        if (temp_s7 > 0) {
            var_s1 = arg0;
            for (; var_s3 < temp_s7; ++var_s3) {
                temp_v0 = func_0034C190(arg1, var_s3);
                func_0057D8A0(&sp0, &sp4, func_003448B8(temp_v0, 0));
                M2C_FIELD(var_s1, f32 *, 0x7910) = sp0;
                M2C_FIELD(var_s1, f32 *, 0x790C) = sp4;
                M2C_FIELD(var_s1, f32 *, 0x791C) = sp4;
                M2C_FIELD(var_s1, f32 *, 0x7918) = (f32) -sp0;
                temp_f20 = func_00344798(temp_v0, 0);
                temp_f0 = func_00344828(temp_v0, 0);
                M2C_FIELD(var_s1, f32 *, 0x7914) = temp_f20;
                temp_f5 = M2C_FIELD(var_s1, f32 *, 0x790C);
                temp_f6 = M2C_FIELD(var_s1, f32 *, 0x7910);
                temp_f3 = M2C_FIELD(var_s1, f32 *, 0x7918);
                temp_f4 = M2C_FIELD(var_s1, f32 *, 0x791C);
                M2C_FIELD(var_s1, f32 *, 0x7920) = temp_f0;
                M2C_FIELD(var_s1, f32 *, 0x7924) = temp_f5;
                M2C_FIELD(var_s1, f32 *, 0x7928) = temp_f3;
                M2C_FIELD(var_s1, f32 *, 0x7930) = temp_f6;
                M2C_FIELD(var_s1, f32 *, 0x7934) = temp_f4;
                M2C_FIELD(var_s1, f32 *, 0x792C) = (f32) ((-temp_f5 * temp_f20) - (temp_f3 * temp_f0));
                M2C_FIELD(var_s1, f32 *, 0x7938) = (f32) ((-temp_f6 * temp_f20) - (temp_f4 * temp_f0));
                var_s1 += 0xA0;
            }
        }
        var_s3 = 0;
        if (temp_s7 > 0) {
            var_s2 = (int *)(arg0 + 0x9290);
            var_s0 = (int *)(arg0 + 0x9684);
            var_s1_2 = (int *)(arg0 + 0x969C);
            do {
                temp_a0 = func_0034C190(arg1, var_s3);
                *var_s1_2 = *var_s0;
                temp_v0_2 = func_003C9F38(temp_a0);
                *var_s0 = temp_v0_2;
                if (temp_v0_2 != *var_s1_2) {
                    func_003D3040(arg0, var_s3);
                    temp_v1 = *var_s0;
                    if ((temp_v1 != (*var_s1_2 + 1)) && (temp_v1 != 0)) {
                        func_003CD430(arg0, var_s3, arg1);
                    }
                }
                temp_v0_3 = *var_s0;
                if (temp_v0_3 == 1) {
                    *var_s2 = temp_v0_3;
                } else if (temp_v0_3 == 0) {
                    *var_s2 = 0;
                }
                var_s3 += 1;
                var_s2++;
                var_s0++;
                var_s1_2++;
            } while (var_s3 < temp_s7);
        }
        var_s3 = 0;
        var_a2 = (char *)6;
        var_a1 = (int *)(arg0 + 0x4364);
        do {
            temp_v0_4 = var_s3 & 1;
            var_s3 += 1;
            if (temp_v0_4 == M2C_FIELD(arg0, s32 *, 0x9670)) {
                *var_a1 = 1;
            }
            var_a1 += 0xAA4/4;
        } while (var_s3 < 6);
        var_s3 = 0;
        if (temp_s7 > 0) {
            do {
                func_003D0D88(arg0, var_s3, arg1);
                func_003CFA98(arg0, var_s3++, arg1, fparg0);
                var_a2 = arg1;
            } while (var_s3 < temp_s7);
        }
        int allInactive = 1;
        for (var_s3 = 0; var_s3 < temp_s7; ++var_s3) {
            if (*(int *)(arg0 + 0x98B8 + var_s3 * 8)) {
                allInactive = 0;
                break;
            }
        }
        if (allInactive) func_00393528(arg0 + 0x98E8, 1);
        var_s3 = 0;
        if (temp_s7 > 0) {
            char *currentBase = arg0 + 8;
            char *previousBase = arg0 + 12;
            int offset = 0x98B0;
            do {
                int current = *(int *)(currentBase + offset);
                int previous = *(int *)(previousBase + offset);
                offset += 8;
                if (current != previous && current != 0) {
                    func_003934A0(arg0 + 0x98E8, 0x46, 0x40, 1.0f, 1.0f);
                    break;
                }
                ++var_s3;
            } while (var_s3 < temp_s7);
        }
        var_s3 = 0;
        func_003CF570(arg0, arg1, fparg0);
        M2C_FIELD(arg0, s32 *, 0x9678) = -1;
        func_003D34A8(&bounds[0]);
        if (temp_s7 > 0) {
            Frame *frame = (Frame *)arg0;
            carbase = arg0;
            yoff = 0x90F4;
            coff = 0;
            stateptr = (int *)(arg0 + 0xB4);
            do {
                temp_s0 = *stateptr;
                temp_s2 = func_0034C190(arg1, var_s3);
                if ((temp_s0 == 8) && !(fabsf(func_003447F8(temp_s2, 0) - M2C_FIELD((arg0 + yoff), f32 *, 8)) > 2.0f)) {
                    temp_f20_2 = -func_003F3738(temp_s2);
                    inner = 0;
                    var_fp = carbase + 0x3E4;
                    var_s0_2 = arg0 + coff;
                    temp_f21 = -func_003F3708(temp_s2);
                    temp_f22 = func_003F3738(temp_s2) * 0.5f;
                    temp_f0_2 = func_003F3720(temp_s2);
                    bounds[1] = temp_f21;
                    bounds[3] = temp_f0_2;
                    bounds[2] = temp_f22;
                    bounds[0] = temp_f20_2 * 0.5f;
                    float &lx = local[0];
                    float &ly = local[1];
                    do {
                        set2(delta,0.0f,0.0f);
                        lx=M2C_FIELD(var_s0_2, f32 *, 0x3E8);
                        ly=M2C_FIELD(var_s0_2, f32 *, 0x3F0);
                        func_003D3490(&circle[0]);
                        func_0057D8A0(&sp4, &sp0, M2C_FIELD(var_s0_2, f32 *, 0x3F4));
                        if (func_003CC958(inner) != 0) {
                            var_f15 = 0.199999988079071044921875f;
                            var_f4 = 1.19999992847442626953125f;
                        } else {
                            var_f15 = 0.25f;
                            var_f4 = 0.77f;
                        }
                        delta[0] = var_f4 * -sp4;
                        delta[1] = var_f4 * -sp0;
                        float dx = delta[0], dy = delta[1];
                        local.add(delta);
                        circle[0] = var_f15;
                        transform<0x78dc>(world, frame,local);
                        transform<0x7924>(local, frame,world);
                        set2(circle+1, local[0], local[1]);
                        if (func_003D34D8(bounds,circle) != 0) {
                            transform<0x790c>(world, frame,circle+1);
                            transform<0x78f4>(local, frame,world);
                            local.subtract(delta);
                            M2C_FIELD(var_s0_2,f32 *,0x3E8) = lx;
                            M2C_FIELD(var_s0_2,f32 *,0x3F0) = ly;
                            func_003CAFE0(var_fp);
                        }
                        var_fp += 0x178;
                        var_s0_2 += 0x178;
                        temp_a0_3 = inner + 1;
                        inner = temp_a0_3;
                    } while (temp_a0_3 < 5);
                }
                var_s3 += 1;
                frame = (Frame *)((char *)frame + 0xA0);
                yoff += 0xC;
                coff += 0x8D0;
                carbase += 0x8D0;
                stateptr++;
            } while (var_s3 < temp_s7);
        }
    }
}

}
