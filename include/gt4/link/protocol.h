/* The link between gt4_game.exe (32-bit: the game's code, the EE kernel and IOP services) and
 * gt4_host.exe (64-bit: window, Vulkan renderer, audio, input). One shared memory section, two
 * byte rings and a small state block; plain C so both compilers (and both pointer sizes) agree.
 *
 *   gt4_host.exe creates the section "Local\\gt4_link_<host pid>" (GT4_LINK_SIZE bytes) and starts
 *   gt4_game.exe with --link <name>. Without --link the game runs headless (streams are dropped
 *   or written to a capture file).
 *
 * Game -> host ring: the graphics and audio stream, as records (Gt4LinkRecord + payload, padded to
 * 16 bytes). The game's DMA-kick replacements walk the DMA chains in guest memory and send what the
 * hardware would have received: GIF packets per path, the VIF1 stream (with VU1 microprograms,
 * MSCAL and UNPACK data), GS privileged register writes, and a frame marker at each VBlank.
 * Host -> game: pad state, GS events (FINISH, SIGNAL, LABEL), image readback data, host commands.
 * Rings are single-producer single-consumer: the producer owns `head`, the consumer `tail`
 * (byte offsets, monotonic, taken modulo the ring size); payloads may wrap. */
#ifndef GT4_LINK_PROTOCOL_H
#define GT4_LINK_PROTOCOL_H

#include <stdint.h>

#define GT4_LINK_MAGIC 0x4B4E4C34u /* "4LNK" */
#define GT4_LINK_VERSION 1u
#define GT4_LINK_SIZE (128u << 20)
#define GT4_LINK_G2H_SIZE (96u << 20) /* game -> host ring */
#define GT4_LINK_H2G_SIZE (8u << 20)  /* host -> game ring */

enum Gt4LinkRecordType {
    /* game -> host */
    GT4_REC_GIF = 1,        /* GIF data for one path: u32 path (1, 2, 3), then qwords (GS packets) */
    GT4_REC_VIF1 = 2,       /* the VIF1 stream as the DMA would feed it: qwords of VIF codes and data */
    GT4_REC_VIF0 = 3,       /* the VIF0 stream (VU0 micro mode), rare */
    GT4_REC_VSYNC = 5,      /* VBlank: u32 field (CSR.FIELD), u32 vblank count; present the frame */
    GT4_REC_GS_READ = 6,    /* GS->EE transfer request (BUSDIR): u32 id, u32 qwc; the host answers GT4_REC_GS_IMAGE */
    GT4_REC_AUDIO = 7,      /* audio: u32 stream (0 SPU2 mix, 1 BGM, 2 voice, 3 movie), u32 rate, s16 stereo frames */
    GT4_REC_SPU2_REGS = 8,  /* the SPUP register image of a VBlank (2 x 0x1D0 bytes) */
    GT4_REC_SPU2_RAM = 9,   /* SPU2 RAM write: u32 address, then bytes */
    GT4_REC_LOG = 10,       /* text */
    GT4_REC_STATE = 11,     /* host captures only: the state a capture taken mid-run starts from (GraphicsStream::saveState) */
    GT4_REC_GFX_DATA = 12,  /* bytes a native record refers to (include/gt4/gfx.h): u32 game address, u32 bytes, then the bytes */
    GT4_REC_GFX = 13,       /* native records (include/gt4/gfx.h) that take effect now, not in the VIF1 stream's order */
    GT4_REC_DISPLAY = 14,   /* the display from the next VBlank on: include/gt4/gfx.h DisplayRecord */
    /* host -> game */
    GT4_REC_PAD = 64,       /* u32 port, u32 connected, 18 bytes libpad2 data (DualShock 2 layout) */
    GT4_REC_GS_EVENT = 65,  /* u32 kind (0 FINISH, 1 SIGNAL, 2 LABEL), u32 id, u64 value */
    GT4_REC_GS_IMAGE = 66,  /* u32 id, u32 qwc, then the downloaded qwords */
    GT4_REC_HOST_CMD = 67,  /* u32 command (1 quit, 2 pause, 3 resume, 4 gameplay option: u32 id, u32 value) */
};

/* Payload details as gt4_host consumes and produces them (docs/GS_BACKEND.md, "Link protocol"):
 *
 *   GT4_REC_GIF       u32 path, then the GIF qwords (they start 4 bytes into the payload, unaligned). A
 *                     GIF packet may continue in the next GIF record of the same path (per-path state).
 *   GT4_REC_VIF1      32-bit words in DMA order; with TTE the upper half of each DMA tag (two VIF codes)
 *                     precedes its data. A VIF command and its data may span records.
 *   GT4_REC_DISPLAY   sent before a VBlank's GT4_REC_VSYNC when the display changed (and at a capture
 *                     window's start).
 *   GT4_REC_VSYNC     ends the frame: the host scans out the display (both read circuits) at this point.
 *   GT4_REC_GS_READ   after the GIF data that set TRXDIR = 1 (local -> host): the host replies GS_IMAGE
 *                     with the same id and qwc qwords of transfer data (zeros past the end of the area).
 *   GT4_REC_STATE     never on the link: the first record of a capture the host recorded from a later VBlank
 *                     (GT4_HOST_RECORD=PATH,FROM,COUNT). Version 4: the uploaded VU1 program image, VIF1's
 *                     two batch buffers, the 3D renderers' constants, VIF1 registers, the display record and the
 *                     general GS registers last set, the native state, then GS memory (4 MiB in address order);
 *                     versions 2 and 3 had the display as the GS's privileged registers; version 1
 *                     (until 2026-10-30) had VU1's memories and registers in place of the buffers and
 *                     constants. The replay restores either (stream.cpp).
 *   GT4_REC_GS_EVENT  host -> game for SIGNAL / FINISH / LABEL written by any path (PATH1/2 included):
 *                     id = low 32 bits of the register value, value = the full 64-bit value.
 *   GT4_REC_PAD       26 bytes: u32 port, u32 connected, 18 bytes of libpad2 data; sent every host frame.
 *   GT4_REC_HOST_CMD  1 (quit) when the host closes; the host terminates the game 3 s later if needed. 2 (pause)
 *                     and 3 (resume) around the host's settings menu: the game's bus clock stands still. 4: a
 *                     gameplay option (runtime.h GameOption) changed, or its value at start. */

typedef struct Gt4LinkRecord {
    uint32_t type;
    uint32_t size; /* payload bytes after this header */
    uint64_t seq;  /* producer's record counter */
} Gt4LinkRecord;   /* 16 bytes; records start 16-byte aligned */

typedef struct Gt4LinkRing {
    volatile uint64_t head; /* bytes written (producer) */
    uint8_t pad0[56];
    volatile uint64_t tail; /* bytes consumed (consumer) */
    uint8_t pad1[56];
} Gt4LinkRing;

typedef struct Gt4LinkHeader {
    uint32_t magic, version;
    uint32_t g2h_offset, g2h_size; /* ring data offsets from the section start */
    uint32_t h2g_offset, h2g_size;
    volatile uint32_t game_pid, host_pid;
    volatile uint32_t game_state; /* 0 starting, 1 running, 2 exited */
    volatile uint32_t frame;      /* VBlanks the game has sent */
    uint8_t pad[16];
    Gt4LinkRing g2h, h2g;
} Gt4LinkHeader;

#endif
