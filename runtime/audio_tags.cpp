// GT4's voices tagged with their car (audio_tags.h; docs/modern/spikes/D9-voices-to-entrants.md).
#include "audio_tags.h"

#include "guest.h"
#include "kernel.h"
#include "log.h"
#include "vehicle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace gt4::rt {
namespace {

// RaceCarSound (RaceEntryCar + 0x2900): its sound groups, each an H block (the listener-relative position GT4 keeps,
// func_00391AF0) and the objects that play in it. Offsets from the decomp's knowledge/race-car-sound.md.
constexpr uint32_t kCarSound = 0x2900;
constexpr uint32_t kVehicleObject = 0x18;   // RaceEntryCar + 0x18: the car's vehicle object
struct Group {
    uint32_t h;          // the H block
    uint32_t engine[2];  // engine pool entry pointers in RaceCarSound (0: none)
    uint32_t players;    // the first SePlayer (0x20 bytes each), 0: none
    uint32_t count;      // SePlayers
};
constexpr Group kGroups[] = {
    {0x14, {0x28, 0x2C}, 0x278, 2},
    {0x2B8, {0x2CC, 0}, 0, 0},
    {0x3F4, {0, 0}, 0x408, 3},
    {0x474 + 0 * 0x1A0, {0, 0}, 0x488 + 0 * 0x1A0, 11},
    {0x474 + 1 * 0x1A0, {0, 0}, 0x488 + 1 * 0x1A0, 11},
    {0x474 + 2 * 0x1A0, {0, 0}, 0x488 + 2 * 0x1A0, 11},
    {0x474 + 3 * 0x1A0, {0, 0}, 0x488 + 3 * 0x1A0, 11},
};
constexpr int kGroupCount = static_cast<int>(sizeof kGroups / sizeof kGroups[0]);
constexpr uint32_t kSePlayerBytes = 0x20, kPlayback[2] = {0x14, 0x18};   // a SePlayer's playback objects

// GT4's voices: SDDRV__VoiceSystem__master_, 48 records of 0xE0 bytes from +0xA8.
constexpr uint32_t kVoiceSystem = 0x006518C8, kVoiceRecords = 0xA8, kVoiceBytes = 0xE0, kVoices = 48;
constexpr uint32_t kVoiceOwner = 0x10, kVoiceHardware = 0x18, kVoiceClass = 0x30;   // (+0x18: s16, func_0055D6F0)

// The registered cars: their sound objects (owners a voice can name), by car. Refreshed every frame by
// func_003B5E80; a car not refreshed for kStale frames is dropped. An owner stays registered kOwnerGrace frames after
// it was last seen in its SePlayer: a sound restarted every few frames is often between two playback objects when the
// car registers, while the sound thread keys it on.
constexpr int kCars = 8, kOwnersPerCar = 192, kStale = 30, kOwnerGrace = 10;
struct Owner {
    uint32_t address;
    uint8_t group;
    uint32_t seen;   // the frame it was last in its SePlayer
};
struct Car {
    uint32_t entryCar = 0;   // RaceEntryCar, 0 a free slot
    uint32_t frame = 0;      // the frame it was last registered
    uint8_t entrant = 0xFF;
    int owners = 0;
    Owner owner[kOwnersPerCar];
    bool hasRel = false;          // (GT4_TRACE_D9) the car against the player's, in the player car's axes
    float rel[3] = {}, world[3] = {};
};
Car g_cars[kCars];
std::mutex g_mutex;

const bool g_trace = [] {   // (set and not 0, as the other switches)
    const char *e = std::getenv("GT4_TRACE_D9");
    return e && *e && *e != '0';
}();
uint64_t g_conflicts = 0;   // an owner two cars saw in the same frame (under g_mutex)

// The car's entrant: its index in the conductor's car array, if its vehicle object is one of those entries.
uint8_t entrant_of(uint32_t entryCar) {
    const uint32_t vehicle = guest_ref<uint32_t>(entryCar + kVehicleObject);
    const uint32_t conductor = vehicle_conductor();
    if (!conductor || vehicle < conductor + car::kCarsOffset) return 0xFF;
    const uint32_t offset = vehicle - (conductor + car::kCarsOffset);
    if (offset % car::kCarStride || offset / car::kCarStride >= car::kMaxCars) return 0xFF;
    return static_cast<uint8_t>(offset / car::kCarStride);
}

bool position_of(uint32_t entryCar, uint8_t group, float out[3]) {
    const uint32_t h = entryCar + kCarSound + kGroups[group].h;
    if (!in_guest_ram(h, 0x14)) return false;
    for (int i = 0; i < 3; ++i) out[i] = guest_ref<float>(h + 4 + 4 * i);
    return true;
}

// (GT4_TRACE_D9 only) RaceCourseSound, at RacePS2Base + 0xE170 (RacePS2Base__soundStart, __updateRace). Its
// playUpdate (00398B70) plays the player's own wheels on the course's surfaces, at the listener, with L and R GT4 has
// already panned: no car sound to position, so it stays untagged, in the bed (PLAN D9's tag rates). Its SePlayers:
// five group players at +4 + i * 0x24 (enabled at +0x20), five more at +0xB8 .. +0x148, their playback objects at
// +0x14 / +0x18. RacePS2Base is found from a car: + 0x6C, then + 0x60, is the car list {count, pad, items}.
constexpr uint32_t kCourseSound = 0xE170, kRaceCarsObject = 0x6C, kCarList = 0x60;
constexpr uint32_t kCoursePlayers[] = {0x04, 0x28, 0x4C, 0x70, 0x94, 0xB8, 0xDC, 0x100, 0x124, 0x148};
constexpr uint32_t kGuestLow = 0x00100000, kGuestHigh = 0x02000000;
// a RacePS2Base's vtable (+0x64, in the ELF's .data): its +0xD0 entry, which __updateRace calls for the camera, is
// RaceBase__getGT4CameraManager (other objects point at the same car list, one with RacePS2Base__idle there). gcc
// 2.9's vtable entries are 8 bytes, {s16 delta, s16 index, function}: the function is at +0xD0 + 4.
constexpr uint32_t kRaceVtable = 0x64, kDataLow = 0x00617A80, kDataHigh = 0x006D5DFC,
                   kGetCameraManager = 0x00387E38;

// whether `base` is a RacePS2Base whose car list holds `entryCar`
bool race_holds(uint32_t base, uint32_t entryCar) {
    if (!in_guest_ram(base, kCourseSound + 0x170)) return false;
    const uint32_t cars = guest_ref<uint32_t>(base + kRaceCarsObject);
    if (!in_guest_ram(cars, kCarList + 4)) return false;
    const uint32_t list = guest_ref<uint32_t>(cars + kCarList);
    if (!in_guest_ram(list, 12)) return false;
    const int32_t count = guest_ref<int32_t>(list);
    const uint32_t items = guest_ref<uint32_t>(list + 8);
    if (count < 1 || count > 16 || !in_guest_ram(items, 4u * static_cast<uint32_t>(count))) return false;
    for (int32_t i = 0; i < count; ++i)
        if (guest_ref<uint32_t>(items + 4 * i) == entryCar) return true;
    return false;
}

// the addresses of the guest words in [lo[k], lo[k] + span] for some k (at most `max`), lo[] holding `n` values
int find_words(const uint32_t lo[], int n, uint32_t span, uint32_t out[], int max) {
    int found = 0;
    for (uint32_t a = kGuestLow; a < kGuestHigh && found < max; a += 4) {
        const uint32_t w = guest_ref<uint32_t>(a);
        for (int k = 0; k < n; ++k)
            if (w - lo[k] <= span && !((w - lo[k]) & 3)) {
                out[found++] = a;
                break;
            }
    }
    return found;
}

// RacePS2Base for a registered car, by the pointers back from it: the car list's items, the list, the cars object,
// the race. Four scans of the EE's 32 MB (about 10 ms each), once a race, at most every 300 frames while not found:
// a ~40 ms stall of the SPUP thread each time, in trace mode only. The cache holds while any car of the race's list
// asks (the first live car can change). Its statics are the SPUP thread's: voice_tags_trace is the only caller.
uint32_t race_of(uint32_t entryCar) {
    static uint32_t base = 0, triedFrame = 0;
    static bool tried = false;
    if (base && race_holds(base, entryCar)) return base;
    base = 0;
    const uint32_t now = kernel_frame_count();
    if (tried && now - triedFrame < 300) return 0;
    tried = true;
    triedFrame = now;
    constexpr int kMax = 64;   // (the last run saw 16 words pointing at the cars object)
    uint32_t a[kMax], items[kMax], lists[kMax], cars[kMax], bases[kMax];
    const int na = find_words(&entryCar, 1, 0, a, kMax);            // items[k] == entryCar
    for (int i = 0; i < na; ++i) items[i] = a[i] - 15 * 4;          // items: up to 15 entries before it
    const int nl = find_words(items, na, 15 * 4, lists, kMax);      // list + 8 == items
    for (int i = 0; i < nl; ++i) lists[i] -= 8;
    const int nc = find_words(lists, nl, 0, cars, kMax);           // cars + 0x60 == list
    for (int i = 0; i < nc; ++i) cars[i] -= kCarList;
    const int nb = find_words(cars, nc, 0, bases, kMax);           // race + 0x6C == cars
    for (int i = 0; i < nb; ++i) {
        const uint32_t b = bases[i] - kRaceCarsObject;
        const uint32_t vt = in_guest_ram(b + kRaceVtable, 4) ? guest_ref<uint32_t>(b + kRaceVtable) : 0;
        const uint32_t fn = vt >= kDataLow && vt < kDataHigh ? guest_ref<uint32_t>(vt + 0xD0 + 4) : 0;
        if (!base && fn == kGetCameraManager && race_holds(b, entryCar)) {
            base = b;
        }
    }
    if (base)
        log_info("d9: RacePS2Base %08x (RaceCourseSound %08x), from car %08x", base, base + kCourseSound, entryCar);
    return base;
}

// whether a voice's owner is one of RaceCourseSound's playback objects
bool course_sound_owns(uint32_t owner) {
    uint32_t entryCar = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const uint32_t now = kernel_frame_count();
        for (const Car &c : g_cars)
            if (c.entryCar && now - c.frame <= kStale) {
                entryCar = c.entryCar;
                break;
            }
    }
    const uint32_t race = entryCar ? race_of(entryCar) : 0;
    if (!race) return false;
    for (uint32_t p : kCoursePlayers)
        for (uint32_t pb : kPlayback)
            if (guest_ref<uint32_t>(race + kCourseSound + p + pb) == owner) return true;
    return false;
}

}  // namespace

