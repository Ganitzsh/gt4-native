// platform/input/response.cpp (PLAN.md D12): the steering's shape is continuous at the inner dead zone's edge (0 at
// the edge, one steering byte just above it), monotonic, and gives full lock at full travel with saturation 100 %;
// smoothing's step response has its time constant; a key ramps to full lock in key_rate and back in half that;
// input.response = original leaves apply()'s bytes as they are for every input.
#include "response.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace gt4;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// The steering byte (pad byte 4) of a stick at x through the shape.
int steerByte(float x, const input::Shape& shape) { return ps2::padAxis(input::shapeAxis(x, shape)); }

// Sweeps the stick over SDL's every step from the centre to each end: the byte moves by one at most a step, never
// back, reaches every byte to full lock, and is 128 up to the dead zone's edge.
void sweep(const input::Shape& shape, const char* name) {
    char what[160];
    for (int side : {1, -1}) {
        int last = 128;
        bool gap = false, back = false, early = false;
        bool seen[256] = {};
        for (int raw = 0; raw <= 32768; ++raw) {
            const float x = side * static_cast<float>(raw) / 32767.0f;   // SDL's -32768 reads -1.00003
            const int b = steerByte(x, shape);
            seen[b] = true;
            gap |= std::abs(b - last) > 1;
            back |= side > 0 ? b < last : b > last;
            early |= std::fabs(x) <= shape.deadzone && b != 128;
            last = b;
        }
        const int end = side > 0 ? 255 : 0;
        bool every = true;
        for (int b = 128; b != end + side; b += side) every &= seen[b];
        std::snprintf(what, sizeof what, "%s, %s: the byte steps by one at most", name, side > 0 ? "right" : "left");
        expect(!gap, what);
        std::snprintf(what, sizeof what, "%s, %s: monotonic", name, side > 0 ? "right" : "left");
        expect(!back, what);
        std::snprintf(what, sizeof what, "%s, %s: centre up to the dead zone's edge", name, side > 0 ? "right" : "left");
        expect(!early, what);
        std::snprintf(what, sizeof what, "%s, %s: every byte to full lock reached", name, side > 0 ? "right" : "left");
        expect(every && last == end, what);
    }
}

}  // namespace

