// Movies, natively: the game's streaming loop (0054E3D8, mpegif) is replaced by a player that reads
// the PSS file straight from the disc, demuxes and decodes it with FFmpeg, and hands the pictures to
// the game's own picture queue, as the PS2 decoder did. Everything after the queue is the game's:
// mStream takes a picture every other VBlank (001FE018), uploads its macroblocks into its GSBuffer
// (001FD860...) and mMovieFace draws it.
//
// On the PS2 the IOP read and demuxed the PSS (MPG1), the EE fed the video PES to the IPU through
// a ring, the IPU decoded and colour-converted macroblocks, and the picture-done callback (0054FA48)
// wrote them into the queue: func_00564888 (wait for a free slot), func_00562A70 (the conversion),
// func_00564910 (commit; the "pictures ready" flag is set once the queue is full). A picture is
// (width / 16) x (height / 16) macroblocks of 16 x 16 RGBA32 pixels (0x400 bytes each), column by
// column (all the macroblocks of the first column, top to bottom, then the next: the order mStream
// uploads them in), alpha 0x80, as the IPU's CSC leaves them.
//
// The movie's audio (private stream 1: PCM or SPU ADPCM, which the IOP played) is decoded with it
// and goes to the game's mix (runtime/audio.cpp) as 48 kHz stereo.
#include "../audio.h"
#include "../guest.h"
#include "../kernel.h"
#include "../log.h"
#include "../runtime.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

using namespace gt4::rt;

namespace {

// The stream object (mpegif) as 0054E3D8 / 0054ED08 / 0054EB20 use it
struct StreamObj {
    char pad0[0x80];
    uint32_t size;    // +0x80 bytes of the file
    uint32_t entry;   // +0x84 its first sector (absolute LBA)
    int32_t width;    // +0x88 picture size (pixels), as the decoder set-up takes them
    int32_t height;   // +0x8C
    char pad90[0x19C - 0x90];
    int32_t mode;     // +0x19C
    int32_t loop;     // +0x1A0 1: play once; 0: repeat
    int32_t flags;    // +0x1A4
    int32_t memory;   // +0x1A8 preloaded (0054DF90): the file is still read from the disc here
    char pad1ac[0x3E8 - 0x1AC];
    uint32_t decoder;  // +0x3E8 the decoder state (below)
    int32_t priority;  // +0x3EC
    char pad3f0[0x3F8 - 0x3F0];
    uint32_t picture_area;  // +0x3F8
};

// The decoder state 0054F7E0 sets up; +0xC is the picture queue (00564610)
struct DecoderState {
    int32_t thread, id;
    uint32_t ring;
    uint32_t queue;
    int32_t count, stride;
    int32_t tail[3];
};

template <class F> F *game_fn(uint32_t addr) { return reinterpret_cast<F *>(guest_ptr(addr)); }
int32_t &game_int(uint32_t addr) { return guest_ref<int32_t>(addr); }

constexpr uint32_t kStreamLock = 0x00654D8C;  // D_00654D8C: the semaphore around the stream state
constexpr uint32_t kStopRequest = 0x00654D88; // D_00654D88: 0054ED08 sets it to stop the movie
constexpr uint32_t kReady = 0x0064C4B0;       // D_0064C4B0: pictures are ready to show
constexpr uint32_t kHold = 0x0064C4B8;        // D_0064C4B8
constexpr uint32_t kDecoderOn = 0x0064C4C4;   // D_0064C4C4: the decoder exists (the queue may be used)

void lock(int32_t sema) { game_fn<int(int)>(0x005ADCE0)(sema); }    // WaitSema
void unlock(int32_t sema) { game_fn<int(int)>(0x005ADCC0)(sema); }  // SignalSema

// ---- reading the PSS from the disc ----------------------------------------------------------

struct DiscFile {
    uint64_t base, size, pos;
};

int disc_read_packet(void *opaque, uint8_t *buf, int n) {
    auto *f = static_cast<DiscFile *>(opaque);
    if (f->pos >= f->size) return AVERROR_EOF;
    const uint32_t len = static_cast<uint32_t>(std::min<uint64_t>(n, f->size - f->pos));
    if (!disc_read(f->base + f->pos, buf, len)) return AVERROR(EIO);
    f->pos += len;
    return static_cast<int>(len);
}

int64_t disc_seek(void *opaque, int64_t offset, int whence) {
    auto *f = static_cast<DiscFile *>(opaque);
    if (whence == AVSEEK_SIZE) return static_cast<int64_t>(f->size);
    int64_t to = offset;
    if ((whence & 3) == SEEK_CUR) to += static_cast<int64_t>(f->pos);
    if ((whence & 3) == SEEK_END) to += static_cast<int64_t>(f->size);
    if (to < 0) return -1;
    f->pos = static_cast<uint64_t>(to);
    return to;
}

// ---- the PSS audio --------------------------------------------------------------------------
// Sony's movie audio is in private stream 1 (0xBD), which FFmpeg's demuxer does not expose: each
// packet's payload is a 4-byte header (0xFF, 0xA1, 0x00, track) and the next bytes of that track's ADS stream
// ("SShd": codec 1 16-bit PCM, 2 SPU ADPCM mono, 0x10 SPU ADPCM stereo; rate; channels; the
// interleave, bytes per channel block; "SSbd": the data), as the BGM files. This reader walks the
// PES packets on its own and keeps the mix's movie queue a little ahead of the pictures.

struct PssAudio {
    DiscFile file{};
    std::vector<uint8_t> pes;     // bytes of the program stream read and not parsed yet
    size_t pes_at = 0;
    std::vector<uint8_t> data;    // the audio payload collected and not decoded yet
    size_t data_at = 0;
    bool header = false, ended = false;
    uint32_t codec = 0, rate = 48000, channels = 2, interleave = 0x400;
    uint8_t track = 1;            // the payload header's 4th byte numbers the tracks (two in the opening)
    int32_t hist[2][2] = {};
    uint64_t resample = 0;        // 16.16 position for a rate other than 48 kHz
    int16_t last[2] = {};

