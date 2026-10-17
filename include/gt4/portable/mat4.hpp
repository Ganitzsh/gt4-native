#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
struct Mat4 { float m[16]; }; inline Mat4 identity4() { Mat4 r{}; for(int i=0;i<4;++i) r.m[i*4+i]=1; return r; }
}
