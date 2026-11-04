#include "disc_setup.h"

#include "stb/stb_image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace {

constexpr const char* kCoreSha1 = "30031137ce494323816bb707b90d3ec0d07937d2";  // CORE.GT4 of SCUS-97328 v1.01
constexpr const char* kElfSha1 = "ca47e116503e12b2db95aa7ee7debaaa99870c8b";   // core2elf.py's CORE.GT4.elf
constexpr size_t kSector = 2048;

std::string sha1Hex(const std::vector<uint8_t>& data) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(data);
    const uint64_t bits = uint64_t(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (i * 8)));
    const auto rotl = [](uint32_t v, int c) { return (v << c) | (v >> (32 - c)); };
    for (size_t block = 0; block < m.size(); block += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(m[block + i * 4]) << 24 | uint32_t(m[block + i * 4 + 1]) << 16 |
                   uint32_t(m[block + i * 4 + 2]) << 8 | m[block + i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
            else f = b ^ c ^ d, k = 0xCA62C1D6u;
            const uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rotl(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    char out[41];
    for (int i = 0; i < 5; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 40);
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAt(std::ifstream& f, uint64_t offset, uint8_t* out, size_t size) {
    f.clear();
    f.seekg(std::streamoff(offset));
    f.read(reinterpret_cast<char*>(out), std::streamsize(size));
    return size_t(f.gcount()) == size;
}

// CORE.GT4 in the image's root directory (ISO 9660): its bytes, or nothing.
std::optional<std::vector<uint8_t>> readCore(std::ifstream& f) {
    std::array<uint8_t, kSector> s{};
    if (!readAt(f, 16 * kSector, s.data(), kSector) || s[0] != 1 || std::memcmp(s.data() + 1, "CD001", 5) != 0)
        return std::nullopt;
    const uint32_t rootLba = le32(s.data() + 156 + 2), rootSize = le32(s.data() + 156 + 10);
    for (uint32_t sector = 0; sector * kSector < rootSize; ++sector) {
        if (!readAt(f, uint64_t(rootLba + sector) * kSector, s.data(), kSector)) return std::nullopt;
        for (size_t pos = 0; pos + 33 < kSector && s[pos] != 0; pos += s[pos]) {
            const uint8_t* r = s.data() + pos;
            std::string name(reinterpret_cast<const char*>(r + 33), r[32]);
            name = name.substr(0, name.find(';'));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            if (name != "CORE.GT4" || (r[25] & 2)) continue;
            std::vector<uint8_t> data(le32(r + 10));
            if (!readAt(f, uint64_t(le32(r + 2)) * kSector, data.data(), data.size())) return std::nullopt;
            return data;
        }
    }
    return std::nullopt;
}

struct Section {
    uint32_t addr;
    std::vector<uint8_t> data;
};

void put16(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v)), o.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, v), put16(o, v >> 16); }

// tools/core2elf.py's build_elf, byte for byte: one PT_LOAD per section, .text for the one holding the entry point.
std::vector<uint8_t> buildElf(uint32_t entry, const std::vector<Section>& sections) {
    const uint32_t ehsize = 52, phentsize = 32, shentsize = 40, phoff = ehsize, n = uint32_t(sections.size());
    uint32_t offset = (phoff + phentsize * n + 0xFF) & ~0xFFu;
    std::vector<uint8_t> phdrs;
    std::vector<uint32_t> offsets;
    std::vector<std::string> names;
    for (const auto& s : sections) {
        const uint32_t size = uint32_t(s.data.size());
        for (uint32_t v : {1u, offset, s.addr, s.addr, size, size, 7u, 0x10u}) put32(phdrs, v);
        offsets.push_back(offset);
        const bool code = s.addr <= entry && entry < s.addr + size;
        const bool haveData = std::find(names.begin(), names.end(), ".data") != names.end();
        names.push_back(code ? ".text" : !haveData ? ".data" : ".data" + std::to_string(names.size()));
        offset = (offset + size + 0xF) & ~0xFu;
    }
    std::vector<uint8_t> shstrtab{0};
    for (const auto& name : names) shstrtab.insert(shstrtab.end(), name.begin(), name.end()), shstrtab.push_back(0);
    for (char c : std::string(".shstrtab")) shstrtab.push_back(uint8_t(c));
    shstrtab.push_back(0);
    const uint32_t shstrOff = offset, shoff = (shstrOff + uint32_t(shstrtab.size()) + 3) & ~3u;
    std::vector<uint8_t> shdrs(shentsize, 0);
    uint32_t namePos = 1;
    for (size_t i = 0; i < sections.size(); ++i) {
        const uint32_t flags = names[i] == ".text" ? 0x6 : 0x3;
        for (uint32_t v : {namePos, 1u, flags, sections[i].addr, offsets[i], uint32_t(sections[i].data.size()), 0u, 0u, 16u, 0u})
            put32(shdrs, v);
        namePos += uint32_t(names[i].size()) + 1;
    }
    for (uint32_t v : {namePos, 3u, 0u, 0u, shstrOff, uint32_t(shstrtab.size()), 0u, 0u, 1u, 0u}) put32(shdrs, v);

    std::vector<uint8_t> out{0x7F, 'E', 'L', 'F', 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    put16(out, 2), put16(out, 8), put32(out, 1), put32(out, entry), put32(out, phoff), put32(out, shoff);
    put32(out, 0x20924001u);
    for (uint32_t v : {ehsize, phentsize, n, shentsize, n + 2, n + 1}) put16(out, v);
    out.insert(out.end(), phdrs.begin(), phdrs.end());
    for (size_t i = 0; i < sections.size(); ++i) {
        out.resize(offsets[i], 0);
        out.insert(out.end(), sections[i].data.begin(), sections[i].data.end());
    }
    out.resize(shstrOff, 0);
    out.insert(out.end(), shstrtab.begin(), shstrtab.end());
    out.resize(shoff, 0);
    out.insert(out.end(), shdrs.begin(), shdrs.end());
    return out;
}

}  // namespace

