#include "vehicle.h"

#include "guest.h"
#include "kernel.h"
#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::rt {

uint32_t vehicle_conductor() {
    using namespace car;
    static uint32_t cond = 0;
    const auto holds = [](uint32_t c) {
        return c && in_guest_ram(c + kCarsOffset, kCarStride + 8) && guest_ref<uint32_t>(c + kCarsOffset + 4) == c &&
               guest_ref<uint32_t>(c + kCarsOffset + kCarStride + 4) == c;
    };
    if (holds(cond)) return cond;
    // (re)found by the cars' back pointer, at most once a second while there is none (a scan of the guest RAM)
    static uint32_t lastScan = 0;
    const uint32_t now = kernel_frame_count();
    if (lastScan && now - lastScan < 60) return cond = 0;
    lastScan = now;
    for (uint32_t c = kGuestSectionStart; c + kCarsOffset + 2 * kCarStride < kRamSize; c += 4)
        if (guest_ref<uint32_t>(c + kCarsOffset + 4) == c && holds(c)) return cond = c;
    return cond = 0;
}

bool vehicle_log_on() {
    static const bool on = std::getenv("GT4_VEHICLE_LOG") && *std::getenv("GT4_VEHICLE_LOG");
    return on;
}

namespace {
uint8_t g_logSteer = 0x80;   // pad 1's left stick X as last read (vehicle_log_pad)
}  // namespace

void vehicle_log_pad(const uint8_t pad[18]) { g_logSteer = pad[4]; }

int vehicle_records(gfx::scene::Vehicle *out, int max) {
    using namespace car;
    const uint32_t cond = vehicle_conductor();
    if (!cond) return 0;
    uint32_t count = guest_ref<uint32_t>(cond + kCountOffset);
    if (count > kMaxCars) count = kMaxCars;
    static std::FILE *log = [] {
        std::FILE *f = vehicle_log_on() ? std::fopen(std::getenv("GT4_VEHICLE_LOG"), "w") : nullptr;
        if (f) std::fprintf(f, "vblank,car,speed_kmh,yaw_rate,load0,load1,load2,load3,slipratio0,slipratio1,slipratio2,"
                               "slipratio3,slipangle0,slipangle1,slipangle2,slipangle3,pad_steer,steer_rad\n");
        return f;
    }();
    int n = 0;
    for (uint32_t i = 0; i < count && n < max; ++i) {
        const uint32_t a = cond + kCarsOffset + i * kCarStride;
        if (!in_guest_ram(a, kCarBytes) || guest_ref<uint32_t>(a + 4) != cond) continue;
        out[n] = decode_vehicle(static_cast<const uint8_t *>(guest_ptr(a)), static_cast<uint8_t>(i));
        if (log) {
            const gfx::scene::Vehicle &v = out[n];
            std::fprintf(log, "%u,%u,%.2f,%.4f", kernel_frame_count(), i, v.speed * 3.6f, v.angularVelocity[1]);
            for (const auto &w : v.wheel) std::fprintf(log, ",%.0f", w.load);
            for (const auto &w : v.wheel) std::fprintf(log, ",%.4f", w.slipRatio);
            for (const auto &w : v.wheel) std::fprintf(log, ",%.4f", w.slipAngle);
            // the steering angle at the wheels (D + 0x468, radians; R14: 0.698 at full lock at a standstill)
            std::fprintf(log, ",%u,%.4f\n", g_logSteer, guest_ref<float>(a + kDynamics + 0x468));
        }
        ++n;
    }
    if (log) std::fflush(log);
    return n;
}

}  // namespace gt4::rt
