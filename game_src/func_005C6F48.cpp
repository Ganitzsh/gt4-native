/* Native correction of src/runtime/unit_005C1CE0/func_005C6F48.cpp (tools/argcheck.py livein --fix --native in gt4-decomp): the original reads $a0, $a1, $a2 on entry and passes on $a2 to 005C80F0 in the registers it got them in; the matched source leaves that implicit (on the EE the registers still hold the values). Here the parameters and arguments are written out, which no longer matches on the EE. */
typedef int s32;
typedef unsigned char u8;
struct Buf005C6F48
{
  u8 b0;
  char pad1[3];
  s32 w4;
  u8 b8;
};
extern void func_005C80F0(s32, s32, u8);
extern void func_005C8288(s32, s32, s32, u8);
void func_005C6F48(s32 arg0, s32 arg1, u8 arg2)
{
  struct Buf005C6F48 buf;
  s32 v0 = arg0;
  s32 s0;
  s32 diff = (arg1 - v0) >> 3;
  buf.b0 = arg2;
  if (diff >= 0x11)
  {
    u8 r;
    func_005C80F0(arg0, v0 + 0x80, arg2);
    r = buf.b0;
    buf.w4 = v0 + 0x80;
    buf.b8 = r;
    func_005C8288(v0 + 0x80, arg1, 0, r);
  }
  else
  {
    func_005C80F0(arg0, arg1, arg2);
  }
}
