# D9: how a GT4 voice maps to an entrant (lane D, 2026-11-08)

PLAN.md D9 (spatial audio) asks for a design note first: how a voice reaches the car it belongs to in GT4's sound
driver, whether the driver already pans by position, and where the tags come from. Sources: the decompilation
(`gt4-decomp/src`, matched; `build/native_drafts`, unmatched drafts, readable but with guessed names), the
decomp's `knowledge/race-car-sound.md`, and this repo's native copy `game_src/func_003B5E80.cpp`. **Confirmed**
means seen in matched source or our native copy; **draft** means read in an unmatched draft; **inferred** says why.

## What GT4's driver does

**The register image.** The sound thread (`func_0055EB50` → `func_00568080` → `func_0055DDB8`) builds the SPUP
image in `func_0055DF70` (draft) in a buffer at VoiceSystem + 0x2AB0, VoiceSystem being `SDDRV__VoiceSystem__master_`
at 0x006518C8. It first runs `func_0055D890` (draft) on voices 0..45; voices 46 and 47 (core 1's last two) are the
BGM ring. Then it copies each voice's changed fields into the image, with the key-on and key-off masks.

**The voices.** VoiceSystem + 0xA8 holds 48 records of 0xE0 bytes (`func_0055DE50`, confirmed):
- +0x00: `1 << n`, and +0x18: the hardware voice (`func_0055D6F0`, confirmed);
- +0x10: the **owner**, the object that started the voice (`func_0055A8B0`, confirmed);
- +0x30: the class: 2 a sound effect (`func_00611AA0`, confirmed), 4 an engine layer (draft), 6 just allocated;
- +0x34: the priority;
- +0x64 / +0x66: the left and right gains the game asked for (confirmed).

**Allocation is dynamic.** `func_0055D0B0` → `func_0055C758` (confirmed) takes the first free voice in a mask.
When none is free, `func_0055D070` steals the lowest priority (confirmed). A voice of class other than 0 is freed
when its ENVX reads 0 (`func_0055D890`, draft; `func_0055E6A8` reads the reply, confirmed). ENDX is not read in the
code seen. No voice belongs to a car for good, and **no voice record holds a car index**: a voice reaches its car
only through its owner.

**The cars' sounds.** Each race car embeds a `RaceCarSound` at RaceEntryCar + 0x2900 (confirmed:
`RaceBase__soundStart` calls `RaceCarSound__playStart(car + 0x2900)` per car). `func_003923D0` builds its two
engine sample names from the car id. Inside it (the decomp's layout note, confirmed by `playStart` / `playStop`):

| offset | what | sound |
|---|---|---|
| 0x14, 0x2B8, 0x3F4, 0x474 + n × 0x1A0 | `H` blocks: the last listener-relative position (`func_00391AF0` writes flip bytes and x, y, z) | one per group |
| 0x28, 0x2C, 0x2CC | engine pool entries (`func_00559808`; a pool of 24 entries of 0x58 bytes at 0x0064FBF8, each feeding 2 voices crossfaded by RPM) | engine |
| 0x278 | 2 `SePlayer`s (0x20 each) | engine-related effects |
| 0x408 | 3 `SePlayer`s | effects (group at 0x3F4) |
| 0x488 + n × 0x1A0, n = 0..3 | 11 `SePlayer`s each | the four groups: tyres and surfaces per wheel (inferred from `func_003B5E80` calling them per wheel) or turbo bands (the decomp's note leaves it open) |

**GT4 already pans and attenuates every car sound by position.** It does this on the EE, before the register image:
- `func_003B5E80` (our native copy) runs per car per frame. It puts the car's position into the listener's space
  (`func_004A7AA8`), works out a Doppler factor (speed of sound about 5.524 world units) and reads the RPM. Then it
  calls `func_00392640(car + 0x2900, …)`.
- `func_00392640` (draft) → `func_00391AF0` (confirmed) → `func_0039A7B0` (draft) computes:
  - `d = sqrt(x² + z²)` and `gain = 1 / ((d − m28) · m24 + 1)` (or `32 / d`);
  - `angle = (x / d + 1) · π / 4`, and L / R by an equal-power pair.
- `func_00391AF0` negates L or R for a sound behind the listener: a phase inversion, Pro Logic-style surround in the
  stereo mix.
- Engine voices get their L / R at pool entry + 0x4C / + 0x4E; effects go through `SePlayer::Play(…, x, y, z)` as
  `x · 16383`, `y · 16383` into the voice's + 0x64 / + 0x66 (confirmed).
- `func_0055D890` folds in the class volume and the pan table, so the image's VOLL / VOLR are GT4's final gains.

**Not tied to a car:** BGM (PBGM, voices 46 and 47), voice and movie streams (outside the voices), UI sounds
(`SystemSoundPlay` 0x463458), course ambience and crowd (`RaceCourseSound::playUpdate` 0x398B70, by its name), the
pit crew (`Pitmen::playSound` 0x3D3358), and the sequencer's tracks (`.sqt`, owner a track).

## The design

**Tags, game side, at each register image.** Our native `func_003B5E80` copy registers, every frame and for its car,
the car's sound objects in a small runtime table, keyed by object address:
- each engine pool entry (+0x28, +0x2C, +0x2CC);
- each `SePlayer`'s playback object (its +0x14 / +0x18);
- with each one: the car's entrant and the address of the `H` block of the object's group.

When the image goes out (`spup` fno 4, `runtime/iop.cpp`), the runtime reads the 48 voice records. For each hardware
voice it looks up the owner, and a match tags the voice with its entrant and its group's position. The tags go to
the host beside the image in a new kind of `GT4_REC_SPU2_REGS` (`GT4_SPU2_TAGS`): per voice the entrant, or none,
and the position, about 0.8 KB a frame. The table and the lookup allocate nothing.

**The entrant.** The runtime's `Vehicle` entrant is the car's index in the conductor's array (conductor + 0x6698 +
k × 0x10A0, `runtime/vehicle.h`). RaceEntryCar + 0x18 is the car's vehicle object; that it is that array entry is
**to verify** in step 1. The fallback is the order of `RaceBase`'s car list.

**Positions: GT4's own, proposed.** D9's text takes positions from `Vehicle::bodyToWorld` and the main view's camera.
GT4 already computes each car sound group's position in its listener's space every frame (the `H` blocks), from
the camera its own pan uses. Taking those has four advantages:
- each group has its own point (a wheel's, the engine's), not the body's centre;
- the image and the positions are of the same frame;
- nothing is needed from the scene records: the vehicles-only camera (CONTRACTS a.1) becomes unnecessary for D9;
- spatial audio can never disagree with the direction GT4 itself intends.
The `Vehicle` records and the main camera stay the acceptance's measure: the direction check compares against the
car's view-space x. If GT4's listener and the main view's camera turn out to differ (an in-car audio point, say),
step 1 shows it and the `Vehicle` path comes back.

**The level without GT4's pan.** For a tagged voice, the host takes the power sum of the image's gains,
`sqrt(VOLL² + VOLR²)`, signs dropped. The equal-power pair sums to GT4's level, so this removes the pan and the rear
phase inversion and keeps the class volume and **GT4's own distance gain**. Spatial then adds direction only, and no
second distance curve. D9's distance check then measures GT4's curve, which spatial must keep within 1 dB. A
voice in sweep mode (VOLL / VOLR bit 15) is left in the bed, as the mixer has no sweep.

**Doppler: none added.** GT4 computes a Doppler factor in `func_003B5E80` (inferred: applied to the pitch). Adding
one on the host would apply it twice. Step 1 confirms it from the pitches of a passing car; if GT4's turns out to be
off, a limited Doppler, off in `original`, comes back.

**Rendering (`audio.spatial`).**
- `original`: the host mix as D5 has it, bit for bit.
- `stereo`: tagged voices with an equal-power pan from their azimuth (atan2 of x, z), no phase inversion. That is
  close to GT4's own pan, without the rear inversion, which smears on headphones and on speakers without a Pro Logic
  decoder.
- `hrtf`: tagged voices through an HRTF (a SOFA dataset with libmysofa, or Steam Audio; the licence is lane C's
  check, no Sony data).
- Untagged voices in all three: the bed, mixed as `original` mixes them, phase inversions included.

## Steps

1. **Game side** (game_src, so `DIFFTEST`):
   - the owner table filled by our `func_003B5E80` copy;
   - the tags at each image, and `GT4_SPU2_TAGS`;
   - counters for the acceptance (`voicesTagged`, `voicesUntagged`, `voicesNoPosition`);
   - car key-ons counted against the car sound banks: the STRP regions loaded for the entrants, logged at load.
   Step 1 measures: the tag rate; the entrant link (RaceEntryCar + 0x18 against the conductor's array); whether
   `func_003B5E80` runs for every car or only near ones; GT4's listener against the main camera (the `H` position
   against the `Vehicle` view-space x); and GT4's Doppler.
2. **Host:** the tags into `HostMixer`; `audio.spatial` with `original` and `stereo`; the direction, distance and
   loudness checks.
3. **`hrtf`**, after lane C clears a library and dataset.

**Open:**
- what each `SePlayer` slot plays;
- the four groups' meaning;
- collision and wind sounds outside `RaceCarSound` (not traced; untagged, so in the bed);
- `func_00558DB0` / `E28` (engine layer volume and pitch, unread).
