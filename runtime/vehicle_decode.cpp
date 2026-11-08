// scene::Vehicle from a car's bytes (runtime/vehicle.h). No guest memory or kernel here, so tests/vehicle.cpp builds
// it on the host.
#include "vehicle.h"

#include <cstring>

namespace gt4::rt {

namespace {

float f32(const uint8_t *p, uint32_t at) {
    float v;
    std::memcpy(&v, p + at, 4);
    return v;
}
// physics (x, y, z), Z up -> the record's world (x, z, -y), Y up
void to_world(const float p[3], float out[3]) {
    out[0] = p[0];
    out[1] = p[2];
    out[2] = -p[1];
}

}  // namespace

gfx::scene::Vehicle decode_vehicle(const uint8_t *a, uint8_t entrant) {
    namespace scene = gfx::scene;
    using namespace car;
    scene::Vehicle v{};
    v.prefix = {scene::kVehicle, scene::kVersion};
    v.entrant = entrant;
    const uint32_t d = kDynamics;
    // the body: its rows (forward, right, up) and position, each axis and the position taken to the world frame
    float row[3], pos[3];
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) row[k] = f32(a, d + 0x0C + r * 12 + k * 4);
        to_world(row, v.bodyToWorld[r]);
    }
    for (int k = 0; k < 3; ++k) pos[k] = f32(a, d + k * 4);
    to_world(pos, v.bodyToWorld[3]);
    v.bodyToWorld[3][3] = 1.0f;
    float vel[3];
    for (int k = 0; k < 3; ++k) vel[k] = f32(a, d + 0x544 + k * 4);
    to_world(vel, v.velocity);
    v.speed = f32(a, d + 0x5C4);
    v.angularVelocity[1] = f32(a, d + 0x57C);   // the yaw rate, about the world's up (pitch and roll rates: open)
    v.rpm = f32(a, d + 0x4FC) * 60.0f / 6.2831853f;
    v.gear = a[d + 0x520];
    v.valid = scene::kVehTransform | scene::kVehVelocity | scene::kVehEngine | scene::kVehWheelContact |
              scene::kVehWheelForces | scene::kVehWheelSlip;
    for (int w = 0; w < 4; ++w) {
        const uint32_t at = kWheels + static_cast<uint32_t>(w) * kWheelStride;
        scene::Wheel &out = v.wheel[w];
        out.load = f32(a, at + 0x48);
        out.slipRatio = f32(a, at + 0x38);
        out.slipAngle = f32(a, at + 0x34);
        out.forceLateral = f32(a, at + 0x68);
        out.forceLongitudinal = f32(a, at + 0x70) + f32(a, at + 0x74);
        out.surface = a[at + 0x89];
        out.onGround = out.load > 0.0f ? 1 : 0;
        float contact[3];
        for (int k = 0; k < 3; ++k) contact[k] = f32(a, kContacts + static_cast<uint32_t>(w) * 0x34 + k * 4);
        to_world(contact, out.contact);
    }
    return v;
}

}  // namespace gt4::rt
