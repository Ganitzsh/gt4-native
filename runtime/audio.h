#pragma once
// The game's sound, replacing the IOP side (docs/IOP_SERVICES.md 5.2-5.6, docs/PLATFORM.md):
//  - SPU2: a software voice mixer driven by the register image the EE sound driver sends every
//    VBlank (SPUP fno 4): SPU ADPCM from the 2 MB SPU RAM (filled by STRP fno 5), pitch, ADSR,
//    volumes, key on/off; the reply's ENVX/ENDX come from it (the EE releases voices that read 0);
//  - BGM (PBGM): the .ads streams, 16-bit PCM or SPU ADPCM, read from the disc image;
//  - movie audio: PCM the movie player (runtime/native/movie.cpp) decodes with FFmpeg.
// Mixed to 48 kHz stereo at each VBlank and sent to the host (GT4_REC_AUDIO, stream 0); with
// GT4_AUDIO_WAV=path also written to a WAV file (headless checks).
#include <cstdint>

namespace gt4::rt {

void audio_init();
void audio_vblank();  // mixes one VBlank of sound (called by the VBlank interrupt thread)

uint8_t *spu_ram();  // 2 MB
void spu_apply(const uint8_t *image);  // SPUP fno 4: 2 cores x 0x1D0 bytes
void spu_status(uint8_t *reply);       // per core: u32 ENDX, u16 ENVX[24] (0x34 bytes)

// PBGM
void bgm_play(uint32_t lba, uint32_t size, bool loop);
void bgm_stop();
void bgm_fade(int32_t target);    // 0 or 0x7FFF, 10 ms steps; reaching 0 stops
void bgm_volume(int32_t volume);  // master, 0..0x8000 (negative: inverted, treated as its magnitude)
void bgm_pause(bool paused);
void bgm_mono(bool mono);
void bgm_status(uint8_t *reply);  // 0x40 bytes: s64 position (us), s64 samples, u32 loops, u32 finished

// Movie audio: interleaved stereo s16 at 48 kHz.
void movie_audio_push(const int16_t *frames, int count);
void movie_audio_clear();
int movie_audio_queued();  // frames waiting to be played
// SPU ADPCM (16-byte blocks of 28 samples), with the decoder's two history samples.
void spu_adpcm_decode(const uint8_t *block, int32_t &s1, int32_t &s2, int16_t out[28]);

}  // namespace gt4::rt
