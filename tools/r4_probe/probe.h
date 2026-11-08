// R4's device probe (docs/modern/spikes/R4-link-across-fex.md): the game link's ring discipline across the boundary
// between a native host and the i686 game under Wine (on the Odin, Wine on FEX). probe_host makes a file both map;
// probe_game (i686 Windows) opens it by path. Each sends kRecords numbered records to the other through its own ring,
// with the link's 8-byte index accesses and barriers, and checks every record it receives. An index moves by kStep =
// 2^32 + 64 a record (lane D's design): the ring position (the index modulo the ring, a divisor of 2^32) still moves
// 64 bytes, but every update changes both 32-bit words, so every read that overlaps a write is a chance to tear,
// millions a run (the case the link's 8-byte fix is about: there the high word changes every 4 GiB).
#pragma once
#include <cstdint>

namespace probe {

constexpr uint32_t kMagic = 0x34524250;          // "PBR4"
constexpr uint32_t kRingBytes = 1u << 20;         // each ring's data
constexpr uint32_t kRecordBytes = 64;             // fixed: a record never straddles the ring's end
constexpr uint32_t kRecords = 1000000;            // each way: 64 MB, the ring wraps 61 times
constexpr uint64_t kStart = (1ull << 32) - 32ull * kRingBytes;   // only a multiple of the ring now: positions start at 0
constexpr uint64_t kStep = (1ull << 32) + kRecordBytes;           // an index's move per record: both words change
constexpr uint64_t kSlots = kRingBytes / kRecordBytes;            // records a ring holds
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

#ifndef PROBE_TORN
inline uint64_t load_index(const volatile uint64_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void store_index(volatile uint64_t* p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#else
// The control build (-DPROBE_TORN): the index as the game had it before the 8-byte fix, two 32-bit accesses, low word
// first. On a run where this build reports bad records or anomalies and the normal build none, the probe has shown it
// can see the failure it guards against (lane D's suggestion). Every index update changes both words (kStep), so a
// single run is the test.
inline uint64_t load_index(const volatile uint64_t* p) {
    const volatile uint32_t* w = reinterpret_cast<const volatile uint32_t*>(p);
    const uint32_t lo = w[0];
    const uint32_t hi = w[1];
    return (uint64_t(hi) << 32) | lo;
}
inline void store_index(volatile uint64_t* p, uint64_t v) {
    volatile uint32_t* w = reinterpret_cast<volatile uint32_t*>(p);
    w[0] = static_cast<uint32_t>(v);
    w[1] = static_cast<uint32_t>(v >> 32);
}
#endif

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
// barrier (MemoryBarrier on Windows, __atomic_thread_fence on POSIX). Returns the records received intact. `anomalies`
// counts indices that cannot be right (a head behind the last one seen or behind its tail, a head and tail not a whole
// number of records apart, or more than a ring apart): a torn read, ignored for that pass.
template <typename Fence>
uint32_t exchange(Ring& out, uint8_t* outData, Ring& in, const uint8_t* inData, Fence fence, uint32_t* bad,
                  uint32_t* anomalies) {
    uint32_t sent = 0, got = 0;
    uint64_t lastHead = 0;
    *bad = 0;
    *anomalies = 0;
    uint8_t rec[kRecordBytes];
    for (uint64_t spins = 0; (sent < kRecords || got < kRecords) && spins < 2000000000ull; ++spins) {
        if (sent < kRecords) {
            const uint64_t head = load_index(&out.head), tail = load_index(&out.tail);
            if (tail > head || (head - tail) % kStep != 0 || (head - tail) / kStep > kSlots) {
                ++*anomalies;
            } else if ((head - tail) / kStep < kSlots) {
                fill(rec, sent);
                uint8_t* at = outData + (head % kRingBytes);
                for (uint32_t k = 0; k < kRecordBytes; ++k) at[k] = rec[k];
                fence();
                store_index(&out.head, head + kStep);
                ++sent;
            }
        }
        const uint64_t head = load_index(&in.head);
        fence();
        uint64_t tail = load_index(&in.tail);
        if (head < lastHead || head < tail || (head - tail) % kStep != 0 || (head - tail) / kStep > kSlots) {
            ++*anomalies;
            continue;
        }
        lastHead = head;
        while (tail < head && got < kRecords) {
            if (!check(inData + (tail % kRingBytes), got)) ++*bad;
            ++got;
            tail += kStep;
        }
        fence();
        store_index(&in.tail, tail);
    }
    return got - *bad;
}

}  // namespace probe