std::string makeCoreElf(const std::filesystem::path& iso, const std::filesystem::path& elf) {
    std::ifstream f(iso, std::ios::binary);
    if (!f) return "Cannot open the disc image " + iso.filename().string() + ".";
    const auto core = readCore(f);
    if (!core)
        return iso.filename().string() + " is not a Gran Turismo 4 disc image (no CORE.GT4 in it). "
               "Make the image from your own disc as a plain .iso.";
    const std::string coreSha1 = sha1Hex(*core);
    if (coreSha1 != kCoreSha1)
        return iso.filename().string() + " is not the supported release. GT4 Native runs Gran Turismo 4 (USA), "
               "SCUS-97328, version 1.01 only (its CORE.GT4 has SHA-1 " + std::string(kCoreSha1) + "; this one has " +
               coreSha1 + ").";

    // u16 boot flags, u32 size, raw deflate; then two hashes, the section count, the entry and the sections.
    const std::vector<uint8_t>& raw = *core;
    const uint32_t size = le32(raw.data() + 2);
    std::vector<uint8_t> data(size);
    const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(data.data()), int(size),
                                                     reinterpret_cast<const char*>(raw.data() + 6), int(raw.size() - 6));
    if (got != int(size)) return "CORE.GT4 did not decompress.";
    size_t pos = 0;
    for (int i = 0; i < 2; ++i) pos += 2 + (data[pos] | data[pos + 1] << 8);
    const uint32_t count = le32(data.data() + pos), entry = le32(data.data() + pos + 4);
    pos += 8;
    std::vector<Section> sections;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t addr = le32(data.data() + pos), length = le32(data.data() + pos + 4);
        pos += 8;
        sections.push_back({addr, std::vector<uint8_t>(data.begin() + pos, data.begin() + pos + length)});
        pos += length;
    }
    // Sections whose bytes sit, identical, inside a larger one (the last 24 bytes of the code, stored twice) are left out.
    std::vector<Section> loaded;
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& s = sections[i];
        bool inside = false;
        for (size_t j = 0; j < sections.size() && !inside; ++j) {
            const auto& o = sections[j];
            inside = j != i && o.addr <= s.addr && s.addr + s.data.size() <= o.addr + o.data.size() &&
                     o.data.size() > s.data.size() &&
                     std::equal(s.data.begin(), s.data.end(), o.data.begin() + (s.addr - o.addr));
        }
        if (!inside) loaded.push_back(s);
    }
    const std::vector<uint8_t> out = buildElf(entry, loaded);
    if (sha1Hex(out) != kElfSha1) return "CORE.GT4.elf came out different from the expected one; please report this.";

    std::error_code error;
    std::filesystem::create_directories(elf.parent_path(), error);
    const std::filesystem::path part = elf.string() + ".part";
    {
        std::ofstream o(part, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        if (!o) return "Cannot write " + elf.string() + ".";
    }
    std::filesystem::rename(part, elf, error);
    if (error) return "Cannot write " + elf.string() + ": " + error.message();
    return {};
}
