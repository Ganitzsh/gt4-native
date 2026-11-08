// runtime/vehicle_decode.cpp: a car's bytes, laid out as GT4 keeps them (docs/modern/spikes/R14-vehicle-state.md), give
// the scene::Vehicle record CONTRACTS.md a.4 describes, in the record's world frame (Y up) from the physics' (Z up).
#include "vehicle.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
void put(std::vector<uint8_t>& car, uint32_t at, float v) { std::memcpy(car.data() + at, &v, 4); }

}  // namespace

int main() {
    using namespace gt4::rt;
    namespace scene = gt4::gfx::scene;
    static_assert(sizeof(scene::Vehicle) == 328, "CONTRACTS.md a.4: the padded 328 bytes");
    std::vector<uint8_t> car(car::kCarBytes, 0);
    const uint32_t d = car::kDynamics;
    // the body: position (1, 2, 3); rows forward (1, 0, 0), right (0, 1, 0), up (0, 0, 1) in the physics' Z-up frame
    put(car, d + 0, 1.0f);
    put(car, d + 4, 2.0f);
    put(car, d + 8, 3.0f);
    put(car, d + 0x0C, 1.0f);
    put(car, d + 0x18 + 4, 1.0f);
    put(car, d + 0x24 + 8, 1.0f);
    put(car, d + 0x544, 10.0f);   // world velocity (10, 2, 0.5)
    put(car, d + 0x548, 2.0f);
    put(car, d + 0x54C, 0.5f);
    put(car, d + 0x5C4, 16.72f);  // speed
    put(car, d + 0x57C, 0.38f);   // yaw rate
    put(car, d + 0x4FC, 5000.0f * 6.2831853f / 60.0f);   // engine speed, rad/s
    car[d + 0x520] = 2;           // gear
    for (int w = 0; w < 4; ++w) {
        const uint32_t at = car::kWheels + static_cast<uint32_t>(w) * car::kWheelStride;
        put(car, at + 0x48, w == 3 ? 0.0f : 2000.0f + 100.0f * w);   // load (wheel 3 airborne)
        put(car, at + 0x38, -0.03f * (w + 1));                       // slip ratio
        put(car, at + 0x34, 0.01f * (w + 1));                        // slip angle
        put(car, at + 0x68, -1000.0f - w);                           // lateral force
        put(car, at + 0x70, 400.0f);                                 // drive force
        put(car, at + 0x74, 50.0f);                                  // the other longitudinal force
        car[at + 0x89] = static_cast<uint8_t>(w);                    // surface
        put(car, at + 0x98, 0.1f * (w + 1) - 0.25f);                 // stroke speed (m/s)
        put(car, car::kContacts + static_cast<uint32_t>(w) * 0x34 + 0, 5.0f);   // contact (5, 6, 7)
        put(car, car::kContacts + static_cast<uint32_t>(w) * 0x34 + 4, 6.0f);
        put(car, car::kContacts + static_cast<uint32_t>(w) * 0x34 + 8, 7.0f);
    }
    const scene::Vehicle v = decode_vehicle(car.data(), 3);
    expect(v.prefix.kind == scene::kVehicle && v.prefix.version == scene::kVersion && v.entrant == 3, "prefix, entrant");
    // world = (x, z, -y): the position (1, 3, -2); forward (1, 0, 0); the physics' right (0, 1, 0) -> (0, 0, -1);
    // its up (0, 0, 1) -> (0, 1, 0), the world's Y
    expect(near(v.bodyToWorld[3][0], 1) && near(v.bodyToWorld[3][1], 3) && near(v.bodyToWorld[3][2], -2) &&
               near(v.bodyToWorld[3][3], 1), "position in the world frame");
    expect(near(v.bodyToWorld[0][0], 1) && near(v.bodyToWorld[1][2], -1) && near(v.bodyToWorld[2][1], 1) &&
               near(v.bodyToWorld[0][3], 0) && near(v.bodyToWorld[2][3], 0), "rows in the world frame");
    expect(near(v.velocity[0], 10) && near(v.velocity[1], 0.5f) && near(v.velocity[2], -2) && near(v.speed, 16.72f),
           "velocity, speed");
    expect(near(v.angularVelocity[1], 0.38f) && std::fabs(v.rpm - 5000.0f) < 0.01f && v.gear == 2, "yaw rate, rpm, gear");
    expect(near(v.wheel[0].load, 2000) && near(v.wheel[2].load, 2200) && v.wheel[0].onGround && !v.wheel[3].onGround,
           "loads, on ground");
    expect(near(v.wheel[1].slipRatio, -0.06f) && near(v.wheel[1].slipAngle, 0.02f) && near(v.wheel[2].forceLateral, -1002) &&
               near(v.wheel[0].forceLongitudinal, 450) && v.wheel[2].surface == 2,
           "wheel slip, forces, surface");
    expect(near(v.wheel[0].contact[0], 5) && near(v.wheel[0].contact[1], 7) && near(v.wheel[0].contact[2], -6),
           "contact in the world frame");
    expect(near(v.wheel[0].strokeSpeed, -0.15f) && near(v.wheel[3].strokeSpeed, 0.15f), "stroke speed");
    expect((v.valid & scene::kVehTransform) && (v.valid & scene::kVehWheelSlip) && (v.valid & scene::kVehWheelStroke) &&
               !(v.valid & scene::kVehInputs) &&
               !(v.valid & scene::kVehSteeringTorque),
           "valid: the fields R14 checked, not the inputs or the rack torque");
    if (failures) return 1;
    std::printf("vehicle: all checks passed\n");
    return 0;
}
