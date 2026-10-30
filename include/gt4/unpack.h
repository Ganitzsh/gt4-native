// GT4's compact batch data formats: how elements of 1 to 4 components of 32, 16, 8 bits (or the 5-5-5-1 colour
// format) fill the quadwords of a 3D batch, as the PS2's VIF UNPACK did. GT4's model files hold their vertex data
// in these formats (decoded by the host, platform/gs/vif1.cpp), and the game's own batch writers use them
// (runtime/native/pgl.cpp gt4_gfx_batch_unpack). Both use this one definition.
//
// `cmd` is the format byte (0x60 | vn << 2 | vl, + 0x10 when masked): vn + 1 components, 32 >> vl bits each (V4-5
// when vn = vl = 3). Elements fill quadwords in cycles: `cl` of every `wl` quadwords take data; with cl < wl the
// others are written from the mask's sources only, with cl > wl the extra elements' quadwords are skipped. A
// masked format takes per lane (two bits per lane, eight per cycle row) the data (0), the row (1), the column (2)
// or nothing (3); `mode` 1 adds the row to the data, 2 also keeps the sum as the new row, 3 keeps the data as the
// row. Components past the end of the data read 0 (a V3 element's w lane takes the next element's first
// component).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gt4::gfx {

// Bytes of one element: (32 >> vl) * (vn + 1) / 8; V4-5 is 2.
inline uint32_t unpackElementBytes(uint32_t cmd) {
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    if (vn == 3 && vl == 3) return 2;
    return ((32u >> vl) * (vn + 1)) / 8;
}

// The 32-bit words of data `num` elements (1..256) take with the cycle (cl, wl).
inline uint32_t unpackWords(uint32_t cmd, uint32_t num, uint32_t cl, uint32_t wl) {
    const uint32_t writeLength = wl ? wl : 256;
    uint32_t elements = num;
    if (cl < writeLength) elements = cl * (num / writeLength) + std::min(num % writeLength, cl);
    return (elements * unpackElementBytes(cmd) + 3) / 4;
}

// Expands `num` elements into quadwords: write(offset, value[4], lanes) for each quadword written, `offset` from
// the first, `lanes` the lanes that take a value (bit k: lane k).
template <class Write>
void unpackExpand(uint32_t cmd, uint32_t num, bool usn, uint32_t cl, uint32_t wl, uint32_t mask, uint32_t mode, uint32_t row[4],
                  const uint32_t col[4], const uint8_t* bytes, size_t dataBytes, Write&& write) {
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    const bool masked = (cmd >> 4) & 1;
    const uint32_t writeLength = wl ? wl : 256;
    const bool fill = cl < writeLength;
    const uint32_t bytesPerElement = unpackElementBytes(cmd);
    const bool v45 = vn == 3 && vl == 3;
    const uint32_t componentBytes = v45 ? 2 : (4u >> vl);
    auto component = [&](size_t offset) -> uint32_t {
        if (offset + componentBytes > dataBytes) return 0;
        switch (componentBytes) {
        case 4: { uint32_t v; std::memcpy(&v, bytes + offset, 4); return v; }
        case 2: {
            uint16_t v; std::memcpy(&v, bytes + offset, 2);
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
        }
        default: {
            const uint8_t v = bytes[offset];
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
        }
        }
    };
    size_t dataOffset = 0;
    uint32_t cycle = 0, offset = 0;
    for (uint32_t n = 0; n < num; ++n) {
        uint32_t value[4];
        if (v45) {
            uint16_t v = 0;
            if (dataOffset + 2 <= dataBytes) std::memcpy(&v, bytes + dataOffset, 2);
            value[0] = (v & 0x001Fu) << 3;
            value[1] = (v & 0x03E0u) >> 2;
            value[2] = (v & 0x7C00u) >> 7;
            value[3] = (v & 0x8000u) >> 8;
        } else if (vn == 0) {
            value[0] = value[1] = value[2] = value[3] = component(dataOffset);
        } else if (vn == 1) {
            value[0] = value[2] = component(dataOffset);
            value[1] = value[3] = component(dataOffset + componentBytes);
        } else {   // V3 and V4 both read four components
            for (uint32_t c = 0; c < 4; ++c) value[c] = component(dataOffset + c * componentBytes);
        }
        const uint32_t maskCycle = std::min(cycle, 3u);
        uint32_t out[4] = {};
        unsigned lanes = 0;
        for (uint32_t f = 0; f < 4; ++f) {
            const uint32_t m = masked ? (mask >> (maskCycle * 8 + f * 2)) & 3 : 0;
            switch (m) {
            case 0:
                switch (v45 ? 0 : mode) {
                case 1: out[f] = value[f] + row[f]; break;
                case 2: row[f] += value[f]; out[f] = row[f]; break;
                case 3: row[f] = value[f]; out[f] = value[f]; break;
                default: out[f] = value[f]; break;
                }
                lanes |= 1u << f;
                break;
            case 1: out[f] = row[f]; lanes |= 1u << f; break;
            case 2: out[f] = col[maskCycle]; lanes |= 1u << f; break;
            default: break;   // write protected
            }
        }
        write(offset, out, lanes);
        ++offset;
        ++cycle;
        if (fill) {
            if (cycle <= cl) dataOffset += bytesPerElement;
            else if (cycle == writeLength) cycle = 0;
        } else {
            dataOffset += bytesPerElement;
            if (cycle >= writeLength) {
                offset += cl - writeLength;
                cycle = 0;
            }
        }
    }
}

}  // namespace gt4::gfx
