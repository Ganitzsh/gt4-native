// The scene records' layouts (docs/modern/CONTRACTS.md (a)): include/gt4/gfx.h namespace gt4::gfx::scene against the
// game's plain C copy compat/game/gt4_scene.h, every size and every member's offset, so both compilers (gcc 2.9 i386,
// MSVC / clang x64) agree.
#include "gt4/gfx.h"

#include "gt4_scene.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}
}  // namespace

namespace scene = gt4::gfx::scene;

// sizeof and offsetof of one member in both: CPP struct, C struct, CPP member, C member
#define SAME_SIZE(cpp, c) check(sizeof(scene::cpp) == sizeof(struct c), #cpp " size: " + std::to_string(sizeof(scene::cpp)) + \
                                " vs " + std::to_string(sizeof(struct c)))
#define SAME_AT(cpp, c, m, mc) check(offsetof(scene::cpp, m) == offsetof(struct c, mc), #cpp "::" #m " offset: " + \
                                     std::to_string(offsetof(scene::cpp, m)) + " vs " + std::to_string(offsetof(struct c, mc)))

namespace {
void layouts() {
    check(scene::kVersion == GT4_SCENE_VERSION, "version");
    check(scene::kFrameBegin == GT4_SCENE_FRAME_BEGIN && scene::kFrameEnd == GT4_SCENE_FRAME_END &&
              scene::kViewBegin == GT4_SCENE_VIEW_BEGIN && scene::kViewEnd == GT4_SCENE_VIEW_END &&
              scene::kInstance == GT4_SCENE_INSTANCE && scene::kLight == GT4_SCENE_LIGHT && scene::kEmitter == GT4_SCENE_EMITTER &&
              scene::kMarker == GT4_SCENE_MARKER && scene::kVehicle == GT4_SCENE_VEHICLE && scene::kProjection == GT4_SCENE_PROJECTION,
          "kinds");

    SAME_SIZE(Prefix, gt4_scene_prefix);
    SAME_AT(Prefix, gt4_scene_prefix, kind, kind);
    SAME_AT(Prefix, gt4_scene_prefix, version, version);

    SAME_SIZE(FrameBegin, gt4_scene_frame_begin);
    SAME_AT(FrameBegin, gt4_scene_frame_begin, frame, frame);
    SAME_AT(FrameBegin, gt4_scene_frame_begin, vblank, vblank);
    SAME_AT(FrameBegin, gt4_scene_frame_begin, flags, flags);

    SAME_SIZE(FrameEnd, gt4_scene_frame_end);
    SAME_AT(FrameEnd, gt4_scene_frame_end, frame, frame);
    SAME_AT(FrameEnd, gt4_scene_frame_end, views, views);
    SAME_AT(FrameEnd, gt4_scene_frame_end, instances, instances);
    SAME_AT(FrameEnd, gt4_scene_frame_end, lights, lights);
    SAME_AT(FrameEnd, gt4_scene_frame_end, emitters, emitters);
    SAME_AT(FrameEnd, gt4_scene_frame_end, projections, projections);
    SAME_AT(FrameEnd, gt4_scene_frame_end, reserved, reserved);

    SAME_SIZE(ViewBegin, gt4_scene_view_begin);
    SAME_AT(ViewBegin, gt4_scene_view_begin, view, view);
    SAME_AT(ViewBegin, gt4_scene_view_begin, role, role);
    SAME_AT(ViewBegin, gt4_scene_view_begin, flags, flags);
    SAME_AT(ViewBegin, gt4_scene_view_begin, pass, pass);
    SAME_AT(ViewBegin, gt4_scene_view_begin, projection, projection);
    SAME_AT(ViewBegin, gt4_scene_view_begin, reserved0, reserved0);
    SAME_AT(ViewBegin, gt4_scene_view_begin, worldToView, world_to_view);
    SAME_AT(ViewBegin, gt4_scene_view_begin, scissor, scissor);

    SAME_SIZE(ViewEnd, gt4_scene_view_end);
    SAME_AT(ViewEnd, gt4_scene_view_end, view, view);
    SAME_AT(ViewEnd, gt4_scene_view_end, instances, instances);

    SAME_SIZE(Projection, gt4_scene_projection);
    SAME_AT(Projection, gt4_scene_projection, id, id);
    SAME_AT(Projection, gt4_scene_projection, view, view);
    SAME_AT(Projection, gt4_scene_projection, viewToClip, view_to_clip);
    SAME_AT(Projection, gt4_scene_projection, viewportScale, viewport_scale);
    SAME_AT(Projection, gt4_scene_projection, viewportOffset, viewport_offset);
    SAME_AT(Projection, gt4_scene_projection, clamp, clamp);

    SAME_SIZE(Instance, gt4_scene_instance);
    SAME_AT(Instance, gt4_scene_instance, view, view);
    SAME_AT(Instance, gt4_scene_instance, flags, flags);
    SAME_AT(Instance, gt4_scene_instance, serial, serial);
    SAME_AT(Instance, gt4_scene_instance, set, set);
    SAME_AT(Instance, gt4_scene_instance, model, model);
    SAME_AT(Instance, gt4_scene_instance, lod, lod);
    SAME_AT(Instance, gt4_scene_instance, lodCount, lod_count);
    SAME_AT(Instance, gt4_scene_instance, cmd, cmd);
    SAME_AT(Instance, gt4_scene_instance, entrant, entrant);
    SAME_AT(Instance, gt4_scene_instance, objectToView, object_to_view);
    SAME_AT(Instance, gt4_scene_instance, axisScale, axis_scale);
    SAME_AT(Instance, gt4_scene_instance, shapeScale, shape_scale);
    SAME_AT(Instance, gt4_scene_instance, items, items);
    SAME_AT(Instance, gt4_scene_instance, matrixSet, matrix_set);
    SAME_AT(Instance, gt4_scene_instance, section, section);
    SAME_AT(Instance, gt4_scene_instance, projection, projection);
    SAME_AT(Instance, gt4_scene_instance, reserved, reserved);

    SAME_SIZE(Light, gt4_scene_light);
    SAME_AT(Light, gt4_scene_light, view, view);
    SAME_AT(Light, gt4_scene_light, kind, kind);
    SAME_AT(Light, gt4_scene_light, source, source);
    SAME_AT(Light, gt4_scene_light, id, id);
    SAME_AT(Light, gt4_scene_light, entrant, entrant);
    SAME_AT(Light, gt4_scene_light, position, position);
    SAME_AT(Light, gt4_scene_light, width, width);
    SAME_AT(Light, gt4_scene_light, direction, direction);
    SAME_AT(Light, gt4_scene_light, height, height);
    SAME_AT(Light, gt4_scene_light, colour, colour);
    SAME_AT(Light, gt4_scene_light, intensity, intensity);
    SAME_AT(Light, gt4_scene_light, range, range);
    SAME_AT(Light, gt4_scene_light, cosInner, cos_inner);
    SAME_AT(Light, gt4_scene_light, cosOuter, cos_outer);
    SAME_AT(Light, gt4_scene_light, reserved, reserved);

    SAME_SIZE(Emitter, gt4_scene_emitter);
    SAME_AT(Emitter, gt4_scene_emitter, kind, kind);
    SAME_AT(Emitter, gt4_scene_emitter, flags, flags);
    SAME_AT(Emitter, gt4_scene_emitter, count, count);
    SAME_AT(Emitter, gt4_scene_emitter, id, id);
    SAME_AT(Emitter, gt4_scene_emitter, entrant, entrant);
    SAME_AT(Emitter, gt4_scene_emitter, position, position);
    SAME_AT(Emitter, gt4_scene_emitter, size, size);
    SAME_AT(Emitter, gt4_scene_emitter, velocity, velocity);
    SAME_AT(Emitter, gt4_scene_emitter, rate, rate);
    SAME_AT(Emitter, gt4_scene_emitter, colour, colour);
    SAME_AT(Emitter, gt4_scene_emitter, lifetime, lifetime);
    SAME_AT(Emitter, gt4_scene_emitter, spread, spread);
    SAME_AT(Emitter, gt4_scene_emitter, view, view);
    SAME_AT(Emitter, gt4_scene_emitter, reserved0, reserved0);
    SAME_AT(Emitter, gt4_scene_emitter, reserved1, reserved1);

    SAME_SIZE(Marker, gt4_scene_marker);
    SAME_AT(Marker, gt4_scene_marker, marker, marker);
    SAME_AT(Marker, gt4_scene_marker, view, view);

    SAME_SIZE(Wheel, gt4_scene_wheel);
    SAME_AT(Wheel, gt4_scene_wheel, contact, contact);
    SAME_AT(Wheel, gt4_scene_wheel, load, load);
    SAME_AT(Wheel, gt4_scene_wheel, slipRatio, slip_ratio);
    SAME_AT(Wheel, gt4_scene_wheel, slipAngle, slip_angle);
    SAME_AT(Wheel, gt4_scene_wheel, forceLateral, force_lateral);
    SAME_AT(Wheel, gt4_scene_wheel, forceLongitudinal, force_longitudinal);
    SAME_AT(Wheel, gt4_scene_wheel, spin, spin);
    SAME_AT(Wheel, gt4_scene_wheel, compression, compression);
    SAME_AT(Wheel, gt4_scene_wheel, surface, surface);
    SAME_AT(Wheel, gt4_scene_wheel, onGround, on_ground);
    SAME_AT(Wheel, gt4_scene_wheel, reserved, reserved);

    SAME_SIZE(Vehicle, gt4_scene_vehicle);
    SAME_AT(Vehicle, gt4_scene_vehicle, entrant, entrant);
    SAME_AT(Vehicle, gt4_scene_vehicle, flags, flags);
    SAME_AT(Vehicle, gt4_scene_vehicle, valid, valid);
    SAME_AT(Vehicle, gt4_scene_vehicle, bodyToWorld, body_to_world);
    SAME_AT(Vehicle, gt4_scene_vehicle, velocity, velocity);
    SAME_AT(Vehicle, gt4_scene_vehicle, speed, speed);
    SAME_AT(Vehicle, gt4_scene_vehicle, angularVelocity, angular_velocity);
    SAME_AT(Vehicle, gt4_scene_vehicle, rpm, rpm);
    SAME_AT(Vehicle, gt4_scene_vehicle, throttle, throttle);
    SAME_AT(Vehicle, gt4_scene_vehicle, brake, brake);
    SAME_AT(Vehicle, gt4_scene_vehicle, clutch, clutch);
    SAME_AT(Vehicle, gt4_scene_vehicle, steer, steer);
    SAME_AT(Vehicle, gt4_scene_vehicle, gear, gear);
    SAME_AT(Vehicle, gt4_scene_vehicle, lamps, lamps);
    SAME_AT(Vehicle, gt4_scene_vehicle, steeringTorque, steering_torque);
    SAME_AT(Vehicle, gt4_scene_vehicle, wheel, wheel);
    SAME_AT(Vehicle, gt4_scene_vehicle, reserved, reserved);

    // what the host requires of each kind (a.6), and that every record keeps the 4-byte alignment its floats need
    const uint32_t sizes[] = {0, 16, 20, 88, 8, 120, 80, 80, 8, 328, 120};
    for (uint16_t k = 1; k <= 10; ++k)
        check(scene::recordSize(k) == sizes[k] && sizes[k] % 4 == 0, "recordSize(" + std::to_string(k) + ")");
    check(scene::recordSize(0) == 0 && scene::recordSize(11) == 0, "recordSize of an unknown kind");
}

}  // namespace

int main() {
    layouts();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("scene_records: all checks passed\n");
    return failures ? 1 : 0;
}
