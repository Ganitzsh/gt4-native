// The memory cards behind libmc as host folders (save_folder.h).
#include "save_folder.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gt4::mc {

namespace fs = std::filesystem;

namespace {

bool hidden(const std::string &name) { return !name.empty() && name[0] == '.'; }

std::string text(const fs::path &p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

McDateTime to_ps2_time(const fs::path &p) {
    // the host file's modification time as calendar time in JST, as the PS2 clock keeps it (UTC + 9)
    std::time_t tt = 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &a)) return {};
    const uint64_t ft = (static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    tt = static_cast<std::time_t>(ft / 10000000ULL - 11644473600ULL);
#else
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return {};
    tt = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(t));
#endif
    tt += 9 * 3600;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    McDateTime d{};
    d.sec = static_cast<uint8_t>(tm.tm_sec);
    d.min = static_cast<uint8_t>(tm.tm_min);
    d.hour = static_cast<uint8_t>(tm.tm_hour);
    d.day = static_cast<uint8_t>(tm.tm_mday);
    d.month = static_cast<uint8_t>(tm.tm_mon + 1);
    d.year = static_cast<uint16_t>(tm.tm_year + 1900);
    return d;
}

void fill_entry(McDirEntry &e, const std::string &name, const fs::path &p) {
    std::memset(&e, 0, sizeof e);
    std::error_code ec;
    const bool dir = fs::is_directory(p, ec);
    e.created = e.modified = to_ps2_time(p);
    e.size = dir ? 0 : static_cast<uint32_t>(fs::file_size(p, ec));
    e.attr = dir ? 0x8427 : 0x8497;  // exists, readable, writable, executable, (sub)directory or file
    std::snprintf(e.name, sizeof e.name, "%s", name.c_str());
}

// "*" and "?" in the last part of a GetDir pattern
bool match(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') return match(pat + 1, s) || (*s && match(pat, s + 1));
    if (*pat == '?' || std::tolower(static_cast<unsigned char>(*pat)) == std::tolower(static_cast<unsigned char>(*s)))
        return *s && match(pat + 1, s + 1);
    return false;
}

FILE *open_file(const fs::path &p, const char *how) {
#ifdef _WIN32
    return _wfopen(p.wstring().c_str(), std::wstring(how, how + std::strlen(how)).c_str());
#else
    return std::fopen(p.c_str(), how);
#endif
}

// The file's bytes on the disk, not only in the C library's or the system's buffers.
bool sync_file(FILE *f) {
    if (std::fflush(f) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

// `from` replaces `to` in one step: `to` is either the old file or the new one, never a part of either.
bool replace_file(const fs::path &from, const fs::path &to) {
#ifdef _WIN32
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
#endif
}

std::string stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
    return b;
}

bool is_temp(const std::string &name) {
    return hidden(name) && name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
}

}  // namespace

void Cards::say(const std::string &line) const {
    if (log) log(line.c_str());
}

void Cards::insert(int port, const fs::path &folder, int backups) {
    if (port < 0 || port >= kPorts) return;
    card[port] = Card{};
    card[port].root = folder;
    card[port].backups = std::max(0, backups);
    if (folder.empty()) return;
    std::error_code ec;
    fs::create_directories(folder, ec);
    // what a session that ended inside a save left: copies never renamed over their file, a backup half made
    std::vector<fs::path> stale;
    for (const auto &top : fs::directory_iterator(folder, ec)) {
        const std::string n = text(top.path().filename());
        if (is_temp(n)) stale.push_back(top.path());
        if (!top.is_directory(ec)) continue;
        std::error_code ec2;
        if (!hidden(n)) {
            for (const auto &f : fs::directory_iterator(top.path(), ec2))
                if (is_temp(text(f.path().filename()))) stale.push_back(f.path());
        } else if (n == ".backup") {
            for (const auto &f : fs::directory_iterator(top.path(), ec2))
                if (hidden(text(f.path().filename()))) stale.push_back(f.path());
        }
    }
    for (const fs::path &p : stale) {
        fs::remove_all(p, ec);
        say("saves: removed " + text(p) + ", left by a session that ended during a save");
    }
}

int Cards::newCardResult(int port) {
    Card &c = card[port];
    const int r = c.reportedNew ? 0 : -1;
    c.reportedNew = true;
    return r;
}

int Cards::freeClusters(int port) {
    uint64_t kb = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(card[port].root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (hidden(text(it->path().filename()))) {
            it.disable_recursion_pending();
            continue;
        }
        kb += 1;  // every entry takes a cluster
        if (it->is_regular_file(ec)) kb += (it->file_size(ec) + 1023) / 1024;
    }
    return static_cast<int>(std::max<int64_t>(0, kCardClusters - static_cast<int64_t>(kb)));
}

