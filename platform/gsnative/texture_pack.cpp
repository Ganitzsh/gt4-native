// Texture packs (see texture_pack.h).
#include "texture_pack.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace gt4::gsn {

namespace fs = std::filesystem;

namespace {

// Paths as UTF-8 std::strings and back (C++20's u8string is std::u8string).
std::string utf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}
fs::path pathOf(const std::string& utf8Text) { return fs::path(std::u8string(utf8Text.begin(), utf8Text.end())); }

// A run of hex digits at `s` (any width up to 16); advances s.
bool hexField(const char*& s, uint64_t& value) {
    value = 0;
    int digits = 0;
    while (std::isxdigit(static_cast<unsigned char>(*s))) {
        if (++digits > 16) return false;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(*s++)));
        value = (value << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    return digits > 0;
}

bool decField(const char*& s, uint64_t& value) {
    value = 0;
    int digits = 0;
    while (std::isdigit(static_cast<unsigned char>(*s))) {
        if (++digits > 9) return false;
        value = value * 10 + static_cast<uint64_t>(*s++ - '0');
    }
    return digits > 0;
}

bool readFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(pathOf(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0);
    bytes.resize(static_cast<size_t>(size));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(bytes.data()), size));
}

bool hasReplacements(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return false;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        TextureName name;
        if (parseTextureName(utf8(it->path().filename()), name)) return true;
    }
    return false;
}

}  // namespace

bool parseTextureName(const std::string& fileName, TextureName& name) {
    // texhash[-cluthash][-rWxH]-bits.png, hex without leading zeros (texture_name.h); older PCSX2 wrote the
    // region as -r and its 64-bit value in hex (MinX, MaxX, MinY, MaxY, 16 bits each from the bottom). The name
    // ends at the first dot; the extension is what follows the last.
    const size_t dot = fileName.find('.'), last = fileName.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = fileName.substr(last + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != "png") return false;
    std::vector<uint64_t> fields;
    uint64_t regionW = 0, regionH = 0;
    bool region = false;
    const char* s = fileName.c_str();
    const char* end = s + dot;
    while (s < end) {
        if (*s == 'r') {
            if (region) return false;
            region = true;
            const char* at = ++s;
            bool modern = decField(s, regionW) && *s == 'x';
            if (modern) {
                ++s;
                modern = decField(s, regionH) && (s == end || *s == '-');
            }
            if (!modern) {   // the old form
                s = at;
                uint64_t bits;
                if (!hexField(s, bits)) return false;
                const auto field = [&](int shift) { return static_cast<int16_t>(static_cast<uint16_t>(bits >> shift)); };
                regionW = static_cast<uint16_t>(field(16) - field(0));
                regionH = static_cast<uint16_t>(field(48) - field(32));
            }
        } else {
            uint64_t v;
            if (!hexField(s, v)) return false;
            fields.push_back(v);
        }
        if (s == end) break;
        if (*s != '-') return false;
        ++s;
    }
    if (fields.size() < 2 || fields.size() > 3) return false;
    name = {};
    name.texHash = fields[0];
    if (fields.size() == 3) name.clutHash = fields[1];
    if (fields.back() > 0xFFFFFFFFull) return false;
    name.bits = static_cast<uint32_t>(fields.back()) & ~(1u << 14);   // once TEX0.TCC, no longer part of names
    name.regionWidth = static_cast<uint32_t>(regionW);
    name.regionHeight = static_cast<uint32_t>(regionH);
    return true;
}

TexturePack::TexturePack() = default;

TexturePack::~TexturePack() { close(); }

std::string TexturePack::locate(const std::string& configured, const std::string& exeDir, const std::string& settingsDir) {
    if (!configured.empty() && configured != "auto") return hasReplacements(pathOf(configured)) ? configured : std::string();
    std::vector<fs::path> candidates;
    auto add = [&](const fs::path& base) { candidates.push_back(base / "textures" / "SCUS-97328"); };
    if (!exeDir.empty()) add(pathOf(exeDir));
    if (!settingsDir.empty()) add(pathOf(settingsDir));
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA")) add(pathOf(local) / "gt4-native");
    if (const char* profile = std::getenv("USERPROFILE")) add(pathOf(profile) / "Documents" / "PCSX2");
#else
    if (const char* data = std::getenv("XDG_DATA_HOME")) add(pathOf(data) / "gt4-native");
    if (const char* home = std::getenv("HOME")) {
        add(pathOf(home) / ".local" / "share" / "gt4-native");
        add(pathOf(home) / ".config" / "PCSX2");
    }
#endif
    for (const fs::path& c : candidates)
        if (hasReplacements(c)) return utf8(c);
    return {};
}

