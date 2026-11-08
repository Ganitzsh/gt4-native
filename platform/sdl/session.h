#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
struct SDL_Window;
struct SDL_Gamepad;
class Renderer;

// Test instrumentation stays above the engine's platform interfaces.
class Session {
public:
    Session(const std::string& directory, bool audio, bool diagnostics = false);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    // window is null in headless runs (resize then changes the offscreen size).
    bool tick(Renderer& renderer, SDL_Window* window, uint32_t frame);
    float horizontal() const;
    bool action() const;
    // The virtual gamepad, which also plays the game's pad 1 in a game session; null after "unplug" until the next
    // "button" or "axis" plugs it in again.
    SDL_Gamepad* gamepad() const;
    // The game's pad 2: a second virtual gamepad, plugged in by the first "button2"/"axis2" command and removed by
    // "unplug2"; null while there is none (port 2 empty).
    SDL_Gamepad* gamepad2() const;
    // The SDL id of the session's pad 1 (`pad` 0) or pad 2 (1), 0 while it is unplugged: the host syncs its ports with
    // them right after the commands run (platform/sdl/pad_devices.h).
    uint32_t padId(int pad) const;
    // Future game mixer integration: stereo signed-16 PCM, 48 kHz, main thread.
    void submitAudio(const int16_t* samples, uint32_t frames);
    void frame(uint32_t number, double milliseconds, double gpuMilliseconds, uint32_t width, uint32_t height);
    // "set KEY VALUE" commands: the handler applies a setting and returns an error message (empty: done).
    void onSetting(std::function<std::string(const std::string&, const std::string&)> handler);
    // The game's clock in VBlanks, for "vwait N" (a game session: presses timed in game frames, which the
    // host's frames are not when the game is busy).
    void onGameClock(std::function<uint64_t()> clock);
    // Adds an event to events.jsonl; fields is a JSON object body without braces.
    void record(const char* type, const std::string& fields);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