    void reset(uint64_t base, uint64_t size) {
        *this = PssAudio{};
        file = {base, size, 0};
    }

    bool more_pes(size_t need) {  // at least `need` unparsed bytes
        while (pes.size() - pes_at < need) {
            if (file.pos >= file.size) return false;
            if (pes_at) {
                pes.erase(pes.begin(), pes.begin() + static_cast<std::ptrdiff_t>(pes_at));
                pes_at = 0;
            }
            const size_t old = pes.size();
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(64 * 1024, file.size - file.pos));
            pes.resize(old + n);
            disc_read(file.base + file.pos, pes.data() + old, n);
            file.pos += n;
        }
        return true;
    }

    // Collects audio payload until `want` bytes are waiting; false at the end of the stream.
    bool more_data(size_t want) {
        while (data.size() - data_at < want) {
            if (!more_pes(6)) return false;
            const uint8_t *p = &pes[pes_at];
            if (p[0] != 0 || p[1] != 0 || p[2] != 1) {
                ++pes_at;
                continue;
            }
            if (p[3] == 0xB9) return false;  // end code
            if (p[3] == 0xBA) {               // pack header (MPEG-2): 14 bytes and stuffing
                if (!more_pes(14)) return false;
                pes_at += 14 + (pes[pes_at + 13] & 7);
                continue;
            }
            const size_t len = (static_cast<size_t>(p[4]) << 8) | p[5];
            if (!more_pes(6 + len)) return false;
            p = &pes[pes_at];
            if (p[3] == 0xBD && len > 3) {
                const size_t skip = 3 + p[8];  // PES header: flags, flags, header data length
                if (len > skip + 4 && p[6 + skip] == 0xFF && p[6 + skip + 3] == track) {
                    const uint8_t *payload = p + 6 + skip + 4;
                    if (data_at) {
                        data.erase(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(data_at));
                        data_at = 0;
                    }
                    data.insert(data.end(), payload, p + 6 + len);
                }
            }
            pes_at += 6 + len;
        }
        return true;
    }

