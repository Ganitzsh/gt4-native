#pragma once
// High-resolution replacement textures from a texture pack in PCSX2's format (docs/TEXTURE_PACKS.md): PNG
// files named after a hash of the texture as it sits in GS memory (texture_name.h), in any subfolders of
// the pack's folder. The pack is indexed by name at start-up; images load on a worker thread when the game
// first uses their texture (until then the original is drawn) and stay decoded within a memory budget.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gt4::gsn {

// What a replacement's file name says (texture_name.h computes it for a texture in GS memory).
struct TextureName {
    uint64_t texHash = 0, clutHash = 0;   // clutHash 0: no palette part
    uint32_t bits = 0;                    // TEX0 / TEXA bits (texture_name.h)
    uint32_t regionWidth = 0, regionHeight = 0;   // 0 x 0: the whole texture
    bool operator==(const TextureName&) const = default;
};
struct TextureNameHash {
    size_t operator()(const TextureName& n) const {
        uint64_t h = n.texHash * 0x9E3779B97F4A7C15ull ^ n.clutHash;
        h = (h ^ (h >> 29)) * 0xBF58476D1CE4E5B9ull ^ n.bits ^ (uint64_t(n.regionWidth) << 32) ^ (uint64_t(n.regionHeight) << 48);
        return static_cast<size_t>(h ^ (h >> 32));
    }
};
// Parses a file name (without folders); false when it is not a replacement's.
bool parseTextureName(const std::string& fileName, TextureName& name);

struct ReplacementImage {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;   // RGBA8, alpha as the GS's (0x80 = 1.0), as PCSX2 dumps it
};

class TexturePack {
public:
    TexturePack();
    ~TexturePack();
    TexturePack(const TexturePack&) = delete;
    TexturePack& operator=(const TexturePack&) = delete;

    // The pack's folder: `configured` unless "auto" or empty, else the first of the usual places that has
    // replacements (textures/SCUS-97328 next to `exeDir` and in `settingsDir`, the local application data
    // folder, PCSX2's textures folder). Empty when there is none.
    static std::string locate(const std::string& configured, const std::string& exeDir, const std::string& settingsDir);

    // Indexes the folder's replacements (recursively). False when it has none.
    bool open(const std::string& folder);
    void close();
    bool isOpen() const { return !index.empty(); }
    size_t count() const { return index.size(); }
    const std::string& folder() const { return root; }

    // Whether the pack has a replacement for the name.
    bool has(const TextureName& name) const { return index.count(name) != 0; }
    // The region sizes (width, height; 0 x 0 for none) of the pack's names with these TEX0 / TEXA bits; null
    // when it has none.
    const std::vector<std::pair<uint32_t, uint32_t>>* regionsFor(uint32_t bits) const {
        auto it = regions.find(bits);
        return it == regions.end() ? nullptr : &it->second;
    }
    // The replacement's image when it is loaded; null while it loads (the first call queues it) or when it
    // failed to load.
    std::shared_ptr<const ReplacementImage> get(const TextureName& name);
    // Images that finished loading since the last call (their textures can be decoded again).
    uint64_t loadedSerial() const { return loaded.load(); }

    struct Stats {
        uint64_t requested = 0, loaded = 0, failed = 0, bytes = 0;
    };
    Stats stats() const;

private:
    std::string root;
    std::unordered_map<TextureName, std::string, TextureNameHash> index;   // name -> path
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> regions;   // bits -> region sizes

    struct Entry {
        std::shared_ptr<const ReplacementImage> image;
        bool queued = false, failed = false;
        std::list<TextureName>::iterator age;
        bool aged = false;
    };
    mutable std::mutex lock;
    std::condition_variable wake;
    std::unordered_map<TextureName, Entry, TextureNameHash> cache;
    std::list<TextureName> ages;   // most recently used first
    std::deque<TextureName> queue;
    std::vector<std::thread> workers;
    bool stopping = false;
    std::atomic<uint64_t> loaded{0};
    uint64_t cachedBytes = 0, budgetBytes = 0;
    Stats counters;
    void work();
    void evict();
};

}  // namespace gt4::gsn