bool TexturePack::open(const std::string& folder) {
    close();
    std::error_code ec;
    // Of files with one name (a pack's update moved some to other folders), the newest.
    std::unordered_map<TextureName, fs::file_time_type, TextureNameHash> written;
    for (fs::recursive_directory_iterator it(pathOf(folder), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        TextureName name;
        if (!parseTextureName(utf8(it->path().filename()), name)) continue;
        const fs::file_time_type time = it->last_write_time(ec);
        auto [at, added] = written.emplace(name, time);
        if (!added && time <= at->second) continue;
        at->second = time;
        index[name] = utf8(it->path());
    }
    if (index.empty()) return false;
    for (const auto& [name, path] : index) {
        auto& sizes = regions[name.bits];
        const std::pair<uint32_t, uint32_t> size{name.regionWidth, name.regionHeight};
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
    }
    root = folder;
    // Decoded images kept in memory: the GPU keeps its own copy of what is drawn.
    budgetBytes = 1536ull << 20;
    if (const char* e = std::getenv("GT4_TEXTURE_PACK_MB")) budgetBytes = std::strtoull(e, nullptr, 10) << 20;
    stopping = false;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
    for (unsigned i = 0; i < threads; ++i) workers.emplace_back([this] { work(); });
    return true;
}

void TexturePack::close() {
    {
        std::lock_guard<std::mutex> g(lock);
        stopping = true;
        queue.clear();
    }
    wake.notify_all();
    for (std::thread& t : workers) t.join();
    workers.clear();
    std::lock_guard<std::mutex> g(lock);
    index.clear();
    regions.clear();
    cache.clear();
    ages.clear();
    cachedBytes = 0;
    root.clear();
}

std::shared_ptr<const ReplacementImage> TexturePack::get(const TextureName& name) {
    if (!index.count(name)) return nullptr;
    std::lock_guard<std::mutex> g(lock);
    Entry& e = cache[name];
    if (e.image) {
        if (e.aged) ages.erase(e.age);
        ages.push_front(name);
        e.age = ages.begin();
        e.aged = true;
        return e.image;
    }
    if (!e.queued && !e.failed) {
        e.queued = true;
        ++counters.requested;
        queue.push_back(name);
        wake.notify_one();
    }
    return nullptr;
}

TexturePack::Stats TexturePack::stats() const {
    std::lock_guard<std::mutex> g(lock);
    Stats s = counters;
    s.bytes = cachedBytes;
    return s;
}

void TexturePack::work() {
    for (;;) {
        TextureName name;
        std::string path;
        {
            std::unique_lock<std::mutex> g(lock);
            wake.wait(g, [&] { return stopping || !queue.empty(); });
            if (stopping) return;
            name = queue.front();
            queue.pop_front();
            path = index.at(name);
        }
        auto image = std::make_shared<ReplacementImage>();
        std::vector<uint8_t> bytes;
        bool ok = false;
        if (readFile(path, bytes)) {
            int w = 0, h = 0, channels = 0;
            if (stbi_uc* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4)) {
                image->width = static_cast<uint32_t>(w);
                image->height = static_cast<uint32_t>(h);
                image->pixels.resize(size_t(w) * h);
                for (size_t i = 0; i < image->pixels.size(); ++i) {
                    const uint8_t* p = rgba + i * 4;
                    // Alpha as the GS's (PCSX2 dumps and loads it unscaled); an image without alpha is opaque (0x80)
                    const uint32_t a = channels == 4 || channels == 2 ? p[3] : 0x80;
                    image->pixels[i] = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (a << 24);
                }
                stbi_image_free(rgba);
                ok = w > 0 && h > 0;
            }
        }
        std::lock_guard<std::mutex> g(lock);
        if (stopping) return;
        Entry& e = cache[name];
        e.queued = false;
        if (!ok) {
            e.failed = true;
            ++counters.failed;
            std::fprintf(stderr, "texture pack: cannot load %s\n", path.c_str());
            continue;
        }
        cachedBytes += image->pixels.size() * 4;
        e.image = std::move(image);
        ages.push_front(name);
        e.age = ages.begin();
        e.aged = true;
        ++counters.loaded;
        evict();
        ++loaded;
    }
}

// Least recently used images out until the cache fits its budget (textures the GPU holds stay drawn).
void TexturePack::evict() {
    while (cachedBytes > budgetBytes && ages.size() > 1) {
        const TextureName oldest = ages.back();
        ages.pop_back();
        auto it = cache.find(oldest);
        if (it == cache.end()) continue;
        cachedBytes -= it->second.image ? it->second.image->pixels.size() * 4 : 0;
        cache.erase(it);
    }
}

}  // namespace gt4::gsn
