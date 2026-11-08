// R4's device probe (docs/modern/spikes/R4-link-across-fex.md): the game link's ring discipline across the boundary
// between a native host and the i686 game under Wine (on the Odin, Wine on FEX). probe_host makes a file both map;
// probe_game (i686 Windows) opens it by path. Each sends kRecords numbered records to the other through its own ring,
// with the link's 8-byte index accesses and barriers, and checks every record it receives. The indices start just
// below 2^32, so their low words wrap during the run (the case the link's 8-byte fix is about).
#pragma once
#include <cstdint>

namespace probe {

constexpr uint32_t kMagic = 0x34524250;          // "PBR4"
constexpr uint32_t kRingBytes = 1u << 20;         // each ring's data
constexpr uint32_t kRecordBytes = 64;             // fixed: a record never straddles the ring's end
constexpr uint32_t kRecords = 1000000;            // each way: 64 MB, the ring wraps 61 times
constexpr uint64_t kStart = (1ull << 32) - 32ull * kRingBytes;   // below 2^32 by 32 MiB: the low word wraps early on
constexpr uint32_t kFileBytes = 4096 + 2 * kRingBytes;

struct Ring {
    volatile uint64_t head;   // written by the producer
    uint8_t pad0[56];
    volatile uint64_t tail;   // written by the consumer
    uint8_t pad1[56];
};
struct Header {
    uint32_t magic, records;
    volatile uint32_t gameDone, hostDone;   // each side's count of good records received, set when it has them all
    uint8_t pad[48];
    Ring g2h, h2g;                          // at 64 and 192: 8-byte aligned
};

inline uint64_t load_index(const volatile uint64_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void store_index(volatile uint64_t* p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

// One record: its number in the first 8 bytes, then bytes that depend on it.
inline void fill(uint8_t* r, uint64_t n) {
    for (int k = 0; k < 8; ++k) r[k] = static_cast<uint8_t>(n >> (8 * k));
    for (uint32_t k = 8; k < kRecordBytes; ++k) r[k] = static_cast<uint8_t>(n * 31 + k);
}
inline bool check(const uint8_t* r, uint64_t n) {
    uint64_t got = 0;
    for (int k = 0; k < 8; ++k) got |= uint64_t(r[k]) << (8 * k);
    if (got != n) return false;
    for (uint32_t k = 8; k < kRecordBytes; ++k)
        if (r[k] != static_cast<uint8_t>(n * 31 + k)) return false;
    return true;
}

// Both directions at once from one thread: send while there is room, take what has come. `fence` is the side's full
// barrier (MemoryBarrier on Windows, __atomic_thread_fence on POSIX). Returns the records received intact.
template <typename Fence>
uint32_t exchange(Ring& out, uint8_t* outData, Ring& in, const uint8_t* inData, Fence fence, uint32_t* bad) {
    uint32_t sent = 0, got = 0;
    *bad = 0;
    uint8_t rec[kRecordBytes];
    for (uint64_t spins = 0; (sent < kRecords || got < kRecords) && spins < 2000000000ull; ++spins) {
        if (sent < kRecords) {
            const uint64_t head = load_index(&out.head);
            if (head + kRecordBytes - load_index(&out.tail) <= kRingBytes) {
                fill(rec, sent);
                uint8_t* at = outData + (head % kRingBytes);
                for (uint32_t k = 0; k < kRecordBytes; ++k) at[k] = rec[k];
                fence();
                store_index(&out.head, head + kRecordBytes);
                ++sent;
            }
        }
        const uint64_t head = load_index(&in.head);
        fence();
        uint64_t tail = load_index(&in.tail);
        while (tail + kRecordBytes <= head && got < kRecords) {
            if (!check(inData + (tail % kRingBytes), got)) ++*bad;
            ++got;
            tail += kRecordBytes;
        }
        fence();
        store_index(&in.tail, tail);
    }
    return got - *bad;
}

}  // namespace probe