    void push(const int16_t *frames, int count) {
        if (rate == 48000) {
            movie_audio_push(frames, count);
            return;
        }
        std::vector<int16_t> out;
        const uint64_t step = (static_cast<uint64_t>(rate) << 16) / 48000;
        for (int i = 0; i < count;) {  // linear, 16.16 positions in the input
            const int32_t f = static_cast<int32_t>(resample & 0xFFFF);
            for (int c = 0; c < 2; ++c)
                out.push_back(static_cast<int16_t>(last[c] + (((frames[2 * i + c] - last[c]) * f) >> 16)));
            resample += step;
            while (resample >= 0x10000 && i < count) {
                resample -= 0x10000;
                last[0] = frames[2 * i];
                last[1] = frames[2 * i + 1];
                ++i;
            }
        }
        movie_audio_push(out.data(), static_cast<int>(out.size() / 2));
    }

    // Decodes until the movie queue holds `frames` frames.
    void pump(int frames) {
        if (ended) return;
        if (!header) {
            if (!more_data(0x28) || std::memcmp(&data[data_at], "SShd", 4) != 0) {
                ended = true;
                return;
            }
            auto word = [&](int k) {
                uint32_t v;
                std::memcpy(&v, &data[data_at + 4 * k], 4);
                return v;
            };
            codec = word(2);
            rate = word(3) ? word(3) : 48000;
            channels = std::clamp<uint32_t>(word(4), 1, 2);
            interleave = word(5) ? word(5) : 0x400;
            data_at += 0x28;
            header = true;
            log_info("movie: audio codec %#x, %u Hz, %u channel(s), interleave %#x", codec, rate, channels, interleave);
        }
        std::vector<int16_t> pcm;
        while (movie_audio_queued() < frames) {
            const size_t chunk = static_cast<size_t>(interleave) * channels;
            if (!more_data(chunk)) {
                ended = true;
                return;
            }
            const uint8_t *in = &data[data_at];
            if (codec == 1) {
                const uint32_t n = interleave / 2;
                pcm.resize(2 * n);
                for (uint32_t i = 0; i < n; ++i)
                    for (uint32_t c = 0; c < 2; ++c) {
                        int16_t v;
                        std::memcpy(&v, in + std::min(c, channels - 1) * interleave + 2 * i, 2);
                        pcm[2 * i + c] = v;
                    }
            } else {
                const uint32_t blocks = interleave / 16;
                pcm.resize(2 * blocks * 28);
                int16_t one[28];
                for (uint32_t ch = 0; ch < channels; ++ch)
                    for (uint32_t k = 0; k < blocks; ++k) {
                        spu_adpcm_decode(in + ch * interleave + 16 * k, hist[ch][0], hist[ch][1], one);
                        for (int i = 0; i < 28; ++i) {
                            pcm[2 * (28 * k + i) + ch] = one[i];
                            if (channels == 1) pcm[2 * (28 * k + i) + 1] = one[i];
                        }
                    }
            }
            data_at += chunk;
            push(pcm.data(), static_cast<int>(pcm.size() / 2));
        }
    }
};

// ---- the decoder ----------------------------------------------------------------------------

struct Movie {
    DiscFile file{};
    AVFormatContext *fmt = nullptr;
    AVIOContext *io = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int video = -1;
    int audio = -1;
    AVCodecContext *acodec = nullptr;
    AVFrame *aframe = nullptr;
    SwrContext *swr = nullptr;