// a card path ("/DIR/FILE", "FILE" relative to the current directory); "" if it names something hidden, or holds a
// backslash or a colon (the host's separators and drive letters: hostPath would join them, and they could leave
// the card; a PS2 card name never has them)
std::string Cards::cardPath(const Card &c, const char *name) const {
    std::string p = name ? name : "";
    if (p.find_first_of("\\:") != std::string::npos) return "";
    if (p.empty() || p[0] != '/') p = c.cwd + (c.cwd.back() == '/' ? "" : "/") + p;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        const size_t j = p.find('/', i);
        const std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (hidden(part)) {
            if (part != ".") return "";
        } else if (!part.empty()) {
            parts.push_back(part);
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::string out = "/";
    for (size_t k = 0; k < parts.size(); ++k) out += (k ? "/" : "") + parts[k];
    return out;
}

fs::path Cards::hostPath(const Card &c, const std::string &cp) const { return c.root / fs::u8path(cp.substr(1)); }

void Cards::backUp(int port) {
    Card &c = card[port];
    if (c.backedUp || c.backups <= 0) return;
    c.backedUp = true;
    std::error_code ec;
    std::vector<fs::path> saves;
    for (const auto &e : fs::directory_iterator(c.root, ec))
        if (!hidden(text(e.path().filename()))) saves.push_back(e.path());
    if (saves.empty()) return;
    const fs::path dir = c.root / ".backup";
    // The name sorts after every session backup there, so the rotation (by name) never takes the new one: in
    // the same second as earlier ones, a suffix after theirs (a name the rotation freed would sort first).
    std::string latest;
    for (const auto &e : fs::directory_iterator(dir, ec)) {
        const std::string n = text(e.path().filename());
        if (std::isdigit(static_cast<unsigned char>(n[0]))) latest = std::max(latest, n);
    }
    const std::string when = stamp();
    std::string name = when;
    for (int n = 2; (name <= latest || fs::exists(dir / name, ec)) && n < 100; ++n) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, "-%02d", n);
        name = when + suffix;
    }
    const fs::path dest = dir / name;
    const fs::path partial = dir / ("." + text(dest.filename()));  // hidden until whole
    fs::create_directories(partial, ec);
    for (const fs::path &s : saves) {
        if (ec) break;
        fs::copy(s, partial / s.filename(), fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    }
    if (!ec) fs::rename(partial, dest, ec);
    if (ec) {
        say("saves: no backup of " + text(c.root) + ": " + ec.message());
        std::error_code ignore;
        fs::remove_all(partial, ignore);
        return;
    }
    c.backup = dest;
    say("saves: backed up to " + text(dest));
    std::vector<fs::path> all;
    for (const auto &e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec) && std::isdigit(static_cast<unsigned char>(text(e.path().filename())[0])))
            all.push_back(e.path());  // session backups only: the saves an import replaced (imports/) stay
    // The oldest go, by name (the names are their times); never the one just made, even when the clock has gone back
    // since an earlier backup (a time step, DST) and its name sorts first.
    std::erase_if(all, [&](const fs::path &p) { return p == dest; });
    std::sort(all.begin(), all.end());
    for (size_t k = 0; k + static_cast<size_t>(c.backups) < all.size() + 1; ++k) fs::remove_all(all[k], ec);
}

int Cards::open(int port, const char *name, int mode) {
    if (!present(port)) return kNoCard;
    Card &c = card[port];
    const std::string cp = cardPath(c, name);
    if (cp.empty()) return kNoEntry;
    const fs::path p = hostPath(c, cp);
    std::error_code ec;
    if (fs::is_directory(p, ec)) return kDenied;
    const bool exists = fs::exists(p, ec);
    if (!exists && !(mode & kOpenCreate)) return kNoEntry;
    if (!fs::is_directory(p.parent_path(), ec)) return kNoEntry;
    int fd = -1;
    for (int i = 0; i < kFiles; ++i)
        if (!files[i].f) {
            fd = i;
            break;
        }
    if (fd < 0) return kTooMany;
    File f;
    f.port = port;
    f.path = p;
    f.writing = !exists || (mode & kOpenWrite);
    if (!f.writing) {
        f.f = open_file(p, "rb");
    } else {
        backUp(port);
        // the writes go to a copy, which replaces the file at close
        f.temp = p.parent_path() / fs::u8path("." + text(p.filename()) + "." + std::to_string(fd) + ".tmp");
        if (exists && !fs::copy_file(p, f.temp, fs::copy_options::overwrite_existing, ec)) return kDenied;
        f.f = open_file(f.temp, exists ? "r+b" : "w+b");
        f.written = !exists;  // a new file appears at close even if nothing is written to it
    }
    if (!f.f) return kDenied;
    files[fd] = f;
    return fd;
}

int Cards::close(int fd) {
    File *f = file(fd);
    if (!f) return kNoEntry;
    int result = 0;
    std::error_code ec;
    if (f->writing && f->written) {
        const bool synced = sync_file(f->f);
        std::fclose(f->f);
        if (!synced || !replace_file(f->temp, f->path)) {
            say("saves: could not write " + text(f->path) + "; the file is unchanged");
            fs::remove(f->temp, ec);
            result = kDenied;
        }
    } else {
        std::fclose(f->f);
        if (f->writing) fs::remove(f->temp, ec);
    }
    *f = File{};
    return result;
}