int voice_tags(VoiceTag out[48]) {
    // every voice untagged first: a hardware voice no record claims stays so (lane C's review)
    for (uint32_t i = 0; i < kVoices; ++i) out[i] = VoiceTag{0xFF, 0xFF, 0, 0, {0, 0, 0}};
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t now = kernel_frame_count();
    int tagged = 0;
    for (uint32_t i = 0; i < kVoices; ++i) {
        const uint32_t rec = kVoiceSystem + kVoiceRecords + i * kVoiceBytes;
        const uint32_t hardware = static_cast<uint32_t>(guest_ref<uint16_t>(rec + kVoiceHardware));
        const uint32_t owner = guest_ref<uint32_t>(rec + kVoiceOwner);
        if (hardware >= kVoices || guest_ref<uint64_t>(rec) != 1ull << hardware || !owner) continue;   // (0055D6F0)
        // the car that saw this owner last: a pooled playback object that moved from one car to another within the
        // grace window is the newer car's (a same-frame tie, two cars at once, is counted: g_conflicts)
        const Car *best = nullptr;
        const Owner *bestOwner = nullptr;
        for (const Car &c : g_cars) {
            if (!c.entryCar || now - c.frame > kStale) continue;
            for (int k = 0; k < c.owners; ++k)
                if (c.owner[k].address == owner) {
                    if (!bestOwner || c.owner[k].seen > bestOwner->seen) {
                        best = &c;
                        bestOwner = &c.owner[k];
                    } else if (c.owner[k].seen == bestOwner->seen && best != &c) {
                        ++g_conflicts;
                    }
                    break;
                }
        }
        if (!best) continue;
        VoiceTag &t = out[hardware];
        t.entrant = best->entrant;
        t.group = bestOwner->group;
        t.flags = position_of(best->entryCar, t.group, t.position) ? 1 : 0;
        ++tagged;
    }
    return tagged;
}
void voice_tags_trace(const uint8_t *image) {
    if (!g_trace) return;
    // key-ons in this image, by hardware voice (core c's mask at c * 0x1D0 + 0x1B4, flag 0x800)
    uint64_t keyed = 0;
    for (int c = 0; c < 2; ++c) {
        uint32_t flags, mask;
        std::memcpy(&flags, image + c * 0x1D0, 4);
        std::memcpy(&mask, image + c * 0x1D0 + 0x1B4, 4);
        if (flags & 0x800) keyed |= static_cast<uint64_t>(mask & 0xFFFFFF) << (24 * c);
    }
    VoiceTag tags[48];
    std::memset(tags, 0xFF, sizeof tags);
    voice_tags(tags);
    // the voices' sample starts (SSA), kept from the images as the SPU2 keeps them: the car samples are the SSAs a
    // tagged voice has played; an untagged key-on of one of them is a car sound missed (D9's denominator)
    static uint32_t ssa[48] = {};
    for (int c = 0; c < 2; ++c) {
        uint32_t mask;
        std::memcpy(&mask, image + c * 0x1D0 + 4, 4);
        for (int n = 0; n < 24; ++n) {
            if (!(mask >> n & 1)) continue;
            const uint8_t *r = image + c * 0x1D0 + 8 + 16 * n;
            uint16_t f;
            std::memcpy(&f, r, 2);
            if (f & 0x08) std::memcpy(&ssa[24 * c + n], r + 8, 4);
        }
    }
    static uint32_t carSamples[1024];
    static int carSampleCount = 0;
    static uint64_t konCarSampleUntagged = 0, konCarSampleTagged = 0, konCourse = 0;
    static uint32_t preReg[64];   // the untagged engine key-ons' samples
    static int preRegCount = 0;
    auto isCarSample = [&](uint32_t s) {
        for (int k = 0; k < carSampleCount; ++k)
            if (carSamples[k] == s) return true;
        return false;
    };
    for (int hw = 0; hw < 48; ++hw) {
        if (!(keyed >> hw & 1)) continue;
        // every key-on, for the offline count against the banks loaded (the d9: spu load lines), with the sound
        // request it plays: its voice's owner (a playback object) and the sound-table entry the owner points to
        // (+8), whose words and text say which sound it is
        uint32_t owner = 0, entry = 0;
        for (uint32_t i = 0; i < kVoices; ++i) {
            const uint32_t rec = kVoiceSystem + kVoiceRecords + i * kVoiceBytes;
            if (static_cast<uint32_t>(guest_ref<uint16_t>(rec + kVoiceHardware)) == static_cast<uint32_t>(hw) &&
                guest_ref<uint64_t>(rec) == 1ull << hw) {
                owner = guest_ref<uint32_t>(rec + kVoiceOwner);
                break;
            }
        }
        if (owner && in_guest_ram(owner, 16)) entry = guest_ref<uint32_t>(owner + 8);
        char text[33] = {};
        uint32_t words[4] = {};
        if (entry && in_guest_ram(entry, 32)) {
            std::memcpy(words, guest_ptr(entry), 16);
            for (int k = 0; k < 32; ++k) {
                const char ch = guest_ref<char>(entry + k);
                text[k] = ch >= 32 && ch < 127 ? ch : '.';
            }
        }
        static const bool every = std::getenv("GT4_TRACE_D9")[0] == '2';   // (=2: every key-on, for the offline counts)
        // RaceCourseSound's key-ons (the player's own wheels on the course, at the listener, panned by GT4): untagged
        // by design, in the bed, and left out of D9's car-effects rate (PLAN D9's tag rates)
        const bool course = tags[hw].group == 0xFF && owner && course_sound_owns(owner);
        if (course) ++konCourse;
        if (every)
            log_info("d9: kon %u ssa %06x %s owner %08x entry %08x [%08x %08x %08x %08x] %s", kernel_frame_count(),
                     ssa[hw], tags[hw].group != 0xFF ? "tagged" : course ? "course" : "untagged", owner, entry,
                     words[0], words[1], words[2], words[3], text);
        if (tags[hw].group != 0xFF) {
            ++konCarSampleTagged;
            if (!isCarSample(ssa[hw]) && carSampleCount < 1024) carSamples[carSampleCount++] = ssa[hw];
        } else if (!course && isCarSample(ssa[hw])) {
            ++konCarSampleUntagged;
        }
    }
    static uint64_t konAll = 0, konTagged = 0, konByClass[8] = {}, konTaggedByClass[8] = {};
    for (uint32_t i = 0; i < kVoices; ++i) {
        const uint32_t rec = kVoiceSystem + kVoiceRecords + i * kVoiceBytes;
        const uint32_t hardware = static_cast<uint32_t>(guest_ref<uint16_t>(rec + kVoiceHardware));
        if (hardware >= kVoices || guest_ref<uint64_t>(rec) != 1ull << hardware || !(keyed >> hardware & 1)) continue;
        const uint32_t cls = guest_ref<uint32_t>(rec + kVoiceClass) & 7;
        ++konAll;
        ++konByClass[cls];
        static int missed = 0;
        if (cls == 4 && tags[hardware].group == 0xFF && missed < 64) {   // an engine voice not tagged: whose is it
            ++missed;
            char cars[256];
            int n = 0;
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const Car &c : g_cars)
                if (c.entryCar && n < 220)
                    n += std::snprintf(cars + n, sizeof cars - n, " %d:%u ago[%08x %08x %08x]", c.entrant,
                                       kernel_frame_count() - c.frame, c.owners > 0 ? c.owner[0].address : 0,
                                       c.owners > 1 ? c.owner[1].address : 0, c.owners > 2 ? c.owner[2].address : 0);
            if (preRegCount < 64) preReg[preRegCount++] = ssa[hardware];   // (checked against the car samples later)
            log_info("d9: untagged engine key-on, voice %u owner %08x ssa %06x; cars:%s", hardware,
                     guest_ref<uint32_t>(rec + kVoiceOwner), ssa[hardware], n ? cars : " none");
        }
        if (tags[hardware].group != 0xFF) {
            ++konTagged;
            ++konTaggedByClass[cls];
        }
    }
    static uint32_t last = 0;
    const uint32_t now = kernel_frame_count();
    if (now - last < 300) return;
    last = now;
    log_info("d9: key-ons %llu, tagged %llu; by class (tagged/all) 0:%llu/%llu 2:%llu/%llu 4:%llu/%llu 6:%llu/%llu "
             "other:%llu/%llu", static_cast<unsigned long long>(konAll), static_cast<unsigned long long>(konTagged),
             static_cast<unsigned long long>(konTaggedByClass[0]), static_cast<unsigned long long>(konByClass[0]),
             static_cast<unsigned long long>(konTaggedByClass[2]), static_cast<unsigned long long>(konByClass[2]),
             static_cast<unsigned long long>(konTaggedByClass[4]), static_cast<unsigned long long>(konByClass[4]),
             static_cast<unsigned long long>(konTaggedByClass[6]), static_cast<unsigned long long>(konByClass[6]),
             static_cast<unsigned long long>(konTaggedByClass[1] + konTaggedByClass[3] + konTaggedByClass[5] +
                                             konTaggedByClass[7]),
             static_cast<unsigned long long>(konByClass[1] + konByClass[3] + konByClass[5] + konByClass[7]));
    {
        int later = 0;
        for (int k = 0; k < preRegCount; ++k) later += isCarSample(preReg[k]) ? 1 : 0;
        log_info("d9: untagged engine key-ons %d, of which %d play a car sample known now", preRegCount,
                 later);
    }
    log_info("d9: owners two cars saw in the same frame: %llu", static_cast<unsigned long long>(g_conflicts));
    log_info("d9: car samples %d; key-ons of a car sample: %llu tagged, %llu untagged; RaceCourseSound key-ons %llu",
             carSampleCount, static_cast<unsigned long long>(konCarSampleTagged),
             static_cast<unsigned long long>(konCarSampleUntagged), static_cast<unsigned long long>(konCourse));
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t conductor = vehicle_conductor();
    for (const Car &c : g_cars) {
        if (!c.entryCar || now - c.frame > kStale) continue;
        const uint32_t vehicle = guest_ref<uint32_t>(c.entryCar + kVehicleObject);
        char line[512];
        int len = std::snprintf(line, sizeof line, "d9: car %08x +14=%d vehicle %08x (conductor %08x, +%x) entrant %d, "
                                "%d owners; H:", c.entryCar, guest_ref<int32_t>(c.entryCar + 0x14), vehicle, conductor,
                                conductor ? vehicle - conductor : 0, c.entrant == 0xFF ? -1 : c.entrant, c.owners);
        for (int g = 0; g < kGroupCount && len < 460; ++g) {
            const uint32_t h = c.entryCar + kCarSound + kGroups[g].h;
            len += std::snprintf(line + len, sizeof line - len, " %d[%u%u %.1f %.1f %.1f %.1f]", g,
                                 guest_ref<uint8_t>(h), guest_ref<uint8_t>(h + 1), guest_ref<float>(h + 4),
                                 guest_ref<float>(h + 8), guest_ref<float>(h + 12), guest_ref<float>(h + 16));
        }
        log_info("%s", line);
        if (c.hasRel) {
            // the engine group's direction from H (x right, z ahead negative: azimuth atan2(x, -z)) against the Vehicle
            // records' (right, forward in the player car's axes), in degrees clockwise from ahead
            const uint32_t h = c.entryCar + kCarSound + kGroups[0].h;
            const double hx = guest_ref<float>(h + 4), hz = guest_ref<float>(h + 12);
            log_info("d9:   entrant %d azimuth: H %.0f, Vehicle %.0f (player's axes: right %.1f, forward %.1f, "
                     "up %.1f)",
                     c.entrant, std::atan2(hx, -hz) * 57.29578, std::atan2(c.rel[0], c.rel[1]) * 57.29578, c.rel[0],
                     c.rel[1], c.rel[2]);
        }
    }
}

}  // namespace gt4::rt

