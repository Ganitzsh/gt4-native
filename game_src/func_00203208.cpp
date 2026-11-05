/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00203208 (matched; MColorReader::virtual_03): reads a colour of the menus' layout files, either
 * bytes ("color", r g b [a], / 255) or floats. Not the original's source, the original's behaviour: each byte / 255.0f
 * rounds to nearest on the EE and rounded toward zero natively, so every menu colour read here came back one lower per
 * channel through a colour object (00202CE8). Same table as 00202C60's copy. */
typedef int s32;
typedef unsigned int u32;
struct Rep { int len, cap, ref, sel; };
struct Str { char *p; char pad[12]; };
struct Info { const char *name; };
struct Holder { char pad[4]; float *out; };
extern Rep D_00659FA8;
extern char D_006972B0[], D_006972B8[], D_006972C0[];
extern "C" int func_0020EEA0(void *, Str *);
extern "C" void func_0020FC58(void *, int);
extern "C" void func_00326798(void *, int, int, const char *);
extern "C" int func_0057F260(const char *) throw();
extern "C" double func_005A2F70(const char *);
extern "C" int func_005A2F88(const char *) throw();
extern "C" void *func_005A48D8(void *, int, unsigned int);
extern "C" Info *func_005C11A8();
extern "C" char *func_005C2560(Rep *);
extern "C" int func_005CD320(void *, const char *, unsigned int, unsigned int);
static inline int clamp(int x) { if (x < 0) x = 0; if (x >= 256) x = 255; return x; }
/* EE division by 255, to nearest: the bits of k / 255.0f as the EE's FPU divides (PCSX2's default division rounding,
 * "nearest"), for k = 0..255. Natively the x87 unit rounds every result toward zero (runtime set_guest_fpu: the EE's
 * add and multiply), so k / 255.0f came out one ulp low, and k * 255 back (00202CE8, toward zero) gave k - 1 for every
 * k in 1..254: each colour byte lost one going through a colour object. Bits, not literals: the toolchain rounds
 * float literals toward zero (game_cc.py ee_float_literal). */
