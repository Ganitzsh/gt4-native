/* compiler: ee-gcc2.9-991111 */
/* Native copy of 0057FD08 (matched; Sony's sceGsResetGraph): mode 0 resets the GS and sets the video mode
 * (SetGsCrt through 005AD8A0) with every GS interrupt masked (GsPutIMR 0xFF00) and the VSync handler it
 * installed removed; mode 1 flushed the GS; mode 5 only sets the video mode. Natively there is no GS: the
 * original's writes to its CSR (reset 0x200, flush 0x100) are left out, and the revision byte it kept from CSR
 * is 0 (what the run time's CSR always held). */
struct D { short s0; short s2; short s4; short s6; int w8; int wc; };
extern struct D *func_0057FE98(void);
extern void func_005AD8A0(int, int, int);
extern void func_005AD9A0(int, int);
extern int func_005ADFE0(int);
extern int func_005AE780(int);

void func_0057FD08(short mode, short a1, short a2, short a3) {
    struct D *d;
    switch (mode) {
    case 0:
        d = func_0057FE98();
        d->s0 = a1;
        d->s2 = a2;
        d->s6 = 0;
        func_005ADFE0(0xFF00);
        d->s4 = (a3 != 0);
        if (d->w8 != 0) {
            func_005AE780(2);
            func_005AD9A0(2, d->wc);
            d->w8 = 0;
            d->wc = 0;
        }
        func_005AD8A0(a1 & 1, a2 & 0xFF, a3 & 1);
        return;
    case 5:
        d = func_0057FE98();
        d->s4 = (a3 != 0);
        d->s0 = a1;
        d->s2 = a2;
        d->s6 = 0;
        func_005AD8A0(a1 & 1, a2 & 0xFF, a3 & 1);
        return;
    default:
        return;
    }
}
