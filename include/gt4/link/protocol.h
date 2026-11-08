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
#define GT4_LINK_VERSION 5u /* 2: GT4_REC_GFX_DATA references to set uploads the host keeps (gfx::kDataReference);
                                3: the graphics stream may carry scene records (gfx::kScene, docs/modern/CONTRACTS.md (a));
                                4: the settings pages' records (GT4_REC_UI, _SETTINGS_SET, _SETTINGS; host commands
                                   6, 7);
                                5: the graphics stream carries the origin's parts (gfx::kOriginParts, R6) */
#define GT4_LINK_SIZE (128u << 20)
#define GT4_LINK_G2H_SIZE (96u << 20) /* game -> host ring */
#define GT4_LINK_H2G_SIZE (8u << 20)  /* host -> game ring */

enum Gt4LinkRecordType {
    /* game -> host */
    /* 1-4 retired (PLAN.md P1): the GS packets and VIF streams of the game before its records went native. The game
       sends none; the host refuses a capture that holds them. The numbers stay reserved so it can tell. */
    GT4_REC_GIF = 1,
    GT4_REC_VIF1 = 2,
    GT4_REC_VIF0 = 3,
    GT4_REC_GS_PRIV_RETIRED = 4,
    GT4_REC_VSYNC = 5,      /* VBlank: u32 field (CSR.FIELD), u32 vblank count; present the frame */
    GT4_REC_GS_READ = 6,    /* GS->EE transfer request (BUSDIR): u32 id, u32 qwc; the host answers GT4_REC_GS_IMAGE */
    GT4_REC_AUDIO = 7,      /* u32 stream (0 the game's mix, 1 BGM + movies, host mixing), u32 rate, s16 L/R frames */
    GT4_REC_SPU2_REGS = 8,  /* the SPU2 for a host mixer: u32 kind (GT4_SPU2_*), u32 value, then the kind's bytes */
    GT4_REC_SPU2_RAM = 9,   /* SPU2 RAM write for a host mixer: u32 address, then bytes */
    GT4_REC_LOG = 10,       /* text */
    GT4_REC_STATE = 11,     /* host captures only: the state a capture taken mid-run starts from (GraphicsStream::saveState) */
    GT4_REC_GFX_DATA = 12,  /* bytes a native record refers to (include/gt4/gfx.h): u32 game address, u32 bytes, then the bytes */
    GT4_REC_GFX = 13,       /* native records (include/gt4/gfx.h) that take effect now, not in the VIF1 stream's order */
    GT4_REC_DISPLAY = 14,   /* the display from the next VBlank on: include/gt4/gfx.h DisplayRecord */
    GT4_REC_RUMBLE = 15,    /* the pad's motors from now on: u32 port, u32 small (0 / 1), u32 large (0-255) */
    GT4_REC_UI = 16,        /* a settings page's draw list (include/gt4/ui.h wire::UiHeader...); empty at close */
    GT4_REC_SETTINGS_SET = 17, /* a settings page's request (include/gt4/ui.h wire::SetHeader, key, value) */
    /* host -> game */
    GT4_REC_PAD = 64,       /* u32 port, u32 connected, 18 bytes libpad2 data (DualShock 2 layout) */
    GT4_REC_GS_EVENT = 65,  /* u32 kind (0 FINISH, 1 SIGNAL, 2 LABEL), u32 id, u64 value */
    GT4_REC_GS_IMAGE = 66,  /* u32 id, u32 qwc, then the downloaded qwords */
    GT4_REC_HOST_CMD = 67,  /* u32 command (1 quit, 2 pause, 3 resume, 4 gameplay option: u32 id, u32 value,
                               5 texture contents lost, 6 open settings, 7 close settings) */
    GT4_REC_SETTINGS = 68,  /* the settings' schema, values or a request's result (include/gt4/ui.h wire::SettingsHeader) */
};

/* Payload details as gt4_host consumes and produces them (docs/GS_BACKEND.md, "Link protocol"):
 *
 *   GT4_REC_DISPLAY   sent before a VBlank's GT4_REC_VSYNC when the display changed (and at a capture
 *                     window's start).
 *   GT4_REC_VSYNC     ends the frame: the host scans out the display (both read circuits) at this point.
 *   GT4_REC_GS_READ   after the read record (include/gt4/gfx.h kRead) in the records: the host replies GS_IMAGE
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
 *                     and 3 (resume) around the host's settings menu: the game's bus clock stands still (retired
 *                     with that menu, MENU_DESIGN M9; the numbers stay reserved). 4: a gameplay option (runtime.h
 *                     GameOption) changed, or its value at start. 5: the host lost a texture content it was
 *                     counted on to keep; the game sends every content again (runtime/frame.cpp). 6: open the
 *                     settings pages at the next safe point; 7: close them (the host is shutting down).
 *   GT4_REC_UI        game -> host, once a game frame while a settings page is open, and one empty list at close:
 *                     drawn with the FrameList of its vblank, kept on interpolated presents until the next list
 *                     (docs/modern/MENU_DESIGN.md 3.6). At most 64 KiB; a longer list is dropped and counted.
 *   GT4_REC_SETTINGS_SET  game -> host: set, revert or act on a setting by key; the host answers with a
 *                     GT4_REC_SETTINGS result carrying the request's number, then the values that changed.
 *   GT4_REC_SETTINGS  host -> game: the schema (at the game's request), values, or a request's result.
 *   GT4_REC_SPU2_REGS, _RAM  game -> host while audio.mixer = host (runtime.h kOptionHostMixer, PLAN.md D5):
 *                     in the order the game's SPU2 sees them. GT4_SPU2_STATE (value 0, then the voices' state,
 *                     platform/audio/spu Spu2::saveState) once at the switch, after the whole SPU RAM (one RAM write
 *                     at 0); GT4_SPU2_IMAGE (value 0, then SPUP fno 4's 2 x 0x1D0 bytes); GT4_SPU2_RUN (value: the
 *                     samples at 48 kHz the SPU2 runs, once a frame). Stream 1 of GT4_REC_AUDIO then carries the
 *                     frame's BGM and movie sound, the same count of samples, for the host to add to its mix. */

/* The gameplay options GT4_REC_HOST_CMD 4 carries (runtime/runtime.h GameOption): ids below GT4_GAME_OPTION_COUNT. */
#define GT4_GAME_OPTION_COUNT 11u

enum Gt4Spu2Kind { GT4_SPU2_STATE = 0, GT4_SPU2_IMAGE = 1, GT4_SPU2_RUN = 2 };   /* GT4_REC_SPU2_REGS's kinds */

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
    volatile uint32_t frame;      /* VBlanks the game has sent (the newest VSYNC record's count) */
    volatile uint32_t game_cpu_ms; /* the game process's CPU time, every thread (it writes it: Wine's
                                      GetProcessTimes on another process gives the caller's own) */
    /* The host: the VBlank count of the newest frame its renderer has drawn (or skipped); 0 from a host that does
     * not say. The game's frame kick waits while it is more than a few VBlanks ahead of it (runtime/link.cpp
     * link_wait_for_host), counting its waits here. */
    volatile uint32_t host_frame_drawn;
    volatile uint32_t game_host_waits, game_host_wait_ms;
    Gt4LinkRing g2h, h2g;
} Gt4LinkHeader;

#endif
