#include "session.h"
#include "renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* action) {
    if (!ok) throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}
std::string quote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c >= 32 && c < 127) result += static_cast<char>(c);
        else { char hex[7]; std::snprintf(hex, sizeof(hex), "\\u%04x", c); result += hex; }
    }
    return result + "\"";
}
void le(std::ostream& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (i * 8)) & 255));
}
SDL_GamepadButton button(const std::string& name) {
    if (name == "cross") return SDL_GAMEPAD_BUTTON_SOUTH;
    if (name == "circle") return SDL_GAMEPAD_BUTTON_EAST;
    if (name == "square") return SDL_GAMEPAD_BUTTON_WEST;
    if (name == "triangle") return SDL_GAMEPAD_BUTTON_NORTH;
    if (name == "select") return SDL_GAMEPAD_BUTTON_BACK;
    if (name == "l1") return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (name == "r1") return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (name == "l3") return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (name == "r3") return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    return SDL_GetGamepadButtonFromString(name.c_str());
}
}

struct Session::Impl {
    fs::path directory;
    std::ofstream log, wav;
    SDL_JoystickID id = 0;
    SDL_Joystick* joystick = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    SDL_AudioStream* audio = nullptr;
    uint32_t audioBytes = 0;
    Uint64 started = SDL_GetTicks(), generated = 0;
    double frequency = 0.0, phase = 0.0;
    bool diagnostics = false;
    int resizeWidth = 0, resizeHeight = 0;
    Uint64 resizeDeadline = 0;
    Uint64 toneUntil = 0;
    std::vector<fs::path> captures;
    uint32_t waitFrames = 0;   // "wait N": frames to render before the next command
    std::function<std::string(const std::string&, const std::string&)> setting;
    explicit Impl(const std::string& path) : directory(path) {}
    ~Impl() {
        if (audio) SDL_DestroyAudioStream(audio);
        if (wav) { wav.seekp(0); header(); wav.close(); }
        if (gamepad) SDL_CloseGamepad(gamepad);
        if (joystick) SDL_CloseJoystick(joystick);
        if (id) SDL_DetachVirtualJoystick(id);
    }
    void header() {
        wav.write("RIFF", 4); le(wav, 36 + audioBytes, 4); wav.write("WAVEfmt ", 8);
        le(wav, 16, 4); le(wav, 1, 2); le(wav, 2, 2); le(wav, 48000, 4);
        le(wav, 48000 * 4, 4); le(wav, 4, 2); le(wav, 16, 2);
        wav.write("data", 4); le(wav, audioBytes, 4);
    }
    void event(const char* type, const std::string& fields = {}) {
        log << "{\"ms\":" << (SDL_GetTicks() - started) << ",\"type\":" << quote(type);
        if (!fields.empty()) log << ',' << fields;
        log << "}\n"; log.flush();
        if (!log) throw std::runtime_error("Session event log write failed");
    }
    void initialize(bool enableAudio) {
        fs::create_directories(directory / "commands");
        fs::create_directories(directory / "processed");
        fs::create_directories(directory / "frames");
        // Refuse to overwrite previous captures/logs; each run needs a fresh session directory.
        if (fs::exists(directory / "events.jsonl") || fs::exists(directory / "audio.wav"))
            throw std::runtime_error("Session directory already contains a recording");
        log.open(directory / "events.jsonl");
        if (!log) throw std::runtime_error("Cannot create session log");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = 15;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << 15) - 1;
        desc.name = "GT4 automation controller";
        id = SDL_AttachVirtualJoystick(&desc);
        require(id != 0, "SDL_AttachVirtualJoystick");
        joystick = SDL_OpenJoystick(id);
        require(joystick != nullptr, "SDL_OpenJoystick");
        gamepad = SDL_OpenGamepad(id);
        require(gamepad != nullptr, "SDL_OpenGamepad");
        reset();
        if (enableAudio) {
            require(SDL_InitSubSystem(SDL_INIT_AUDIO), "SDL_InitSubSystem(audio)");
            SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            require(audio != nullptr, "SDL_OpenAudioDeviceStream");
            require(SDL_ResumeAudioStreamDevice(audio), "SDL_ResumeAudioStreamDevice");
            wav.open(directory / "audio.wav", std::ios::binary);
            if (!wav) throw std::runtime_error("Cannot create audio capture");
            header();
        }
        event("session", "\"audio\":" + std::string(audio ? "true" : "false") +
              ",\"controller\":" + std::to_string(id) + ",\"video_driver\":" + quote(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none (headless)"));
    }
    void reset() {
        for (int i = 0; i < 15; ++i) require(SDL_SetJoystickVirtualButton(joystick, i, false), "virtual button");
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
            require(SDL_SetJoystickVirtualAxis(joystick, i, i >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0), "virtual axis");
        SDL_UpdateJoysticks();
    }
    void submit(const int16_t* samples, uint32_t frames) {
        if (!audio) throw std::runtime_error("Audio is disabled for this session");
        const uint64_t bytes = static_cast<uint64_t>(frames) * 4;
        if (bytes > INT32_MAX || bytes + audioBytes > UINT32_MAX - 36)
            throw std::runtime_error("Audio capture exceeds WAV/stream size limits");
        require(SDL_PutAudioStreamData(audio, samples, static_cast<int>(bytes)), "SDL_PutAudioStreamData");
        wav.write(reinterpret_cast<const char*>(samples), static_cast<std::streamsize>(bytes));
        if (!wav) throw std::runtime_error("Audio capture write failed");
        audioBytes += static_cast<uint32_t>(bytes);
        double squares = 0.0;
        int peak = 0, clipped = 0;
        for (uint32_t i = 0; i < frames * 2; ++i) {
            const int value = std::abs(static_cast<int>(samples[i]));
            peak = std::max(peak, value);
            clipped += value >= 32767;
            const double normalized = static_cast<double>(samples[i]) / 32768.0;
            squares += normalized * normalized;
        }
        const int queued = SDL_GetAudioStreamQueued(audio);
        require(queued >= 0, "SDL_GetAudioStreamQueued");
        event("audio", "\"frames\":" + std::to_string(frames) + ",\"peak\":" + std::to_string(peak) +
              ",\"rms\":" + std::to_string(frames ? std::sqrt(squares / (frames * 2)) : 0.0) +
              ",\"clipped_samples\":" + std::to_string(clipped) + ",\"queued_bytes\":" + std::to_string(queued));
    }
    void diagnosticAudio() {
        if (!audio) return;
        // Silence/tone are diagnostic inputs only. Game mixing will call submitAudio instead.
        // Generate against elapsed time, not rendering speed, to avoid audio-rate drift.
        const Uint64 target = (SDL_GetTicks() - started) * 48 + 2048;
        if (target <= generated) return;
        const Uint64 end = std::min(target, generated + 4800);
        std::vector<int16_t> samples(static_cast<size_t>(end - generated) * 2);
        for (Uint64 i = generated; i < end; ++i) {
            const bool sounding = frequency > 0.0 && i < toneUntil;
            const auto value = sounding ? static_cast<int16_t>(std::sin(phase) * 8000.0) : static_cast<int16_t>(0);
            if (sounding) { phase += 6.283185307179586 * frequency / 48000.0; if (phase >= 6.283185307179586) phase -= 6.283185307179586; }
            samples[static_cast<size_t>(i - generated) * 2] = value;
            samples[static_cast<size_t>(i - generated) * 2 + 1] = value;
        }
        submit(samples.data(), static_cast<uint32_t>(end - generated));
        generated = end;
    }
    bool command(const std::string& text, Renderer& renderer, SDL_Window* window, const std::string& label) {
        std::istringstream input(text);
        std::string op;
        input >> op;
        bool quit = false;
        if (op == "axis") {
            std::string name; int value;
            if (!(input >> name >> value)) throw std::runtime_error("axis NAME VALUE required");
            auto axis = SDL_GetGamepadAxisFromString(name.c_str());
            if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT || value < -32768 || value > 32767)
                throw std::runtime_error("Invalid axis or value");
            require(SDL_SetJoystickVirtualAxis(joystick, axis, static_cast<Sint16>(value)), "virtual axis");
        } else if (op == "button") {
            std::string name; int value;
            if (!(input >> name >> value)) throw std::runtime_error("button NAME 0|1 required");
            auto which = button(name);
            if (which < 0 || which >= 15 || (value != 0 && value != 1)) throw std::runtime_error("Invalid button or state");
            require(SDL_SetJoystickVirtualButton(joystick, which, value != 0), "virtual button");
        } else if (op == "reset") reset();
        else if (op == "capture") {
            std::string name; input >> name;
            if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
                throw std::runtime_error("Capture name must contain only letters, numbers, underscores and hyphens");
            auto path = directory / "frames" / (name + ".ppm");
            if (fs::exists(path)) throw std::runtime_error("Capture name already exists");
            renderer.capture(path.string()); captures.push_back(path);
        } else if (op == "tone") {
            double seconds, candidate;
            if (!(input >> candidate >> seconds) || !std::isfinite(candidate) || candidate < 20 || candidate > 20000 ||
                !std::isfinite(seconds) || seconds <= 0 || seconds > 60 || !audio || !diagnostics)
                throw std::runtime_error("tone HZ SECONDS requires audio, 20..20000 Hz and 0..60 seconds");
            frequency = candidate;
            phase = 0.0;
            toneUntil = generated + static_cast<Uint64>(seconds * 48000);
        } else if (op == "resize") {
            int width, height;
            if (!(input >> width >> height) || width < 64 || height < 64 || width > 8192 || height > 8192)
                throw std::runtime_error("resize WIDTH HEIGHT requires 64..8192 pixels");
            if (window) {
                if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
                    require(SDL_RestoreWindow(window), "SDL_RestoreWindow");
                require(SDL_SetWindowSize(window, width, height), "SDL_SetWindowSize");
                renderer.resize();
            } else {
                renderer.resizeHeadless({static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
            }
            resizeWidth = width; resizeHeight = height; resizeDeadline = SDL_GetTicks() + 15000;
            event("resize_requested", "\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) +
                  ",\"window_flags\":" + std::to_string(window ? SDL_GetWindowFlags(window) : 0));
        } else if (op == "set") {
            // set KEY VALUE (or KEY=VALUE): a display / image-quality setting, applied live.
            std::string key, value;
            input >> key;
            const auto equals = key.find('=');
            if (equals != std::string::npos) { value = key.substr(equals + 1); key = key.substr(0, equals); }
            else input >> value;
            if (key.empty() || value.empty() || !setting) throw std::runtime_error("set KEY VALUE required");
            if (const std::string error = setting(key, value); !error.empty()) throw std::runtime_error(error);
            event("setting", "\"key\":" + quote(key) + ",\"value\":" + quote(value) +
                  ",\"pipeline\":" + quote(renderer.pipelineDescription()));
        } else if (op == "wait") {
            int count = 0;
            if (!(input >> count) || count < 1 || count > 100000) throw std::runtime_error("wait FRAMES requires 1..100000");
            waitFrames = static_cast<uint32_t>(count);
        } else if (op == "quit") quit = true;
        else throw std::runtime_error("Unknown command");
        std::string extra;
        if (input >> extra) throw std::runtime_error("Unexpected command arguments");
        SDL_UpdateJoysticks();
        event("command", "\"id\":" + quote(label) + ",\"text\":" + quote(text) + ",\"ok\":true");
        return quit;
    }
};

Session::Session(const std::string& directory, bool audio, bool diagnostics) : impl(std::make_unique<Impl>(directory)) { impl->diagnostics = diagnostics; impl->initialize(audio); }
Session::~Session() = default;
bool Session::tick(Renderer& renderer, SDL_Window* window, uint32_t frame) {
    if (impl->resizeWidth) {
        const auto extent = renderer.drawableExtent();
        if (extent.width == static_cast<uint32_t>(impl->resizeWidth) && extent.height == static_cast<uint32_t>(impl->resizeHeight))
            impl->resizeWidth = 0;
        else if (SDL_GetTicks() > impl->resizeDeadline) throw std::runtime_error("Resize acknowledgment timed out");
        else { if (impl->diagnostics) impl->diagnosticAudio(); return true; }
    }
    std::vector<fs::path> pending;
    if (impl->waitFrames) --impl->waitFrames;   // "wait N": render N frames before the next command
    else
        for (const auto& entry : fs::directory_iterator(impl->directory / "commands"))
            if (entry.is_regular_file() && entry.path().extension() == ".cmd") pending.push_back(entry.path());
    std::sort(pending.begin(), pending.end());
    bool quit = false;
    for (const auto& path : pending) {
        if (impl->waitFrames) break;
        std::ifstream input(path); std::string text; std::getline(input, text); input.close();
        try { quit |= impl->command(text, renderer, window, path.filename().string()); }
        catch (const std::exception& error) {
            impl->event("command", "\"id\":" + quote(path.filename().string()) + ",\"ok\":false,\"error\":" + quote(error.what()));
        }
        fs::rename(path, impl->directory / "processed" / path.filename());
        // Let rendering complete captures/resizes before consuming later queued commands.
        if (impl->resizeWidth || !impl->captures.empty()) break;
    }
    for (auto it = impl->captures.begin(); it != impl->captures.end();) {
        if (fs::exists(*it)) {
            impl->event("capture", "\"file\":" + quote(it->filename().string()) + ",\"frame\":" + std::to_string(frame));
            it = impl->captures.erase(it);
        } else ++it;
    }
    if (impl->diagnostics) impl->diagnosticAudio();
    return !quit;
}
float Session::horizontal() const { return SDL_GetGamepadAxis(impl->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32768.0f; }
SDL_Gamepad* Session::gamepad() const { return impl->gamepad; }
bool Session::action() const { return SDL_GetGamepadButton(impl->gamepad, SDL_GAMEPAD_BUTTON_SOUTH); }
void Session::submitAudio(const int16_t* samples, uint32_t frames) { impl->submit(samples, frames); }
void Session::onSetting(std::function<std::string(const std::string&, const std::string&)> handler) {
    impl->setting = std::move(handler);
}
void Session::record(const char* type, const std::string& fields) { impl->event(type, fields); }
void Session::frame(uint32_t number, double milliseconds, double gpuMilliseconds, uint32_t width, uint32_t height) {
    impl->event("frame", "\"frame\":" + std::to_string(number) + ",\"duration_ms\":" + std::to_string(milliseconds) +
                ",\"gpu_ms\":" + std::to_string(gpuMilliseconds) +
                ",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) +
                ",\"leftx\":" + std::to_string(SDL_GetGamepadAxis(impl->gamepad, SDL_GAMEPAD_AXIS_LEFTX)) +
                ",\"cross\":" + std::string(action() ? "true" : "false"));
}
