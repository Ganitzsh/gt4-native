/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00423dd8/00423dd8_dd704f7928e2a1a8.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
struct Vec2 {
    float x,y;
    float &operator[](int i) { return (&x)[i]; }
    const float &operator[](int i) const { return (&x)[i]; }
};
extern "C" void func_0057D8A0(float *,float *,float);
static inline void sincos(const float &a,float &s,float &c) { func_0057D8A0(&s,&c,a); }
extern "C" void func_00423DD8(Vec2 *out,const Vec2 *in,const float *angle) {
    float sc[2];
    float a[4];
    a[0]=*angle;
    sincos(a[0],sc[0],sc[1]);
    float c=sc[1],s=sc[0];
    float sy=s*(*in)[1];
    (*out)[0]=c*(*in)[0]-sy;
    (*out)[1]=s*(*in)[0]+c*(*in)[1];
}
