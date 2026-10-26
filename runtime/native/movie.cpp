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
// Movie audio is not played yet (the IOP's MPG1/MPG2 servers are silent).
#include "../guest.h"
#include "../kernel.h"
#include "../log.h"
#include "../runtime.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cstring>

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

// ---- the decoder ----------------------------------------------------------------------------

struct Movie {
    DiscFile file{};
    AVFormatContext *fmt = nullptr;
    AVIOContext *io = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int video = -1;

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
            av_packet_unref(packet);
        }
    }

    bool rewind() {
        avcodec_flush_buffers(codec);
        return av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BYTE) >= 0 || avformat_seek_file(fmt, -1, 0, 0, 0, AVSEEK_FLAG_BYTE) >= 0;
    }

    ~Movie() {
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
        for (int frames = 0; ok;) {
            bool have;
            {
                KernelScope ks;
                have = movie.next();
                if (!have && p->loop != 1) {  // repeat: from the start again
                    have = movie.rewind() && movie.next();
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
            }
            game_fn<void(uint32_t)>(0x00564910)(state->queue);  // commit (sets "ready" once full)
            ++frames;
        }
    }

    // the end, as the original's: no more pictures, the decoder closes
    game_int(kReady) = 0;
    game_fn<void(uint32_t)>(0x0054FDA8)(p->decoder);
    lock(stream_lock);
    game_fn<void(uint32_t)>(0x005646D8)(state->queue);  // the queue's semaphores
    game_int(kDecoderOn) = 0;
    unlock(stream_lock);
}
