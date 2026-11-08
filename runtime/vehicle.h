#pragma once
// The cars' state from GT4's physics, read-only, for scene::Vehicle records (CONTRACTS.md a.4; ticket D6). Where it is
// kept: docs/modern/spikes/R14-vehicle-state.md. The physics manager (GT HD: DynamicsConductor) holds car i at
// cond + 0x6698 + i * 0x10A0; each car keeps `cond` at +4, which is how the reader finds it.
#include "gt4/gfx.h"

#include <cstdint>

namespace gt4::rt {

// The R14 layout, offsets from the car (A) and its dynamics block (D = A + 0x104).
namespace car {
constexpr uint32_t kCarsOffset = 0x6698, kCarStride = 0x10A0, kCarBytes = 0x10A0, kCountOffset = 0xF8A0;
constexpr uint32_t kMaxCars = 6, kDynamics = 0x104, kWheels = 0x164, kWheelStride = 0xEC, kContacts = 0x7B8;
}  // namespace car

// One car's record from its 0x10A0 bytes (`a`, read-only), with `entrant` its index. The physics frame is Z-up; the
// record's world is the course's object space, Y-up (CONTRACTS.md a.1): (x, y, z) physics -> (x, z, -y). Only the
// fields R14 checked are filled; `valid` says which.
gfx::scene::Vehicle decode_vehicle(const uint8_t *a, uint8_t entrant);

// The conductor's guest address, 0 when no race runs (found by the cars' back pointer, kept while it holds). While
// there is none, each call scans 256 KiB of the guest RAM at most, and a pass that found nothing waits a second. The
// state is per thread.
uint32_t vehicle_conductor();

// The conductor while a race's physics runs (some car has weight on a wheel), else 0. The conductor alone is not a
// race: GT4 keeps one with its cars through the menus, from the boot on, every load 0.
uint32_t vehicle_live();

// Each running car's record (at most `max`), by decode_vehicle, in entrant order; 0 outside a race. Read-only on the
// guest. The scene's Vehicle records (runtime/frame.cpp send_frame_end, at the kick, with scene records on).
int vehicle_records(gfx::scene::Vehicle *out, int max);

// GT4_VEHICLE_LOG=PATH (debugging, the D6 and D12 plots): a CSV line per running car (VBlank, speed, yaw rate, each
// wheel's load, slip ratio and slip angle, `pad`'s left stick X, the steering angle at the wheels). Called with pad
// 1's 18 bytes as the game reads them, once a VBlank.
void vehicle_log(const uint8_t pad[18]);

// Whether GT4_VEHICLE_LOG is set.
bool vehicle_log_on();
}  // namespace gt4::rt
