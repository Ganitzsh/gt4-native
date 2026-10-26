/* The EE instructions of the decomp's inline asm, as C (C89 and C++, gcc 2.9).  tools/game_cc.py
 * (tools/game_toolchain/asm_translate.py) turns each asm statement it can into a block that copies
 * the operands into the temporaries below, runs these helpers in the order of the asm's
 * instructions and copies the outputs back:
 *
 *   - a general-purpose register ("r", "d", $8...) is a __gt4_reg: the EE's 128-bit GPR.  A 32-bit
 *     value is sign-extended into it, as the EE keeps it; the non-multimedia instructions write the
 *     low 64 bits and keep the high 64 (EE), the MMI ones all 128;
 *   - a floating-point register ("f", $f0...) is a float;
 *   - VU0 (COP2: $vf0-31, ACC, $vi0-31, Q, I, R, P) is one register file for the whole program,
 *     __gt4_vu0, defined by the native runtime (runtime/native/vu0.cpp), which also runs VU0
 *     microprograms (vcallms).  VU0 float arithmetic is single precision here (x87 with the
 *     runtime's precision control); the VU's clamping of infinities and denormals is not modelled.
 */
#ifndef GT4_ASM_H
#define GT4_ASM_H

typedef union {
  unsigned long long d[2];
  long long sd[2];
  unsigned int w[4];
  int sw[4];
  unsigned short h[8];
  short sh[8];
  unsigned char b[16];
  signed char sb[16];
  float f[4];
} __gt4_reg;

/* The VU0 register file (layout shared with runtime/native/vu0.h). */
struct __gt4_vu0_state {
  float vf[32][4];
  float acc[4];
  float q, i, p, pad0;
  unsigned int r;          /* the random register: 0x3F800000 | 23 random bits */
  unsigned int clip;       /* clipping flags (vi18) */
  unsigned int vi[32];     /* integer registers 0-15, control registers 16-31 (cfc2/ctc2) */
};

#ifdef __cplusplus
extern "C" {
#endif
extern struct __gt4_vu0_state __gt4_vu0;
void __gt4_vu0_callms (unsigned int addr);  /* vcallms: run the VU0 microprogram at addr (bytes) */
void __gt4_ee_mtc0 (int reg, unsigned int value);
int __gt4_ee_syscall (int number, unsigned int a0, unsigned int a1, unsigned int a2, unsigned int a3);
#ifdef __cplusplus
}
#endif

/* ---- operands --------------------------------------------------------------------------------- */

static __inline__ void __gt4_to_reg (__gt4_reg *r, const void *p, unsigned int n)
{
  if (n >= 16)
    *r = *(const __gt4_reg *) p;
  else if (n == 8)
    { r->d[0] = *(const unsigned long long *) p; r->d[1] = 0; }
  else if (n == 4)
    { r->sd[0] = *(const int *) p; r->d[1] = 0; }
  else if (n == 2)
    { r->d[0] = *(const unsigned short *) p; r->d[1] = 0; }
  else
    { r->d[0] = *(const unsigned char *) p; r->d[1] = 0; }
}

static __inline__ void __gt4_from_reg (void *p, const __gt4_reg *r, unsigned int n)
{
  const unsigned char *s = r->b;
  unsigned char *d = (unsigned char *) p;
  unsigned int k;
  if (n > 16)
    n = 16;
  for (k = 0; k < n; k++)
    d[k] = s[k];
}

/* Whether the low n bytes of a register equal the value at p (the shadow of a variable that held a
 * 128-bit register: see asm_translate.py). */
static __inline__ int __gt4_low_match (const __gt4_reg *r, const void *p, unsigned int n)
{
  const unsigned char *a = r->b, *b = (const unsigned char *) p;
  unsigned int k;
  if (n >= 16)
    return 0;
  for (k = 0; k < n; k++)
    if (a[k] != b[k])
      return 0;
  return 1;
}

static __inline__ float __gt4_fpr_in (const void *p, unsigned int n)
{
  if (n == 4)
    return *(const float *) p;
  return (float) *(const double *) p;
}

static __inline__ void __gt4_fpr_out (void *p, unsigned int n, float f)
{
  if (n == 4)
    *(float *) p = f;
  else if (n == 8)
    *(double *) p = f;
}

#define __GT4_TO_REG(r, e) do { __typeof__ (e) __gt4_v = (e); \
  __gt4_to_reg (&(r), &__gt4_v, sizeof (__gt4_v)); } while (0)
