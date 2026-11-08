#include "vehicle.h"

#include "guest.h"
#include "kernel.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::rt {

uint32_t vehicle_conductor() {
    using namespace car;
    // Per thread (the pad's thread for the log, the kick's for the records), so neither shares the other's state.
    thread_local uint32_t cond = 0, scanAt = 0, passEnded = 0;
    const auto holds = [](uint32_t c) {
        return c && in_guest_ram(c + kCarsOffset, kCarStride + 8) && guest_ref<uint32_t>(c + kCarsOffset + 4) == c &&
               guest_ref<uint32_t>(c + kCarsOffset + kCarStride + 4) == c;
    };
    if (holds(cond)) return cond;
    cond = 0;
    // (Re)found by the cars' back pointer: a scan of the guest RAM, 1 MiB a call at most (the kick's cost bounded: one
    // call reads 256 Ki words), a whole pass in 32 calls; after a pass that found nothing, the next starts a second
    // later.
    const uint32_t now = kernel_frame_count();
    if (passEnded && now - passEnded < 60) return 0;
    if (scanAt < kGuestSectionStart) scanAt = kGuestSectionStart;
    constexpr uint32_t kChunk = 1u << 20;
    const uint32_t limit = kRamSize - kCarsOffset - 2 * kCarStride;
    const uint32_t end = std::min(scanAt + kChunk, limit);
    for (uint32_t c = scanAt; c < end; c += 4)
        if (guest_ref<uint32_t>(c + kCarsOffset + 4) == c && holds(c)) {
            scanAt = c;   // (found again from here if it moves a little)
            passEnded = 0;
            return cond = c;
        }
    scanAt = end;
    if (end >= limit) {
        scanAt = kGuestSectionStart;
        passEnded = now ? now : 1;
    }
    return 0;
}

bool vehicle_log_on() {
    static const bool on = std::getenv("GT4_VEHICLE_LOG") && *std::getenv("GT4_VEHICLE_LOG");
    return on;
}

int vehicle_records(gfx::scene::Vehicle *out, int max) {
    using namespace car;
    const uint32_t cond = vehicle_conductor();
    if (!cond) return 0;
    uint32_t count = guest_ref<uint32_t>(cond + kCountOffset);
    if (count > kMaxCars) count = kMaxCars;
    int n = 0;
    for (uint32_t i = 0; i < count && n < max; ++i) {
        const uint32_t a = cond + kCarsOffset + i * kCarStride;
        if (!in_guest_ram(a, kCarBytes) || guest_ref<uint32_t>(a + 4) != cond) continue;
        out[n++] = decode_vehicle(static_cast<const uint8_t *>(guest_ptr(a)), static_cast<uint8_t>(i));
    }
    return n;
}

void vehicle_log(const uint8_t pad[18]) {
    using namespace car;
    static std::FILE *log = [] {
        std::FILE *f = vehicle_log_on() ? std::fopen(std::getenv("GT4_VEHICLE_LOG"), "w") : nullptr;
        if (f) std::fprintf(f, "vblank,car,speed_kmh,yaw_rate,load0,load1,load2,load3,slipratio0,slipratio1,slipratio2,"
                               "slipratio3,slipangle0,slipangle1,slipangle2,slipangle3,pad_steer,steer_rad\n");
        return f;
    }();
    if (!log) return;
    gfx::scene::Vehicle cars[kMaxCars];
    const int n = vehicle_records(cars, static_cast<int>(kMaxCars));
    const uint32_t cond = vehicle_conductor();
    for (int k = 0; k < n; ++k) {
        const gfx::scene::Vehicle &v = cars[k];
        std::fprintf(log, "%u,%u,%.2f,%.4f", kernel_frame_count(), v.entrant, v.speed * 3.6f, v.angularVelocity[1]);
        for (const auto &w : v.wheel) std::fprintf(log, ",%.0f", w.load);
        for (const auto &w : v.wheel) std::fprintf(log, ",%.4f", w.slipRatio);
        for (const auto &w : v.wheel) std::fprintf(log, ",%.4f", w.slipAngle);
        // pad 1's left stick X as this read gives it; the steering angle at the wheels (D + 0x468, radians; R14: 0.698
        // at full lock at a standstill)
        const uint32_t a = cond + kCarsOffset + v.entrant * kCarStride;
        std::fprintf(log, ",%u,%.4f\n", pad[4], guest_ref<float>(a + kDynamics + 0x468));
    }
    std::fflush(log);
}
}  // namespace gt4::rt
