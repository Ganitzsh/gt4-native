#include "gt4/portable/align.hpp"
#include "gt4/portable/range.hpp"
#include "gt4/portable/bits.hpp"
#include "gt4/portable/sign_extend.hpp"
#include "gt4/portable/rotate.hpp"
#include "gt4/portable/byteswap.hpp"
#include "gt4/portable/saturate.hpp"
#include "gt4/portable/clamp.hpp"
#include "gt4/portable/lerp.hpp"
#include "gt4/portable/vec2.hpp"
#include "gt4/portable/vec3.hpp"
#include "gt4/portable/vec4.hpp"
#include "gt4/portable/mat4.hpp"
#include "gt4/portable/color.hpp"
#include "gt4/portable/fixed16.hpp"
#include "gt4/portable/lcg.hpp"
#include "gt4/portable/crc32.hpp"
#include "gt4/portable/fnv1a.hpp"
#include "gt4/portable/adler32.hpp"
#include "gt4/portable/cursor.hpp"
#include "gt4/portable/u16reader.hpp"
#include "gt4/portable/u32reader.hpp"
#include "gt4/portable/u64reader.hpp"
#include "gt4/portable/u32writer.hpp"
#include "gt4/portable/cstring.hpp"
#include "gt4/portable/eucjp_digits.hpp"
#include "gt4/portable/mips_jump.hpp"
#include "gt4/portable/mips_branch.hpp"
#include "gt4/portable/mips_fields.hpp"
#include "gt4/portable/pad_buttons.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
using namespace gt4::portable;
int main() {
    // align
    assert(align_up(5,4)==8);
    // range
    assert(contains_range(10,8,16,2)); assert(!contains_range(10,8,17,2));
    // bits
    assert(extract_bits(0xABCD,4,8)==0xBC);
    // sign_extend
    assert(sign_extend(0xFF,8)==-1);
    // rotate
    assert(rotl32(0x12345678,8)==0x34567812);
    // byteswap
    assert(byteswap32(0x12345678)==0x78563412);
    // saturate
    assert(saturate_u8(-1)==0 && saturate_u8(999)==255);
    // clamp
    assert(clamp_value(8,0,5)==5);
    // lerp
    assert(lerp_f32(2,6,0.5f)==4);
    // vec2
    { auto v=add(Vec2{1,2},Vec2{3,4}); assert(v.x==4 && v.y==6); }
    // vec3
    assert(dot(Vec3{1,2,3},Vec3{4,5,6})==32);
    // vec4
    { auto v=scale(Vec4{1,2,3,4},2); assert(v.w==8); }
    // mat4
    { auto m=identity4(); assert(m.m[0]==1 && m.m[5]==1 && m.m[1]==0); }
    // color
    assert(rgba8(1,2,3,4)==0x04030201);
    // fixed16
    assert(from_fixed16(98304)==1.5f);
    // lcg
    assert(lcg_next(0)==1013904223u);
    // crc32
    { std::array<std::byte,3> b{std::byte{'a'},std::byte{'b'},std::byte{'c'}}; assert(crc32(b)==0x352441C2u); }
    // fnv1a
    { std::array<std::byte,1> b{std::byte{'a'}}; assert(fnv1a(b)==0xE40C292Cu); }
    // adler32
    { std::array<std::byte,1> b{std::byte{'a'}}; assert(adler32(b)==0x00620062u); }
    // cursor
    { std::array<std::byte,1> b{std::byte{7}}; Cursor c(b); assert(c.read8()==7 && c.position()==1); }
    // u16reader
    { std::array<std::byte,2> b{std::byte{0x34},std::byte{0x12}}; assert(u16le(b)==0x1234); }
    // u32reader
    { std::array<std::byte,4> b{std::byte{1},std::byte{2},std::byte{3},std::byte{4}}; assert(u32le(b)==0x04030201); }
    // u64reader
    { std::array<std::byte,8> b{}; b[7]=std::byte{1}; assert(u64le(b)==(1ull<<56)); }
    // u32writer
    { std::array<std::byte,4> b{}; put_u32le(b,0x12345678); assert(b[0]==std::byte{0x78}); }
    // cstring
    { std::array<std::byte,3> b{std::byte{'a'},std::byte{0},std::byte{'b'}}; assert(bounded_cstrlen(b)==1); }
    // eucjp_digits
    assert(eucjp_fullwidth_digit(0xA3,0xB5)==5);
    // mips_jump
    assert(mips_jump_target(0x80000000,0x08000004)==0x80000010);
    // mips_branch
    assert(mips_branch_target(0x1000,-1)==0x1000);
    // mips_fields
    { auto f=decode_mips(0x012A4020); assert(f.opcode==0 && f.rs==9 && f.rt==10 && f.rd==8 && f.funct==32); }
    // pad_buttons
    assert(pad_pressed(0xFFFE,1)); assert(!pad_pressed(0xFFFF,1));
}