int main() {
    const input::Shape defaults;   // 5 % inner, 2 % outer, linear
    sweep(defaults, "defaults");
    sweep({0.05f, 0.02f, 2.0f, 1.0f, 1.0f}, "linearity 2");
    sweep({0.30f, 0.30f, 3.0f, 1.0f, 1.0f}, "the widest dead zones, linearity 3");
    sweep({0.0f, 0.0f, 1.0f, 1.0f, 1.0f}, "no dead zones");

    // the dead zone's edge: 0 there, the first step above it one byte
    expect(input::shapeMagnitude(0.05f, defaults) == 0.0f, "0 at the inner edge");
    int first = 128;
    for (int raw = 1; raw <= 32767 && first == 128; ++raw) first = steerByte(raw / 32767.0f, defaults);
    expect(first == 129, "the first byte above the edge is one step");
    // the outer dead zone: full lock from 98 % of travel
    expect(steerByte(0.98f, defaults) == 255 && steerByte(0.97f, defaults) < 255, "full lock from the outer dead zone");
    // sensitivity: full lock earlier; saturation: the most lock
    expect(steerByte(0.6f, {0.05f, 0.02f, 1.0f, 2.0f, 1.0f}) == 255, "sensitivity 2: full lock past half travel");
    expect(steerByte(1.0f, {0.05f, 0.02f, 1.0f, 1.0f, 0.5f}) == ps2::padAxis(0.5f), "saturation 50 %");
    expect(steerByte(-1.0f, {0.05f, 0.02f, 1.0f, 0.5f, 1.0f}) == ps2::padAxis(-0.5f), "sensitivity 0.5: half lock");
    // linearity: finer near the centre
    expect(input::shapeMagnitude(0.5f, {0.0f, 0.0f, 2.0f, 1.0f, 1.0f}) == 0.25f, "linearity 2 at half travel");

    // smoothing: after one time constant, 1 - 1/e of a step
    {
        float y = 0.0f;
        for (int ms = 0; ms < 40; ++ms) y = input::smooth(y, 1.0f, 40.0f, 0.001f);
        expect(std::fabs(y - (1.0f - std::exp(-1.0f))) < 1e-3f, "smoothing's time constant");
        expect(input::smooth(0.3f, 1.0f, 0.0f, 0.016f) == 1.0f, "no smoothing at 0 ms");
    }
    // the keys' ramp: full lock in 125 ms, back in 62.5 ms (frames of 1/64 s: exact steps of 1/8 and 1/4), and
    // across the centre at the return rate first
    {
        const float dt = 1.0f / 64.0f;
        float v = 0.0f;
        int steps = 0;
        while (v < 1.0f && steps < 100) { v = input::ramp(v, 1.0f, 125.0f, dt); ++steps; }
        expect(steps == 8, "a key: full lock in 125 ms");
        steps = 0;
        while (v > 0.0f && steps < 100) { v = input::ramp(v, 0.0f, 125.0f, dt); ++steps; }
        expect(steps == 4, "released: straight in half the time");
        v = 0.5f;
        v = input::ramp(v, -1.0f, 150.0f, 0.050f);   // 50 ms back: 0.667 at twice the rate, stopped at the centre
        expect(v == 0.0f, "the other key: back to the centre first");
        expect(input::ramp(0.2f, 1.0f, 0.0f, dt) == 1.0f, "key_rate 0: at once");
    }

    // the pad: original untouched, modern recomputed
    const input::Map map = input::originalMap();
    {
        std::mt19937 random(12);
        input::Response response(map);
        const input::ResponseConfig original;   // modern = false
        bool same = true;
        for (int i = 0; i < 20000; ++i) {
            input::Snapshot s;
            s.gamepad = (random() & 3) != 0;
            for (int k : {SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_1, SDL_SCANCODE_3, SDL_SCANCODE_X})
                s.keys[k] = (random() & 3) == 0;
            for (float& a : s.axes) a = std::uniform_real_distribution<float>(-1.00003f, 1.0f)(random);
            s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = std::fabs(s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]);
            s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = std::fabs(s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]);
            ps2::PadInput in = input::apply(map, s);
            uint8_t want[18], got[18];
            ps2::encodePad(in, want);
            response.step(in, s, original, 0.016f);
            ps2::encodePad(in, got);
            same &= std::memcmp(want, got, 18) == 0;
        }
        expect(same, "original: apply()'s bytes for every input");
    }
    {
        input::Response response(map);
        input::ResponseConfig modern;
        modern.modern = true;
        input::Snapshot s;
        s.gamepad = true;
        s.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0.07f;   // inside the binding's 8 % cut, past the 5 % dead zone
        ps2::PadInput in = input::apply(map, s);
        expect(ps2::padAxis(in.leftX) == 128, "(original: the 8 % cut)");
        response.step(in, s, modern, 0.016f);
        expect(ps2::padAxis(in.leftX) == ps2::padAxis(input::shapeAxis(0.07f, modern.steer)) &&
                   ps2::padAxis(in.leftX) > 129,
               "modern: no 8 % cut, the shape's value");
        s.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0.0f;
        s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0.04f;
        s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 1.0f;
        in = input::apply(map, s);
        response.step(in, s, modern, 0.016f);
        expect(in.button[ps2::PadR2] == 0.0f && in.button[ps2::PadL2] == 1.0f, "pedals: the dead zone, full travel");
        s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.0f;
        s.keys[SDL_SCANCODE_3] = true;
        in = input::apply(map, s);
        response.step(in, s, modern, 0.016f);
        expect(in.button[ps2::PadR2] == 1.0f && in.button[ps2::PadL2] == 0.0f, "pedals: a key reads full");
        s.axes[SDL_GAMEPAD_AXIS_RIGHTY] = -0.5f;   // the right stick up: a gas half
        in = input::apply(map, s);
        response.step(in, s, modern, 0.016f);
        expect(in.rightY == input::shapeAxis(-0.5f, modern.pedal), "the right stick's halves shaped as pedals");
        // a steering key ramps, summed with the stick
        s = input::Snapshot{};
        s.gamepad = true;
        s.keys[SDL_SCANCODE_L] = true;
        s.axes[SDL_GAMEPAD_AXIS_LEFTX] = -0.5f;
        float last = -2.0f;
        bool rising = true;
        for (int f = 0; f < 12; ++f) {
            in = input::apply(map, s);
            response.step(in, s, modern, 0.016f);
            rising &= in.leftX >= last;
            last = in.leftX;
        }
        expect(rising && std::fabs(last - input::shapeAxis(0.5f, modern.steer)) < 1e-6f,
               "a key ramps, summed with the stick, then shaped");
    }

    if (failures) return 1;
    std::printf("input_response: all checks passed\n");
    return 0;
}