#define __GT4_TO_REG_SH(r, e, sh) do { __typeof__ (e) __gt4_v = (e); \
  __gt4_to_reg (&(r), &__gt4_v, sizeof (__gt4_v)); \
  if (__gt4_low_match (&(sh), &__gt4_v, sizeof (__gt4_v))) (r) = (sh); } while (0)
#define __GT4_FROM_REG(lv, r) do { __typeof__ (lv) __gt4_v; \
  __gt4_from_reg (&__gt4_v, &(r), sizeof (__gt4_v)); (lv) = __gt4_v; } while (0)
#define __GT4_TO_FPR(f, e) do { __typeof__ (e) __gt4_v = (e); \
  (f) = __gt4_fpr_in (&__gt4_v, sizeof (__gt4_v)); } while (0)
#define __GT4_FROM_FPR(lv, f) do { __typeof__ (lv) __gt4_v; \
  __gt4_fpr_out (&__gt4_v, sizeof (__gt4_v), (f)); (lv) = __gt4_v; } while (0)

/* ---- integer (non-MMI: the low 64 bits) ------------------------------------------------------ */

#define __GT4_SET32(d, v) ((d)->sd[0] = (int) (v))
static __inline__ void __gt4_set64 (__gt4_reg *d, unsigned long long v) { d->d[0] = v; }

/* memory */
static __inline__ void __gt4_lq (__gt4_reg *d, const char *a)
{ *d = *(const __gt4_reg *) ((unsigned int) a & ~15u); }
static __inline__ void __gt4_sq (const __gt4_reg *s, char *a)
{ *(__gt4_reg *) ((unsigned int) a & ~15u) = *s; }

/* ---- MMI (128 bits) ---------------------------------------------------------------------------- */

