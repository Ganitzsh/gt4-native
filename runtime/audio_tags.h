#pragma once
// D9 (spatial audio), the game side: GT4's voices tagged with the car whose sound started them
// (docs/modern/spikes/D9-voices-to-entrants.md). GT4 allocates voices dynamically and no voice record holds a car: a
// voice's owner (voice record +0x10) is an engine pool entry or a SePlayer's playback object inside a car's
// RaceCarSound (RaceEntryCar + 0x2900). Our native func_003B5E80 copy registers each car's sound objects every frame;
// at each SPUP register image the owners are looked up, and a voice found is tagged with the car's entrant and the
// listener-relative position GT4 keeps for that sound's group (RaceCarSound's H blocks).
#include <cstdint>

extern "C" void gt4_car_sound_frame(uint32_t raceEntryCar);   // game_src/func_003B5E80.cpp, per car per frame

namespace gt4::rt {

// One hardware voice's tag (core 0's voices 0..23, then core 1's).
struct VoiceTag {
    uint8_t entrant;   // 0..5, 0xFF untagged
    uint8_t group;     // the RaceCarSound sound group (its H block), 0xFF none
    uint8_t flags;     // 1: a position is known
    uint8_t pad;
    float position[3];   // GT4's listener-relative x, y, z of the group
};
static_assert(sizeof(VoiceTag) == 16);

// The 48 voices' tags now (allocation-free): all 48 are written, untagged (entrant and group 0xFF, flags 0) unless a
// car's sound owns the voice; with an owner two cars list, the car that saw it last. Returns the number tagged.
int voice_tags(VoiceTag out[48]);

// GT4_TRACE_D9 (debugging, step 1's measurements): at an SPUP image, the keyed voices' owners against the registered
// sound objects, and every 300 frames the cars' links and H blocks. `image`: SPUP fno 4's register image.
void voice_tags_trace(const uint8_t *image);

// GT4_TRACE_D9=3 (D9 step 2's direction check): after the `image`-th register image sent to the host (counted from
// 1), each car's position along the player car's body rows (forward, right, up; metres), from the vehicle data of the
// Vehicle records. The host's GT4_TRACE_D9 lines carry the same image count, so the two logs join exactly.
void voice_tags_positions(uint64_t image);

}  // namespace gt4::rt
