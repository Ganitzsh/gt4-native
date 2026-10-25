#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
struct SDL_Window;
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
    // Future game mixer integration: stereo signed-16 PCM, 48 kHz, main thread.
    void submitAudio(const int16_t* samples, uint32_t frames);
    void frame(uint32_t number, double milliseconds, double gpuMilliseconds, uint32_t width, uint32_t height);
    // "set KEY VALUE" commands: the handler applies a setting and returns an error message (empty: done).
    void onSetting(std::function<std::string(const std::string&, const std::string&)> handler);
    // Adds an event to events.jsonl; fields is a JSON object body without braces.
    void record(const char* type, const std::string& fields);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