int Cards::seek(int fd, int offset, int origin) {
    File *f = file(fd);
    if (!f) return kNoEntry;
    std::fseek(f->f, offset, origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET);
    return static_cast<int>(std::ftell(f->f));
}

int Cards::read(int fd, void *buf, int size) {
    File *f = file(fd);
    if (!f) return kNoEntry;
    return static_cast<int>(std::fread(buf, 1, static_cast<size_t>(size), f->f));
}

int Cards::write(int fd, const void *buf, int size) {
    File *f = file(fd);
    if (!f) return kNoEntry;
    if (!f->writing) return 0;  // as fwrite on a file opened for reading
    f->written = true;
    return static_cast<int>(std::fwrite(buf, 1, static_cast<size_t>(size), f->f));
}

int Cards::flush(int fd) {
    if (File *f = file(fd)) std::fflush(f->f);  // the copy; the file itself changes at close
    return 0;
}

int Cards::mkdir(int port, const char *name) {
    if (!present(port)) return kNoCard;
    const std::string cp = cardPath(card[port], name);
    if (cp.empty() || cp == "/") return kNoEntry;
    const fs::path p = hostPath(card[port], cp);
    std::error_code ec;
    if (fs::exists(p, ec)) return kNoEntry;  // libmc: the entry already exists
    backUp(port);
    return fs::create_directory(p, ec) ? 0 : kNoEntry;
}

int Cards::chdir(int port, const char *name, std::string *previous) {
    if (!present(port)) return kNoCard;
    Card &c = card[port];
    if (previous) *previous = c.cwd;
    const std::string cp = cardPath(c, name);
    std::error_code ec;
    if (cp.empty() || !fs::is_directory(hostPath(c, cp), ec)) return kNoEntry;
    c.cwd = cp;
    return 0;
}

int Cards::getdir(int port, const char *name, unsigned mode, int maxent, McDirEntry *table) {
    if (!present(port)) return kNoCard;
    Card &c = card[port];
    if (mode == 0) {  // a new listing
        c.listing.clear();
        c.next = 0;
        const std::string cp = cardPath(c, name);
        if (cp.empty()) return kNoEntry;
        const size_t slash = cp.rfind('/');
        const std::string dir = cp.substr(0, slash ? slash : 1), pattern = cp.substr(slash + 1);
        const fs::path hp = hostPath(c, dir);
        std::error_code ec;
        if (!fs::is_directory(hp, ec)) return kNoEntry;
        McDirEntry e;
        if (dir != "/" && (pattern == "*" || pattern.empty())) {  // as on a card: "." and ".." first
            fill_entry(e, ".", hp);
            c.listing.push_back(e);
            fill_entry(e, "..", hp.parent_path());
            c.listing.push_back(e);
        }
        for (const auto &it : fs::directory_iterator(hp, ec)) {
            const std::string n = text(it.path().filename());  // UTF-8, as every other name here
            if (!hidden(n) && match(pattern.empty() ? "*" : pattern.c_str(), n.c_str())) {
                fill_entry(e, n, it.path());
                c.listing.push_back(e);
            }
        }
    }
    int n = 0;
    while (n < maxent && c.next < c.listing.size()) table[n++] = c.listing[c.next++];
    return n;
}

int Cards::remove(int port, const char *name) {
    if (!present(port)) return kNoCard;
    const std::string cp = cardPath(card[port], name);
    if (cp.empty() || cp == "/") return kNoEntry;
    const fs::path p = hostPath(card[port], cp);
    std::error_code ec;
    if (!fs::exists(p, ec)) return kNoEntry;
    if (fs::is_directory(p, ec) && !fs::is_empty(p, ec)) return kNotEmpty;
    backUp(port);
    return fs::remove(p, ec) ? 0 : kDenied;
}

int Cards::rename(int port, const char *from, const char *to) {
    if (!present(port)) return kNoCard;
    const std::string cp = cardPath(card[port], from);
    if (cp.empty() || cp == "/") return kNoEntry;
    const fs::path a = hostPath(card[port], cp);
    std::error_code ec;
    if (!fs::exists(a, ec)) return kNoEntry;
    const std::string target = to ? to : "";
    if (target.empty() || hidden(target) || target.find('/') != std::string::npos) return kDenied;
    backUp(port);
    fs::rename(a, a.parent_path() / fs::u8path(target), ec);
    return ec ? kDenied : 0;
}

void Cards::abandon() {
    for (File &f : files) {
        if (f.f) std::fclose(f.f);  // the copy stays on the disk, as a session that ends here leaves it
        f = File{};
    }
}

}  // namespace gt4::mc
