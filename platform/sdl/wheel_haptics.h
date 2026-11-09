#pragma once
// The wheel's force feedback (PLAN.md D7): GT4's three LGDEV effects, as input::wheelForces gives them, played on the
// wheel's SDL_Haptic: a constant force (GT4's effect A), a spring (C) and a damper (B).
//
// - The constant force comes from the cartesian direction (1, 0, 0) at level +A: SDL names where a force comes from,
//   so a positive A (the wheel turned right) pushes left, towards the centre (PLAN D7: the host plays -A). It lasts
//   wheel_ffb.h's 200 ms and is run again every 30 ms and on a change while it is on (lane C: a few device writes a
//   second at any display rate), so a host that stalls lets it lapse at the device. The spring and the damper are
//   passive and run without end.
// - The device's own autocentre is switched off at open where SDL can (GT4's spring is the centring, as on the PS2
//   wheel; Linux's Logitech driver starts with its own on), and the open line says which.
// - The stops are the caller's (main.cpp: a page, a pause, the watchdog, no live game); stop() stops every effect and
//   play() applies the latest forces again. A different wheel (another port, a new device) closes the old one first.
// - D8's vibration (kerbs, slip: wheel_physics.h) is a sine with the constant force's length and renewal, so it too
//   lapses with a stalled host.
// - A wheel SDL gives no haptic for (a session's virtual wheel) is played on nothing: the forces are still worked out
//   and, in a session, recorded, so the session test sees what a real wheel would get.
#include "wheel_ffb.h"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_joystick.h>

#include <cstdint>
#include <functional>
#include <string>

namespace gt4 {

class WheelHaptics {
public:
    // `record`: a session's event sink (an event type and its JSON fields); empty outside sessions.
    using Recorder = std::function<void(const std::string& type, const std::string& fields)>;
    explicit WheelHaptics(Recorder record = {});
    ~WheelHaptics();   // every effect stopped, the device closed
    WheelHaptics(const WheelHaptics&) = delete;
    WheelHaptics& operator=(const WheelHaptics&) = delete;

    // The wheel to play on (null: none, which stops and closes the one before). Its identity is the joystick's
    // instance ID, never reused: a wheel that leaves and comes back in one event pump is a new one (lane C).
    void attach(SDL_Joystick* joystick);
    // This frame's forces (nowMs: SDL_GetTicks). Changes go to the device; the constant force is run again every
    // 30 ms and on a change while it is on (its 200 ms renewed). Ends a stop.
    void play(const input::WheelForces& forces, uint64_t nowMs);
    // Every effect stopped; `why` logged and recorded once per stop.
    void stop(const char* why);
    bool stopped() const { return stopped_; }

private:
    void close();
    void record(const std::string& type, const std::string& fields) const;
    bool sameForces(const input::WheelForces& a, const input::WheelForces& b) const;

    Recorder record_;
    SDL_Joystick* joystick_ = nullptr;
    SDL_JoystickID id_ = 0;
    SDL_Haptic* haptic_ = nullptr;
    uint32_t features_ = 0;
    int constant_ = -1, spring_ = -1, damper_ = -1, periodic_ = -1;   // effect ids (-1: not made)
    bool constantRuns_ = false, springRuns_ = false, damperRuns_ = false, periodicRuns_ = false;
    input::WheelForces applied_{};                           // what the device has
    input::WheelForces recorded_{};                          // what a session last recorded
    bool stopped_ = true;
    uint64_t renewedAt_ = 0;                                 // the constant force's last run
    uint64_t playedAt_ = 0;                                  // the last play(): the host's frame time
    uint64_t sineRenewedAt_ = 0;                             // the vibration's last run (its own 30 ms timer)
    bool centreLogged_ = false;
};

}  // namespace gt4