extern "C" void gt4_car_sound_frame(uint32_t entryCar) {
    using namespace gt4::rt;
    const uint32_t sound = entryCar + kCarSound;
    if (!in_guest_ram(sound, 0xB40)) return;
    Car fresh;
    fresh.entryCar = entryCar;
    fresh.frame = kernel_frame_count();
    fresh.entrant = entrant_of(entryCar);
    if (g_trace && fresh.entrant != 0xFF) {   // (on the game thread, where the conductor is known)
        const uint32_t conductor = vehicle_conductor();
        const auto self = decode_vehicle(&guest_ref<uint8_t>(conductor + car::kCarsOffset +
                                                             fresh.entrant * car::kCarStride),
                                         fresh.entrant);
        const auto player = decode_vehicle(&guest_ref<uint8_t>(conductor + car::kCarsOffset), 0);
        for (int i = 0; i < 3; ++i) fresh.world[i] = self.bodyToWorld[3][i] - player.bodyToWorld[3][i];
        for (int a = 0; a < 3; ++a)
            fresh.rel[a] = fresh.world[0] * player.bodyToWorld[a][0] + fresh.world[1] * player.bodyToWorld[a][1] +
                           fresh.world[2] * player.bodyToWorld[a][2];
        fresh.hasRel = true;
    }
    for (int g = 0; g < kGroupCount; ++g) {
        const Group &grp = kGroups[g];
        for (uint32_t e : grp.engine)
            if (e) {
                const uint32_t p = guest_ref<uint32_t>(sound + e);
                if (p && fresh.owners < kOwnersPerCar)
                    fresh.owner[fresh.owners++] = {p, static_cast<uint8_t>(g), fresh.frame};
            }
        for (uint32_t k = 0; k < grp.count; ++k)
            for (uint32_t pb : kPlayback) {
                const uint32_t p = guest_ref<uint32_t>(sound + grp.players + k * kSePlayerBytes + pb);
                if (p && fresh.owners < kOwnersPerCar)
                    fresh.owner[fresh.owners++] = {p, static_cast<uint8_t>(g), fresh.frame};
            }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Car *slot = nullptr;
    for (Car &c : g_cars)
        if (c.entryCar == entryCar) slot = &c;
    if (!slot)
        for (Car &c : g_cars)
            if (!c.entryCar || fresh.frame - c.frame > kStale) {
                slot = &c;
                break;
            }
    if (!slot) return;
    if (slot->entryCar == entryCar)   // the owners seen lately but not now, kept for kOwnerGrace frames
        for (int k = 0; k < slot->owners && fresh.owners < kOwnersPerCar; ++k) {
            const Owner &o = slot->owner[k];
            if (fresh.frame - o.seen > static_cast<uint32_t>(kOwnerGrace)) continue;
            bool there = false;
            for (int j = 0; j < fresh.owners && !there; ++j) there = fresh.owner[j].address == o.address;
            if (!there) fresh.owner[fresh.owners++] = o;
        }
    *slot = fresh;
}