static __inline__ void __gt4_pcpyld (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ unsigned long long lo = t->d[0], hi = s->d[0]; d->d[0] = lo; d->d[1] = hi; }
static __inline__ void __gt4_pcpyud (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ unsigned long long lo = s->d[1], hi = t->d[1]; d->d[0] = lo; d->d[1] = hi; }
static __inline__ void __gt4_pcpyh (__gt4_reg *d, const __gt4_reg *t)
{
  unsigned short a = t->h[0], b = t->h[4];
  int k;
  for (k = 0; k < 4; k++)
    { d->h[k] = a; d->h[k + 4] = b; }
}
/* pext{l,u}{b,h,w}: interleave the low (l) or high (u) halves of t (even lanes) and s (odd). */
static __inline__ void __gt4_pextb (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t, int up)
{
  __gt4_reg r; int k, o = up ? 8 : 0;
  for (k = 0; k < 8; k++)
    { r.b[2 * k] = t->b[o + k]; r.b[2 * k + 1] = s->b[o + k]; }
  *d = r;
}
static __inline__ void __gt4_pexth (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t, int up)
{
  __gt4_reg r; int k, o = up ? 4 : 0;
  for (k = 0; k < 4; k++)
    { r.h[2 * k] = t->h[o + k]; r.h[2 * k + 1] = s->h[o + k]; }
  *d = r;
}
static __inline__ void __gt4_pextw (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t, int up)
{
  __gt4_reg r; int k, o = up ? 2 : 0;
  for (k = 0; k < 2; k++)
    { r.w[2 * k] = t->w[o + k]; r.w[2 * k + 1] = s->w[o + k]; }
  *d = r;
}
/* ppac{b,h,w}: the even lanes of t (low half) and s (high half). */
static __inline__ void __gt4_ppacb (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{
  __gt4_reg r; int k;
  for (k = 0; k < 8; k++)
    { r.b[k] = t->b[2 * k]; r.b[k + 8] = s->b[2 * k]; }
  *d = r;
}
static __inline__ void __gt4_ppach (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{
  __gt4_reg r; int k;
  for (k = 0; k < 4; k++)
    { r.h[k] = t->h[2 * k]; r.h[k + 4] = s->h[2 * k]; }
  *d = r;
}
static __inline__ void __gt4_ppacw (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{
  __gt4_reg r;
  r.w[0] = t->w[0]; r.w[1] = t->w[2]; r.w[2] = s->w[0]; r.w[3] = s->w[2];
  *d = r;
}

/* Lane-wise operations: op 0 add, 1 sub, 2 ceq, 3 cgt, 4 max, 5 min, 6 add unsigned saturated,
 * 7 sub unsigned saturated, 8 add signed saturated, 9 sub signed saturated. */
#define __GT4_LANES(NAME, FIELD, SFIELD, N, UT, ST, UMAX, SMIN, SMAX) \
static __inline__ void NAME (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t, int op) \
{ \
  __gt4_reg r; int k; \
  for (k = 0; k < N; k++) \
    { \
      ST a = s->SFIELD[k], b = t->SFIELD[k]; UT ua = s->FIELD[k], ub = t->FIELD[k]; long long v; \
      switch (op) \
        { \
        case 0: r.FIELD[k] = (UT) (ua + ub); break; \
        case 1: r.FIELD[k] = (UT) (ua - ub); break; \
        case 2: r.FIELD[k] = ua == ub ? (UT) UMAX : 0; break; \
        case 3: r.FIELD[k] = a > b ? (UT) UMAX : 0; break; \
        case 4: r.SFIELD[k] = a > b ? a : b; break; \
        case 5: r.SFIELD[k] = a < b ? a : b; break; \
        case 6: v = (long long) ua + ub; r.FIELD[k] = v > (long long) UMAX ? (UT) UMAX : (UT) v; break; \
        case 7: r.FIELD[k] = ua > ub ? (UT) (ua - ub) : 0; break; \
        case 8: v = (long long) a + b; r.SFIELD[k] = v > SMAX ? SMAX : v < SMIN ? SMIN : (ST) v; break; \
        default: v = (long long) a - b; r.SFIELD[k] = v > SMAX ? SMAX : v < SMIN ? SMIN : (ST) v; break; \
        } \
    } \
  *d = r; \
}
__GT4_LANES (__gt4_plb, b, sb, 16, unsigned char, signed char, 0xFF, -128, 127)
__GT4_LANES (__gt4_plh, h, sh, 8, unsigned short, short, 0xFFFF, -32768, 32767)
__GT4_LANES (__gt4_plw, w, sw, 4, unsigned int, int, 0xFFFFFFFFu, (-2147483647 - 1), 2147483647)

static __inline__ void __gt4_pand (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ d->d[0] = s->d[0] & t->d[0]; d->d[1] = s->d[1] & t->d[1]; }
static __inline__ void __gt4_por (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ d->d[0] = s->d[0] | t->d[0]; d->d[1] = s->d[1] | t->d[1]; }
static __inline__ void __gt4_pxor (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ d->d[0] = s->d[0] ^ t->d[0]; d->d[1] = s->d[1] ^ t->d[1]; }
static __inline__ void __gt4_pnor (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t)
{ d->d[0] = ~(s->d[0] | t->d[0]); d->d[1] = ~(s->d[1] | t->d[1]); }

/* Shifts by an immediate: kind 0 left, 1 right logical, 2 right arithmetic. */
static __inline__ void __gt4_psh (__gt4_reg *d, const __gt4_reg *t, int sa, int kind)
{
  int k;
  for (k = 0; k < 8; k++)
    d->h[k] = kind == 0 ? (unsigned short) (t->h[k] << sa)
            : kind == 1 ? (unsigned short) (t->h[k] >> sa) : (unsigned short) (t->sh[k] >> sa);
}
static __inline__ void __gt4_psw (__gt4_reg *d, const __gt4_reg *t, int sa, int kind)
{
  int k;
  for (k = 0; k < 4; k++)
    d->w[k] = kind == 0 ? t->w[k] << sa : kind == 1 ? t->w[k] >> sa : (unsigned int) (t->sw[k] >> sa);
}

/* pexew/pexcw...: the word exchanges. */
static __inline__ void __gt4_pexew (__gt4_reg *d, const __gt4_reg *t)
{ __gt4_reg r = *t; r.w[0] = t->w[2]; r.w[2] = t->w[0]; *d = r; }
static __inline__ void __gt4_prot3w (__gt4_reg *d, const __gt4_reg *t)
{ __gt4_reg r = *t; r.w[0] = t->w[1]; r.w[1] = t->w[2]; r.w[2] = t->w[0]; *d = r; }
static __inline__ void __gt4_pexcw (__gt4_reg *d, const __gt4_reg *t)
{ __gt4_reg r = *t; r.w[1] = t->w[2]; r.w[2] = t->w[1]; *d = r; }

static __inline__ int __gt4_lzc32 (int x)
{
  unsigned int u = x < 0 ? ~(unsigned int) x : (unsigned int) x;
  int n = 0;
  while (n < 31 && !(u & (0x40000000u >> n)))
    n++;
  return n;
}
static __inline__ void __gt4_mmi_plzcw (__gt4_reg *d, const __gt4_reg *s)
{ d->w[0] = __gt4_lzc32 (s->sw[0]); d->w[1] = __gt4_lzc32 (s->sw[1]); }

/* Halfword multiplies into HI/LO (pmulth, pmaddh: acc 1) and pmfhl.lh. */
static __inline__ void __gt4_pmulth (__gt4_reg *d, const __gt4_reg *s, const __gt4_reg *t,
                                     __gt4_reg *hi, __gt4_reg *lo, int acc)
{
  /* product k goes to LO.w0, LO.w1, HI.w0, HI.w1, LO.w2, LO.w3, HI.w2, HI.w3; rd gets products
     (or sums) 0, 2, 4, 6 */
  int k;
  for (k = 0; k < 8; k++)
    {
      int p = (int) s->sh[k] * (int) t->sh[k];
      __gt4_reg *r = (k & 2) ? hi : lo;
      int w = (k & 1) + ((k & 4) ? 2 : 0);
      r->sw[w] = acc ? r->sw[w] + p : p;
    }
  d->sw[0] = lo->sw[0]; d->sw[1] = hi->sw[0]; d->sw[2] = lo->sw[2]; d->sw[3] = hi->sw[2];
}
static __inline__ void __gt4_pmfhl_lh (__gt4_reg *d, const __gt4_reg *hi, const __gt4_reg *lo)
{
  __gt4_reg r;
  r.h[0] = lo->h[0]; r.h[1] = lo->h[2]; r.h[2] = hi->h[0]; r.h[3] = hi->h[2];
  r.h[4] = lo->h[4]; r.h[5] = lo->h[6]; r.h[6] = hi->h[4]; r.h[7] = hi->h[6];
  *d = r;
}
/* ---- FPU --------------------------------------------------------------------------------------- */

static __inline__ float __gt4_f_bits (unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static __inline__ unsigned int __gt4_f_uint (float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }
static __inline__ float __gt4_f_sqrt (float x) { float r; __asm__ ("fsqrt" : "=t" (r) : "0" (x)); return r; }
static __inline__ float __gt4_f_abs (float x) { return __gt4_f_bits (__gt4_f_uint (x) & 0x7FFFFFFFu); }
static __inline__ float __gt4_f_min (float a, float b) { return a < b ? a : b; }
static __inline__ float __gt4_f_max (float a, float b) { return a > b ? a : b; }
/* cvt.w.s: the EE rounds toward zero and saturates. */
static __inline__ int __gt4_f_toint (float x)
{
  if (x >= 2147483648.0f)
    return 0x7FFFFFFF;
  if (x <= -2147483648.0f)
    return (int) 0x80000000u;
  if (x != x)
    return 0x7FFFFFFF;
  return (int) x;
}

/* ---- VU0 --------------------------------------------------------------------------------------- */

/* mask: x 1, y 2, z 4, w 8; bc: -1 for the full vector t, else the broadcast field of t. */
#define __GT4_VUOP(NAME, EXPR) \
static __inline__ void NAME (float *d, const float *s, const float *t, int bc, int mask) \
{ \
  float r[4]; int k; \
  for (k = 0; k < 4; k++) \
    if (mask & (1 << k)) \
      { float a = s[k], b = t[bc < 0 ? k : bc]; r[k] = (EXPR); } \
  for (k = 0; k < 4; k++) \
    if (mask & (1 << k)) \
      d[k] = r[k]; \
}
__GT4_VUOP (__gt4_vadd, a + b)
__GT4_VUOP (__gt4_vsub, a - b)
__GT4_VUOP (__gt4_vmul, a * b)
/* MAX/MINI compare as the VU does, as sign-magnitude integers: the VU has no NaN, so 0x7FFFFFFF
 * (the largest value, which the game uses as "no limit") is bigger than every float. */
static __inline__ int __gt4_vu_order (float f)
{
  union { float f; int i; } u;
  u.f = f;
  return u.i ^ ((u.i >> 31) & 0x7FFFFFFF);
}
__GT4_VUOP (__gt4_vmax, __gt4_vu_order (a) > __gt4_vu_order (b) ? a : b)
__GT4_VUOP (__gt4_vmini, __gt4_vu_order (a) < __gt4_vu_order (b) ? a : b)
__GT4_VUOP (__gt4_vmadd, __gt4_vu0.acc[k] + a * b)
__GT4_VUOP (__gt4_vmsub, __gt4_vu0.acc[k] - a * b)

/* One-source operations: op 0 move, 1 abs, 2-5 ftoi0/4/12/15, 6-9 itof0/4/12/15, 10 mr32. */
static __inline__ void __gt4_vmove (float *d, const float *s, int mask, int op)
{
  /* no static table: gcc 2.9 emits an inline function's static data in every object */
  int fb = op >= 6 ? op - 6 : op - 2;
  float scale = fb == 0 ? 1.0f : fb == 1 ? 16.0f : fb == 2 ? 4096.0f : 32768.0f;
  float r[4]; int k;
  for (k = 0; k < 4; k++)
    if (mask & (1 << k))
      {
        float a = op == 10 ? s[(k + 1) & 3] : s[k];
        if (op == 1)
          a = __gt4_f_abs (a);
        else if (op >= 2 && op <= 5)
          a = __gt4_f_bits ((unsigned int) __gt4_f_toint (a * scale));
        else if (op >= 6 && op <= 9)
          a = (float) (int) __gt4_f_uint (a) / scale;
        r[k] = a;
      }
  for (k = 0; k < 4; k++)
    if (mask & (1 << k))
      d[k] = r[k];
}

/* vopmula (msub 0): ACC.xyz = s.yzx * t.zxy; vopmsub (msub 1): d.xyz = ACC.xyz - s.yzx * t.zxy. */
static __inline__ void __gt4_vopm (float *d, const float *s, const float *t, int msub)
{
  float x = s[1] * t[2], y = s[2] * t[0], z = s[0] * t[1];
  if (msub)
    { x = __gt4_vu0.acc[0] - x; y = __gt4_vu0.acc[1] - y; z = __gt4_vu0.acc[2] - z; }
  d[0] = x; d[1] = y; d[2] = z;
}

static __inline__ void __gt4_vclip (const float *s, float w)
{
  unsigned int f = 0;
  float aw = __gt4_f_abs (w);
  if (s[0] > aw) f |= 1;
  if (s[0] < -aw) f |= 2;
  if (s[1] > aw) f |= 4;
  if (s[1] < -aw) f |= 8;
  if (s[2] > aw) f |= 16;
  if (s[2] < -aw) f |= 32;
  __gt4_vu0.clip = ((__gt4_vu0.clip << 6) | f) & 0xFFFFFF;
}

static __inline__ void __gt4_vrnext (void)
{
  unsigned int x = __gt4_vu0.r;
  unsigned int bit = ((x >> 4) ^ (x >> 22)) & 1;
  __gt4_vu0.r = 0x3F800000u | (((x << 1) | bit) & 0x7FFFFFu);
}

static __inline__ void __gt4_vfill (float *d, float v, int mask)
{
  int k;
  for (k = 0; k < 4; k++)
    if (mask & (1 << k))
      d[k] = v;
}

static __inline__ void __gt4_qmtc2 (float *d, const __gt4_reg *s)
{ int k; for (k = 0; k < 4; k++) d[k] = s->f[k]; }
static __inline__ void __gt4_qmfc2 (__gt4_reg *d, const float *s)
{ int k; for (k = 0; k < 4; k++) d->f[k] = s[k]; }

/* cfc2/ctc2: the integer registers (16 bits) and the control registers. */
static __inline__ unsigned int __gt4_cfc2 (int n)
{
  switch (n)
    {
    case 18: return __gt4_vu0.clip;
    case 20: return __gt4_vu0.r;
    case 21: return __gt4_f_uint (__gt4_vu0.i);
    case 22: return __gt4_f_uint (__gt4_vu0.q);
    case 23: return __gt4_f_uint (__gt4_vu0.p);
    default: return n < 16 ? __gt4_vu0.vi[n] & 0xFFFF : __gt4_vu0.vi[n];
    }
}
static __inline__ void __gt4_ctc2 (int n, unsigned int v)
{
  switch (n)
    {
    case 0: return;
    case 18: __gt4_vu0.clip = v & 0xFFFFFF; return;
    case 20: __gt4_vu0.r = 0x3F800000u | (v & 0x7FFFFF); return;
    case 21: __gt4_vu0.i = __gt4_f_bits (v); return;
    case 22: __gt4_vu0.q = __gt4_f_bits (v); return;
    case 23: __gt4_vu0.p = __gt4_f_bits (v); return;
    default: __gt4_vu0.vi[n] = n < 16 ? v & 0xFFFF : v; return;
    }
}

/* VU0 data memory (4 KB at 0x11004000) for vlqi/vsqi/vilwr/viswr: addresses in quadwords. */
#define __GT4_VU0_MEM(qw) ((float *) (0x11004000u + (((unsigned int) (qw) & 0xFF) << 4)))

#endif