static const union {
    unsigned int u[256];
    float f[256];
} kEeInv255 = {{
    0x00000000, 0x3B808081, 0x3C008081, 0x3C40C0C1, 0x3C808081, 0x3CA0A0A1, 0x3CC0C0C1, 0x3CE0E0E1,
    0x3D008081, 0x3D109091, 0x3D20A0A1, 0x3D30B0B1, 0x3D40C0C1, 0x3D50D0D1, 0x3D60E0E1, 0x3D70F0F1,
    0x3D808081, 0x3D888889, 0x3D909091, 0x3D989899, 0x3DA0A0A1, 0x3DA8A8A9, 0x3DB0B0B1, 0x3DB8B8B9,
    0x3DC0C0C1, 0x3DC8C8C9, 0x3DD0D0D1, 0x3DD8D8D9, 0x3DE0E0E1, 0x3DE8E8E9, 0x3DF0F0F1, 0x3DF8F8F9,
    0x3E008081, 0x3E048485, 0x3E088889, 0x3E0C8C8D, 0x3E109091, 0x3E149495, 0x3E189899, 0x3E1C9C9D,
    0x3E20A0A1, 0x3E24A4A5, 0x3E28A8A9, 0x3E2CACAD, 0x3E30B0B1, 0x3E34B4B5, 0x3E38B8B9, 0x3E3CBCBD,
    0x3E40C0C1, 0x3E44C4C5, 0x3E48C8C9, 0x3E4CCCCD, 0x3E50D0D1, 0x3E54D4D5, 0x3E58D8D9, 0x3E5CDCDD,
    0x3E60E0E1, 0x3E64E4E5, 0x3E68E8E9, 0x3E6CECED, 0x3E70F0F1, 0x3E74F4F5, 0x3E78F8F9, 0x3E7CFCFD,
    0x3E808081, 0x3E828283, 0x3E848485, 0x3E868687, 0x3E888889, 0x3E8A8A8B, 0x3E8C8C8D, 0x3E8E8E8F,
    0x3E909091, 0x3E929293, 0x3E949495, 0x3E969697, 0x3E989899, 0x3E9A9A9B, 0x3E9C9C9D, 0x3E9E9E9F,
    0x3EA0A0A1, 0x3EA2A2A3, 0x3EA4A4A5, 0x3EA6A6A7, 0x3EA8A8A9, 0x3EAAAAAB, 0x3EACACAD, 0x3EAEAEAF,
    0x3EB0B0B1, 0x3EB2B2B3, 0x3EB4B4B5, 0x3EB6B6B7, 0x3EB8B8B9, 0x3EBABABB, 0x3EBCBCBD, 0x3EBEBEBF,
    0x3EC0C0C1, 0x3EC2C2C3, 0x3EC4C4C5, 0x3EC6C6C7, 0x3EC8C8C9, 0x3ECACACB, 0x3ECCCCCD, 0x3ECECECF,
    0x3ED0D0D1, 0x3ED2D2D3, 0x3ED4D4D5, 0x3ED6D6D7, 0x3ED8D8D9, 0x3EDADADB, 0x3EDCDCDD, 0x3EDEDEDF,
    0x3EE0E0E1, 0x3EE2E2E3, 0x3EE4E4E5, 0x3EE6E6E7, 0x3EE8E8E9, 0x3EEAEAEB, 0x3EECECED, 0x3EEEEEEF,
    0x3EF0F0F1, 0x3EF2F2F3, 0x3EF4F4F5, 0x3EF6F6F7, 0x3EF8F8F9, 0x3EFAFAFB, 0x3EFCFCFD, 0x3EFEFEFF,
    0x3F008081, 0x3F018182, 0x3F028283, 0x3F038384, 0x3F048485, 0x3F058586, 0x3F068687, 0x3F078788,
    0x3F088889, 0x3F09898A, 0x3F0A8A8B, 0x3F0B8B8C, 0x3F0C8C8D, 0x3F0D8D8E, 0x3F0E8E8F, 0x3F0F8F90,
    0x3F109091, 0x3F119192, 0x3F129293, 0x3F139394, 0x3F149495, 0x3F159596, 0x3F169697, 0x3F179798,
    0x3F189899, 0x3F19999A, 0x3F1A9A9B, 0x3F1B9B9C, 0x3F1C9C9D, 0x3F1D9D9E, 0x3F1E9E9F, 0x3F1F9FA0,
    0x3F20A0A1, 0x3F21A1A2, 0x3F22A2A3, 0x3F23A3A4, 0x3F24A4A5, 0x3F25A5A6, 0x3F26A6A7, 0x3F27A7A8,
    0x3F28A8A9, 0x3F29A9AA, 0x3F2AAAAB, 0x3F2BABAC, 0x3F2CACAD, 0x3F2DADAE, 0x3F2EAEAF, 0x3F2FAFB0,
    0x3F30B0B1, 0x3F31B1B2, 0x3F32B2B3, 0x3F33B3B4, 0x3F34B4B5, 0x3F35B5B6, 0x3F36B6B7, 0x3F37B7B8,
    0x3F38B8B9, 0x3F39B9BA, 0x3F3ABABB, 0x3F3BBBBC, 0x3F3CBCBD, 0x3F3DBDBE, 0x3F3EBEBF, 0x3F3FBFC0,
    0x3F40C0C1, 0x3F41C1C2, 0x3F42C2C3, 0x3F43C3C4, 0x3F44C4C5, 0x3F45C5C6, 0x3F46C6C7, 0x3F47C7C8,
    0x3F48C8C9, 0x3F49C9CA, 0x3F4ACACB, 0x3F4BCBCC, 0x3F4CCCCD, 0x3F4DCDCE, 0x3F4ECECF, 0x3F4FCFD0,
    0x3F50D0D1, 0x3F51D1D2, 0x3F52D2D3, 0x3F53D3D4, 0x3F54D4D5, 0x3F55D5D6, 0x3F56D6D7, 0x3F57D7D8,
    0x3F58D8D9, 0x3F59D9DA, 0x3F5ADADB, 0x3F5BDBDC, 0x3F5CDCDD, 0x3F5DDDDE, 0x3F5EDEDF, 0x3F5FDFE0,
    0x3F60E0E1, 0x3F61E1E2, 0x3F62E2E3, 0x3F63E3E4, 0x3F64E4E5, 0x3F65E5E6, 0x3F66E6E7, 0x3F67E7E8,
    0x3F68E8E9, 0x3F69E9EA, 0x3F6AEAEB, 0x3F6BEBEC, 0x3F6CECED, 0x3F6DEDEE, 0x3F6EEEEF, 0x3F6FEFF0,
    0x3F70F0F1, 0x3F71F1F2, 0x3F72F2F3, 0x3F73F3F4, 0x3F74F4F5, 0x3F75F5F6, 0x3F76F6F7, 0x3F77F7F8,
    0x3F78F8F9, 0x3F79F9FA, 0x3F7AFAFB, 0x3F7BFBFC, 0x3F7CFCFD, 0x3F7DFDFE, 0x3F7EFEFF, 0x3F800000
}};
extern "C" void func_00203208(Holder *self, void *reader, void *tag) {
 Str s;
 int v[4];
 float f[4];
 if (func_005CD320(tag, D_006972B0, 0, func_0057F260(D_006972B0)) == 0) {
  Rep *r = &D_00659FA8;
  char *d;
  int i, t;
  if (r->sel != 0) d = func_005C2560(r);
  else { d = (char *)(r + 1); r->ref++; }
  s.p = d;
  func_0020EEA0(reader, &s);
  func_005A48D8(v, 0, 16);
  v[3] = 255;
  i = 0;
  while ((t = func_0020EEA0(reader, &s)) != -1 && t != 0x7d) {
   const char *c = D_006972B8;
   if (*(int *)(s.p - 16) != 0) {
    s.p[*(int *)(s.p - 16)] = 0;
    c = s.p;
   }
   v[i++] = func_005A2F88(c);
   if (i >= 4) break;
  }
  if (t != 0x7d) func_0020FC58(reader, 1);
  self->out[0] = kEeInv255.f[clamp(v[0])];
  self->out[1] = kEeInv255.f[clamp(v[1])];
  self->out[2] = kEeInv255.f[clamp(v[2])];
  self->out[3] = kEeInv255.f[clamp(v[3])];
  Rep *q = (Rep *)(s.p - 16);
  if (--q->ref == 0) { int cap = q->cap + 16; func_00326798(q, cap, 4, func_005C11A8()->name); }
 } else if (func_005CD320(tag, D_006972C0, 0, func_0057F260(D_006972C0)) == 0) {
  Rep *r = &D_00659FA8;
  char *d;
  int i, t;
  if (r->sel != 0) d = func_005C2560(r);
  else { d = (char *)(r + 1); r->ref++; }
  s.p = d;
  func_0020EEA0(reader, &s);
  func_005A48D8(f, 0, 16);
  f[3] = 1.0f;
  i = 0;
  while ((t = func_0020EEA0(reader, &s)) != -1 && t != 0x7d) {
   const char *c = D_006972B8;
   if (*(int *)(s.p - 16) != 0) {
    s.p[*(int *)(s.p - 16)] = 0;
    c = s.p;
   }
   f[i++] = (float)func_005A2F70(c);
   if (i >= 4) break;
  }
  if (t != 0x7d) func_0020FC58(reader, 1);
  self->out[0] = f[0];
  self->out[1] = f[1];
  self->out[2] = f[2];
  self->out[3] = f[3];
  Rep *q = (Rep *)(s.p - 16);
  if (--q->ref == 0) { int cap = q->cap + 16; func_00326798(q, cap, 4, func_005C11A8()->name); }
 }
}
