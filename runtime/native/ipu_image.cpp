// Still pictures, natively: 0054C8F0 decoded an "ipum" picture (an IPU intra stream: MPEG-1/2 intra
// macroblocks, as Sony's .ipu files hold them) with the IPU, driving its registers and both IPU DMA
// channels. Here FFmpeg's IPU decoder does it, and the picture is written as the IPU's IDEC + CSC
// left it: (width / 16) x (height / 16) macroblocks of 16 x 16 RGBA32 pixels (0x400 bytes each) in
// decoding order (row by row), colours with the IPU's integer BT.601 coefficients, alpha 0x80
// (thresholds 0). Callers: 002771A8 (a movie face's .ipic archive, mode 0) and the image loader
// 001C4DF8 (mode 1).
//
// The file: 16 bytes of header ("ipum", u32 size of the whole file, u16 width, u16 height, 4 bytes),
// then the stream: a flags byte (intra DC precision, alternate scan, intra VLC, q scale type, MPEG-1,
// "DT" bit per macroblock: what the original copies into IPU_CTRL and IDEC; mode 1 uses 0), the
// macroblocks, and the end code 0x000001B0. The original skipped the flags byte with FDEC 8 and
// decoded the rest with IDEC (QSC 1): FFmpeg's packet is the flags byte, the stream and the end code.
//
// Unverified (2026-10-30): no tested route reaches this (the race route calls 001FE2B8 but no face had
// an .ipic loaded), and the one ipum stored uncompressed on the disc (640 x 448, flags 0x62) is rejected
// by FFmpeg's decoder at every packet length, so either that file is not one the game decodes or the
// IPU's IDEC syntax differs from FFmpeg's reading. A picture that does not decode comes out black, with
// a warning in the log.
#include "../guest.h"
#include "../log.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace gt4::rt;

namespace {

constexpr uint32_t kMagic = 0x006C8490;   // "ipum" in the image (D_006C8490)

inline uint8_t clip(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// The picture as the IPU's CSC wrote it (the same coefficients as runtime/native/movie.cpp).
void store(const AVFrame *f, uint8_t *dst, int width, int height) {
    const int mbw = width / 16, mbh = height / 16;
    for (int mby = 0; mby < mbh; ++mby)
        for (int mbx = 0; mbx < mbw; ++mbx) {
            uint8_t *mb = dst + (mby * mbw + mbx) * 0x400;
            for (int row = 0; row < 16; ++row) {
                const int y = mby * 16 + row;
                uint8_t *out = mb + row * 64;
                for (int col = 0; col < 16; ++col, out += 4) {
                    const int x = mbx * 16 + col;
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

bool decode(const uint8_t *stream, size_t bytes, int width, int height, uint8_t *dst) {
    const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_IPU);
    if (!dec) {
        log_error("ipum: FFmpeg has no IPU decoder (rebuild it with tools/build_ffmpeg.sh)");
        return false;
    }
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool ok = false;
    if (ctx && pkt && frame) {
        ctx->width = width;
        ctx->height = height;
        if (avcodec_open2(ctx, dec, nullptr) == 0 && av_new_packet(pkt, static_cast<int>(bytes)) == 0) {
            std::memcpy(pkt->data, stream, bytes);
            ok = avcodec_send_packet(ctx, pkt) == 0 && avcodec_receive_frame(ctx, frame) == 0;
            if (ok) store(frame, dst, width, height);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ok;
}

}  // namespace

// 0054C8F0 (mode, dest, file): 0 when the picture was decoded into dest, -1 otherwise (not an ipum
// file, an unknown mode, or a stream the decoder rejects: the IPU's error bits).
extern "C" int gt4_replace_func_0054C8F0(int mode, uint32_t dest, uint32_t file) {
    const auto *data = static_cast<const uint8_t *>(guest_ptr(file));
    if (std::memcmp(data, guest_ptr(kMagic), 4) != 0 || (mode != 0 && mode != 1)) return -1;
    uint32_t size;
    uint16_t width, height;
    std::memcpy(&size, data + 4, 4);
    std::memcpy(&width, data + 8, 2);
    std::memcpy(&height, data + 10, 2);
    if (size <= 16 || width < 16 || height < 16) return -1;
    // the stream ends at the first end code (the IPU stopped there; the size may include padding)
    std::vector<uint8_t> stream(data + 16, data + size);
    for (size_t k = 3; k < stream.size(); ++k)
        if (stream[k - 3] == 0 && stream[k - 2] == 0 && stream[k - 1] == 1 && stream[k] == 0xB0) {
            stream.resize(k + 1);
            break;
        }
    if (mode == 1) stream[0] = 0;   // mode 1 decodes with the default flags (IPU_CTRL 0)
    auto *out = static_cast<uint8_t *>(guest_ptr(dest));
    if (!decode(stream.data(), stream.size(), width, height, out)) {
        // black rather than what the buffer held (002771A8 ignores the result)
        for (uint32_t k = 0; k < uint32_t(width) * height; ++k) {
            out[4 * k] = out[4 * k + 1] = out[4 * k + 2] = 0;
            out[4 * k + 3] = 0x80;
        }
        log_warn("ipum: %ux%u picture at %08x (mode %d) did not decode", width, height, file, mode);
        return -1;
    }
    return 0;
}
