#pragma once
#include <memory>
#include <string>
#include <cstdint>
struct SDL_Window;
class Renderer;

// Test instrumentation stays above the engine's platform interfaces.
class Session {
public:
    Session(const std::string& directory, bool audio, bool diagnostics = false);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool tick(Renderer& renderer, SDL_Window* window, uint32_t frame);
    float horizontal() const;
    bool action() const;
    // Future game mixer integration: stereo signed-16 PCM, 48 kHz, main thread.
    void submitAudio(const int16_t* samples, uint32_t frames);
    void frame(uint32_t number, double milliseconds, uint32_t width, uint32_t height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