    void open_audio() {
        audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, video, nullptr, 0);
        if (audio < 0) return;
        const AVCodec *dec = avcodec_find_decoder(fmt->streams[audio]->codecpar->codec_id);
        acodec = dec ? avcodec_alloc_context3(dec) : nullptr;
        if (!acodec || avcodec_parameters_to_context(acodec, fmt->streams[audio]->codecpar) < 0 ||
            avcodec_open2(acodec, dec, nullptr) < 0) {
            log_warn("movie: no decoder for the audio (codec %d)", fmt->streams[audio]->codecpar->codec_id);
            avcodec_free_context(&acodec);
            audio = -1;
            return;
        }
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, 48000, &acodec->ch_layout, acodec->sample_fmt,
                                acodec->sample_rate, 0, nullptr) < 0 ||
            swr_init(swr) < 0) {
            swr_free(&swr);
            avcodec_free_context(&acodec);
            audio = -1;
            return;
        }
        aframe = av_frame_alloc();
        log_info("movie: audio %s, %d Hz, %d channel(s)", dec->name, acodec->sample_rate, acodec->ch_layout.nb_channels);
    }

    void decode_audio(const AVPacket *pkt) {
        if (avcodec_send_packet(acodec, pkt) < 0) return;
        std::vector<int16_t> out;
        while (avcodec_receive_frame(acodec, aframe) == 0) {
            const int cap = swr_get_out_samples(swr, aframe->nb_samples);
            out.resize(2 * static_cast<size_t>(std::max(cap, 0)));
            uint8_t *dst = reinterpret_cast<uint8_t *>(out.data());
            const int n = swr_convert(swr, &dst, cap, const_cast<const uint8_t **>(aframe->extended_data),
                                      aframe->nb_samples);
            if (n > 0) movie_audio_push(out.data(), n);
        }
    }

    bool open(uint32_t lba, uint32_t bytes) {
        file = {static_cast<uint64_t>(lba) * 2048, bytes, 0};
        auto *buffer = static_cast<uint8_t *>(av_malloc(64 * 1024));
        io = avio_alloc_context(buffer, 64 * 1024, 0, &file, disc_read_packet, nullptr, disc_seek);
        fmt = avformat_alloc_context();
        fmt->pb = io;
        fmt->probesize = 256 * 1024;
        if (avformat_open_input(&fmt, nullptr, av_find_input_format("mpeg"), nullptr) < 0) {
            fmt = nullptr;
            return false;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
        video = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (video < 0) return false;
        const AVCodec *dec = avcodec_find_decoder(fmt->streams[video]->codecpar->codec_id);
        if (!dec) return false;
        codec = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(codec, fmt->streams[video]->codecpar);
        if (avcodec_open2(codec, dec, nullptr) < 0) return false;
        open_audio();
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        return true;
    }

    // the next decoded picture; false at the end of the file
    bool next() {
        for (;;) {
            const int r = avcodec_receive_frame(codec, frame);
            if (r == 0) return true;
            if (r == AVERROR_EOF) return false;
            if (av_read_frame(fmt, packet) < 0) {
                avcodec_send_packet(codec, nullptr);  // drain
                continue;
            }
            if (packet->stream_index == video) avcodec_send_packet(codec, packet);
            else if (packet->stream_index == audio) decode_audio(packet);
            av_packet_unref(packet);
        }
    }

    bool rewind() {
        avcodec_flush_buffers(codec);
        if (acodec) avcodec_flush_buffers(acodec);
        return av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BYTE) >= 0 || avformat_seek_file(fmt, -1, 0, 0, 0, AVSEEK_FLAG_BYTE) >= 0;
    }

    ~Movie() {
        swr_free(&swr);
        av_frame_free(&aframe);
        avcodec_free_context(&acodec);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    }
};

inline uint8_t clip(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// The picture in the queue's layout: 16 x 16 macroblocks of RGBA32, colours converted with the
// IPU's integer BT.601 coefficients (Y 1.1640625, Cr->R 1.59375, Cr->G -0.8125, Cb->G -0.390625,
// Cb->B 2.015625), alpha 0x80.
void store_picture(const AVFrame *f, uint8_t *dst, int width, int height) {
    const int mbw = width / 16, mbh = height / 16;
    const int fw = std::min(width, f->width), fh = std::min(height, f->height);
    for (int mby = 0; mby < mbh; ++mby)
        for (int mbx = 0; mbx < mbw; ++mbx) {
            uint8_t *mb = dst + (mbx * mbh + mby) * 0x400;  // column by column (001FD860's upload order)
            for (int row = 0; row < 16; ++row) {
                const int y = mby * 16 + row;
                uint8_t *out = mb + row * 64;
                for (int col = 0; col < 16; ++col, out += 4) {
                    const int x = mbx * 16 + col;
                    if (x >= fw || y >= fh) {
                        out[0] = out[1] = out[2] = 0;
                        out[3] = 0x80;
                        continue;
                    }
                    const int Y = std::max(0, f->data[0][y * f->linesize[0] + x] - 16) * 0x95;
                    const int cb = f->data[1][(y >> 1) * f->linesize[1] + (x >> 1)] - 128;
                    const int cr = f->data[2][(y >> 1) * f->linesize[2] + (x >> 1)] - 128;
                    out[0] = clip((Y + 0xCC * cr + 64) >> 7);
                    out[1] = clip((Y - 0x68 * cr - 0x32 * cb + 64) >> 7);
                    out[2] = clip((Y + 0x102 * cb + 64) >> 7);
                    out[3] = 0x80;
                }
            }
        }
}

}  // namespace

