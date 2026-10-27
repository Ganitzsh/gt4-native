/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 003D6D30 (unmatched; from the decomp's draft, corrected against the original): reads a
 * level from func_003F33A8(s->p); when it rises above 0.1 (and a random draw from func_003D6D18 is not above
 * it) every idle one of the four 20-byte records at +0x10 is started (rate d = 1/(r*0.1+0.1), phase
 * a = -d*(r*0.02+0.01), angle c = r*360, scale b = r*0.4+0.8, s->a64 = 1); active records advance a by dt*d
 * and stop once a exceeds 1.
 * The draft wrote 0.1f, 0.4f and 0.8f as decimal literals, which the PS2 compiler truncates but a native
 * compiler rounds one ulp higher; the original loads 0x3DCCCCCC (0x3D6D8C/0x3D6DE0), 0x3ECCCCCC (0x3D6E14)
 * and 0x3F4CCCCC (0x3D6EC4). The literals are now written in hex with the original bits. */
struct Rec { float a,b,c,d; int active; };
struct S { int pad0; void *p; int flag; char bytes[4]; Rec rec[4]; int a60,a64,a68; };
extern "C" float func_003D6D18(S *) throw();
extern "C" float func_003F33A8(void *,int *);
extern "C" void func_003D6D30(S *s,float dt) {
    if(s->p) {
        int out=0;
        float v=func_003F33A8(s->p,&out);
        int on = v>0x1.999998p-4f;
        int trigger=0;
        if(on) trigger=s->a60==0;
        s->a60=on;
        if(trigger && v<func_003D6D18(s)) trigger=0;
        S *cursor=s;
        for(int i=0;i<4;i++) {
            Rec *r=&cursor->rec[0];
            cursor=(S *)((char *)cursor+0x14);
            if(r->active) {
                r->a += dt*r->d;
                if(r->a>1.0f) {
                    r->active=0;
                    r->a=0.0f;
                }
            } else if(trigger) {
                r->active=1;
                r->d=1.0f/(func_003D6D18(s)*0x1.999998p-4f+0x1.999998p-4f);
                r->a=-r->d*(func_003D6D18(s)*0x1.47ae14p-6f+0x1.47ae14p-7f);
                r->c=func_003D6D18(s)*360.0f;
                float random=func_003D6D18(s);
                r->b=random*0x1.999998p-2f+0x1.999998p-1f;
                s->a64=1;
            }
        }
    }
}
