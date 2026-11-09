// The wheel's force feedback (wheel_haptics.h).
#include "wheel_haptics.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_platform.h>

#include <cstdio>
#include <string>

namespace gt4 {

namespace {

SDL_HapticEffect constantEffect(int16_t level) {
    SDL_HapticEffect e{};
    e.type = SDL_HAPTIC_CONSTANT;
    e.constant.type = SDL_HAPTIC_CONSTANT;
    e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    e.constant.direction.dir[0] = 1;   // from the right: +A pushes left (wheel_haptics.h)
    e.constant.length = input::kWheelConstantLengthMs;
    e.constant.level = level;
    return e;
}

SDL_HapticEffect sineEffect(int16_t magnitude, uint16_t periodMs) {
    SDL_HapticEffect e{};
    e.type = SDL_HAPTIC_SINE;
    e.periodic.type = SDL_HAPTIC_SINE;
    e.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
    e.periodic.direction.dir[0] = 1;
    e.periodic.length = input::kWheelConstantLengthMs;   // (renewed as the constant force is)
    e.periodic.period = periodMs;
    e.periodic.magnitude = magnitude;
    return e;
}

SDL_HapticEffect conditionEffect(uint16_t type, const input::WheelCondition& c) {
    SDL_HapticEffect e{};
    e.type = type;
    e.condition.type = type;
    e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    e.condition.direction.dir[0] = 1;
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = c.rightSat;
    e.condition.left_sat[0] = c.leftSat;
    e.condition.right_coeff[0] = c.rightCoeff;
    e.condition.left_coeff[0] = c.leftCoeff;
    e.condition.center[0] = c.center;
    return e;
}

std::string onOff(bool on) { return on ? "true" : "false"; }

}  // namespace

WheelHaptics::WheelHaptics(Recorder record) : record_(std::move(record)) {}

WheelHaptics::~WheelHaptics() { close(); }

void WheelHaptics::record(const std::string& type, const std::string& fields) const {
    if (record_) record_(type, fields);
}

bool WheelHaptics::sameForces(const input::WheelForces& a, const input::WheelForces& b) const {
    // (springCentreRaw is GT4's, not played: lane C)
    return a.constantOn == b.constantOn && a.constantLevel == b.constantLevel && a.springOn == b.springOn &&
           a.spring == b.spring && a.damperOn == b.damperOn && a.damper == b.damper &&
           a.periodicOn == b.periodicOn && a.periodicMagnitude == b.periodicMagnitude &&
           a.periodicPeriodMs == b.periodicPeriodMs;
}

void WheelHaptics::close() {
    if (haptic_) {
        SDL_StopHapticEffects(haptic_);
        for (int* id : {&constant_, &spring_, &damper_, &periodic_})
            if (*id >= 0) SDL_DestroyHapticEffect(haptic_, *id);
        SDL_CloseHaptic(haptic_);
        std::printf("Input: wheel force feedback closed\n");
        std::fflush(stdout);
    }
    haptic_ = nullptr;
    joystick_ = nullptr;
    id_ = 0;
    features_ = 0;
    constant_ = spring_ = damper_ = periodic_ = -1;
    constantRuns_ = springRuns_ = damperRuns_ = periodicRuns_ = false;
    applied_ = {};
    stopped_ = true;
}

void WheelHaptics::attach(SDL_Joystick* joystick) {
    const SDL_JoystickID id = joystick ? SDL_GetJoystickID(joystick) : 0;
    if (id == id_) return;
    if (joystick_) {
        stop("the wheel left or changed");
        close();
    }
    if (!joystick) return;
    joystick_ = joystick;
    id_ = id;
    haptic_ = SDL_OpenHapticFromJoystick(joystick);
    if (!haptic_) {
        const char* e = SDL_GetError();
        std::printf("Input: wheel %s: no force feedback device (%s); its forces are worked out and played on nothing\n",
                    SDL_GetJoystickName(joystick), e && *e ? e : "SDL gives none");
        record("wheel_haptic", "\"opened\":false");
    } else {
        features_ = SDL_GetHapticFeatures(haptic_);
        if (features_ & SDL_HAPTIC_GAIN) SDL_SetHapticGain(haptic_, 100);   // (the strength is ours: wheel_ffb.h)
        const bool autocentre = (features_ & SDL_HAPTIC_AUTOCENTER) && SDL_SetHapticAutocenter(haptic_, 0);
        const char* name = SDL_GetHapticName(haptic_);
        // the platform says which backend SDL used (DirectInput on Windows, evdev on Linux): PLAN D7's live check
        std::printf("Input: wheel force feedback: %s on %s: constant %s, spring %s, damper %s, %s\n",
                    name ? name : "?", SDL_GetPlatform(), features_ & SDL_HAPTIC_CONSTANT ? "yes" : "no",
                    features_ & SDL_HAPTIC_SPRING ? "yes" : "no", features_ & SDL_HAPTIC_DAMPER ? "yes" : "no",
                    autocentre ? "autocentre off" : "no autocentre control");
        record("wheel_haptic", "\"opened\":true");
    }
    std::fflush(stdout);
    stopped_ = true;
}

void WheelHaptics::play(const input::WheelForces& f, uint64_t nowMs) {
    if (!joystick_) return;
    if (f.springCentreRaw && !centreLogged_) {   // (its scale is unknown: played centred; wheel_ffb.h)
        centreLogged_ = true;
        std::printf("Input: GT4 set the wheel's spring off centre (%d, scale unknown); played centred\n",
                    static_cast<int>(f.springCentreRaw));
    }
    // a session sees each change, and a gap in the constant force's renewal (the host stalled: the device's 200 ms
    // ran out in it), with the host's last frame time, so a gap reads as load or a freeze
    if (stopped_ || !sameForces(f, recorded_)) {
        recorded_ = f;
        record("wheel_ffb", "\"constant\":" + onOff(f.constantOn) + ",\"level\":" + std::to_string(f.constantLevel) +
                                ",\"spring\":" + onOff(f.springOn) + ",\"spring_coeff\":" +
                                std::to_string(f.spring.rightCoeff) + ",\"damper\":" + onOff(f.damperOn) +
                                ",\"damper_coeff\":" + std::to_string(f.damper.rightCoeff) + ",\"periodic\":" +
                                onOff(f.periodicOn) + ",\"periodic_magnitude\":" +
                                std::to_string(f.periodicMagnitude) + ",\"periodic_ms\":" +
                                std::to_string(f.periodicPeriodMs));
    }
    if (f.constantOn && !stopped_ && renewedAt_ && nowMs - renewedAt_ > input::kWheelConstantLengthMs)
        record("wheel_ffb_gap", "\"ms\":" + std::to_string(nowMs - renewedAt_) + ",\"frame_ms\":" +
                                    std::to_string(playedAt_ ? nowMs - playedAt_ : 0) + ",\"lapsed_after_ms\":" +
                                    std::to_string(input::kWheelConstantLengthMs));
    playedAt_ = nowMs;
    const bool wasStopped = stopped_;
    stopped_ = false;
    const bool renew = f.constantOn && (wasStopped || !renewedAt_ || f.constantLevel != applied_.constantLevel ||
                                        nowMs - renewedAt_ >= input::kWheelConstantRenewMs);
    if (renew) renewedAt_ = nowMs;
    if (!haptic_) {
        applied_ = f;
        return;
    }
    const auto drive = [&](uint32_t feature, int& id, bool& runs, bool on, bool changed, bool renew,
                           const SDL_HapticEffect& effect) {
        if (!(features_ & feature)) return;   // (a wheel that lacks one gets the others)
        if (!on) {
            if (runs) SDL_StopHapticEffect(haptic_, id);
            runs = false;
            return;
        }
        SDL_HapticEffect e = effect;
        if (id < 0) id = SDL_CreateHapticEffect(haptic_, &e);
        else if (changed) SDL_UpdateHapticEffect(haptic_, id, &e);
        if (id < 0) return;
        if (!runs || renew) runs = SDL_RunHapticEffect(haptic_, id, 1);
    };
    drive(SDL_HAPTIC_CONSTANT, constant_, constantRuns_, f.constantOn,
          wasStopped || f.constantLevel != applied_.constantLevel, renew, constantEffect(f.constantLevel));
    drive(SDL_HAPTIC_SPRING, spring_, springRuns_, f.springOn, wasStopped || !(f.spring == applied_.spring), false,
          conditionEffect(SDL_HAPTIC_SPRING, f.spring));
    drive(SDL_HAPTIC_DAMPER, damper_, damperRuns_, f.damperOn, wasStopped || !(f.damper == applied_.damper), false,
          conditionEffect(SDL_HAPTIC_DAMPER, f.damper));
    const bool sineChanged = f.periodicMagnitude != applied_.periodicMagnitude ||
                             f.periodicPeriodMs != applied_.periodicPeriodMs;
    const bool sineRenew = f.periodicOn && (wasStopped || sineChanged || !sineRenewedAt_ ||
                                            nowMs - sineRenewedAt_ >= input::kWheelConstantRenewMs);
    if (sineRenew) sineRenewedAt_ = nowMs;
    drive(SDL_HAPTIC_SINE, periodic_, periodicRuns_, f.periodicOn, wasStopped || sineChanged, sineRenew,
          sineEffect(f.periodicMagnitude, f.periodicPeriodMs));
    applied_ = f;
}

void WheelHaptics::stop(const char* why) {
    if (stopped_ || !joystick_) return;
    if (haptic_) SDL_StopHapticEffects(haptic_);
    constantRuns_ = springRuns_ = damperRuns_ = periodicRuns_ = false;
    stopped_ = true;
    renewedAt_ = sineRenewedAt_ = 0;
    std::printf("Input: wheel force feedback stopped: %s\n", why);
    std::fflush(stdout);
    record("wheel_ffb", std::string("\"stopped\":\"") + why + "\"");
}

}  // namespace gt4