// 0054E3D8: plays the stream object's current file into its picture queue until it ends or the
// game stops it (0054ED08), then closes the decoder, as the original's loop does.
extern "C" void gt4_replace_func_0054E3D8(StreamObj *p) {
    if (!game_fn<int(StreamObj *)>(0x0054E200)(p)) return;  // resolve the file (entry, size)
    auto *state = reinterpret_cast<DecoderState *>(guest_ptr(p->decoder));
    const int32_t stream_lock = game_int(kStreamLock);

    // the decoder set-up of 0054F7E0, without the IPU: the picture queue for two pictures
    lock(stream_lock);
    const int32_t picture_bytes = p->width * p->height * 4;
    {
        DecoderState old = *state;
        std::memset(state, 0, sizeof *state);
        state->id = old.id;
        state->ring = old.ring;
        state->queue = old.queue;
        state->count = p->width;
        state->stride = p->height;
    }
    game_fn<void *(uint32_t, int, int, int, int)>(0x00564610)(state->queue, picture_bytes, 2, p->picture_area,
                                                              p->priority);
    game_int(kDecoderOn) = 1;
    const bool stopped = game_int(kStopRequest) == 1;
    if (stopped) game_int(kStopRequest) = 0;
    unlock(stream_lock);
    game_int(kHold) = 0;
    game_int(kReady) = 0;

    if (!stopped) {
        Movie movie;
        bool ok;
        {
            KernelScope ks;
            ok = movie.open(p->entry, p->size);
        }
        if (!ok) log_warn("movie: cannot decode the PSS at LBA %u (%u bytes)", p->entry, p->size);
        log_info("movie: LBA %u, %u bytes, %dx%d%s", p->entry, p->size, p->width, p->height,
                 p->loop == 1 ? "" : " (repeating)");
        PssAudio pss;
        const bool own_audio = ok && movie.audio < 0;  // FFmpeg found no audio: the PSS reader
        if (own_audio) pss.reset(static_cast<uint64_t>(p->entry) * 2048, p->size);
        for (int frames = 0; ok;) {
            bool have;
            {
                KernelScope ks;
                have = movie.next();
                if (!have && p->loop != 1) {  // repeat: from the start again
                    have = movie.rewind() && movie.next();
                    if (own_audio) pss.reset(static_cast<uint64_t>(p->entry) * 2048, p->size);
                }
            }
            if (!have) break;
            // a free slot of the queue (waits while both pictures are still to be shown)
            const uint32_t slot = game_fn<uint32_t(uint32_t)>(0x00564888)(state->queue);
            lock(stream_lock);
            const bool stop = game_int(kStopRequest) == 1;
            if (stop) game_int(kStopRequest) = 0;
            unlock(stream_lock);
            if (stop) break;
            {
                KernelScope ks;
                store_picture(movie.frame, static_cast<uint8_t *>(guest_ptr(slot)), p->width, p->height);
                if (own_audio) pss.pump(48000 / 4);  // a quarter of a second ahead
            }
            game_fn<void(uint32_t)>(0x00564910)(state->queue);  // commit (sets "ready" once full)
            ++frames;
        }
    }

    // the end, as the original's: no more pictures, the decoder closes
    movie_audio_clear();
    game_int(kReady) = 0;
    game_fn<void(uint32_t)>(0x0054FDA8)(p->decoder);
    lock(stream_lock);
    game_fn<void(uint32_t)>(0x005646D8)(state->queue);  // the queue's semaphores
    game_int(kDecoderOn) = 0;
    unlock(stream_lock);
}
