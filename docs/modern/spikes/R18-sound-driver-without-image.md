# R18: the sound driver without the SPU2 register image (lane D, 2026-11-09)

RISKS.md R18 (PLAN.md P6) asks whether a native voice API, called where GT4's sound driver plays, pitches, sets the
volume of and releases a voice, could replace the SPU2 register image the driver sends every frame, with the same
timing and voice release, and gain something by it. Answer: **no; the register image stays.** Neither half of the
decision rule holds. The one value the driver reads back needs the same game-side SPU2 model either way, and
what the image costs is too small to gain from: everything a native API was to enable is already done, or can be
done, from the image.

## What the driver sends and reads back

The IOP's SPUP module (`runtime/iop.cpp` `spup`) takes three calls:
- fno 0, once: the version (0x112).
- fno 4, once a frame: the register image in (0x3B8 bytes from the EE: the two cores' 0x1D0-byte images and a
  header; `func_0055E6D0`, `func_006120C0`), the status out (0x80 bytes into `D_00873DC0`).
- fno 3: the status alone (`func_0055E828`, `func_00612070`).

The status is 0x34 bytes a core: ENDX (a u32), then ENVX (24 u16). Who reads it, from the decomp and the ELF (read
in place):
- **ENVX: read, once a voice a frame.** `func_0055E678` returns the u16 at `D_00873DC4 + (core * 0x1A + voice) * 2`,
  the reply plus 4, which is each core's ENVX. Its one caller, `func_0055E6A8` (hardware voice to core and voice), is
  called from 0x0055D8D4 and 0x0055D904, both in `func_0055D890`, the per-voice update that frees a voice whose
  envelope reads 0 (D9's note: "A voice of class other than 0 is freed when its ENVX reads 0").
- **ENDX: never read.** Every instruction that forms the reply's address (`lui` / `addiu` to 0x00873DC0, or to
  0x00873E40, the copy `func_0055E4E0` makes) does so to pass the buffer to the RPC call or to that copy. No load
  reads either core's ENDX word, in the reply or the copy. The copy's accessor, `func_0055E668`, is never called (no
  `jal` to it in the ELF). This matches D9's note ("ENDX is not read in the code seen"), now for the whole ELF.

R16 measured that this ENVX can't come even one frame late: with the status one frame behind, the driver frees
voices at other moments and plays different sounds. So whatever replaces the image, each voice's envelope must still
be answered exactly, in the same call, by a game-side voice model with the SPU2's ADSR semantics (attack, decay,
sustain, release, and the rates they step at). That model is D5's `Spu2` (`platform/audio/spu`), which the game
already runs to answer the status. A native API would remove the image's encoding, not that model.

## The gain, measured

- **Bytes.** The image is 928 bytes a frame (two cores of 0x1D0), and D9's tags add 776 (`GT4_SPU2_TAGS`: 48 x 16
  plus the record's head). On `race_mixer_host.cap` (8,121 VBlanks, 3,144,205,376 bytes, about 387 KB a frame) the
  two are about 0.4 % of the stream.
- **The host's parse.** `Spu2::apply` on a dense image (all 48 voices' registers written and keyed on, the worst
  case; a frame of a race writes far fewer): 4.2 us each (200,000 runs on the desktop, an i686 g++ -O2 build; the
  host's mixer runs x64 there and aarch64 on the Odin, so this is not the Odin's number). The host's whole mix of a
  frame is 208 us on the desktop (`original`, D9 step 3's PERF), so the parse is under 2 % of it.
- **What a native API was to enable** (RISKS R18), each already available with the image:
  - positional audio: D9 (`audio.spatial = stereo | hrtf`), from the image and D9's tags (each voice's car and its
    sound group's direction, GT4's own);
  - per-voice effects: the host mixer has each voice's registers and tag, and can treat any voice on its own (as
    `hrtf` routes the cars' voices to their own buses);
  - higher rates and quality: `audio.interpolation` (cubic, sinc) on the same voices; the output rate is the host
    mixer's to resample;
  - replacing samples (an HD sound pack, say): a voice's sample start address (SSA) is in the image at its key-on,
    as D9's trace logs it, so the host can recognise a sample and play another in its place.

## Decision

The rule (RISKS R18) asks for both a measurable gain and every read-back answerable from native voices with the same
semantics. The read-back is answerable only by the model the game already runs, and the gain is a few hundred bytes
and a few microseconds a frame, with nothing a native API alone would enable. The register image stays; D5's
design (the game answers the status from its `Spu2`, the host mixes the same voices from the image) is the one to
build on. No ticket follows. RISKS R18 is closed with this note.
