# The GS back end: GT4's frames in gt4_host

`gt4_host` displays what the PS2 would have shown: it takes the graphics stream of `gt4_game.exe` (the
game's own code on the PS2 memory map, `docs/ARCHITECTURE.md`) over the shared-memory link, runs it
through a VIF1 + VU1 front end and a GS back end, reads out the displayed frame the way the CRTC does, and
hands that frame to the display pipeline (`docs/RENDERING.md`) as the scene. The game never renders
itself; the host never runs game logic.

```
gt4_game.exe (32-bit)                         gt4_host.exe (64-bit)
 DMA-kick replacements walk the DMA chains     GameLink (platform/gs/link_host.*): section, process, rings
 GIF PATH3, VIF1 stream, GS_PRIV, VSYNC  ==>    copies records out of the ring (worker thread)
                                                 GraphicsStream (stream.*)
 pad state, GS events, GS_IMAGE          <==      VIF1 (vif1.*)  -> UNPACK/MPG into VU1 memory, DIRECT -> PATH2
                                                  VU1 (vu1.*)    -> microprograms, XGKICK -> PATH1
                                                  GS back end (gs_backend.*): paraLLEl-GS, PATH1/2/3,
                                                   privileged registers, VSYNC scanout -> CPU frame
                                                 GsHost (gs_host.*) -> newest frame -> GsScene (render/gs_scene.*)
                                                   -> display pipeline: upscaling, AA, post, 2D layer, output
```

## Running it

```bat
tools\build_windows.cmd Release
:: a live game: gt4_host creates the link, starts the game with --link NAME [ARGS] and shows its frames
build\host-windows-Release\gt4_host.exe --game path\to\gt4_game.exe --game-args "--elf C:/.../CORE.GT4.elf"
:: a capture (the game's --capture output: the same records, concatenated)
gt4_host --replay build\cap.bin [--replay-loop]
:: headless, one VBlank per rendered frame (deterministic), PS2-faithful, 640 x 448 PPM of the last frame
gt4_host --headless --replay build\cap.bin --frames 120 --size 640x448 --preset original ^
         --set aspect.ratio=window --set debug.overlay=off --capture build\captures\frame.ppm
```

| Option | Effect |
|---|---|
| `--game PATH` | Creates `Local\gt4_link_<host pid>`, starts `PATH --link NAME`, streams its frames. When the game exits (or crashes) its exit code is logged (`Link: gt4_game exited with code ... (0x...): access violation`), the host closes and returns 3 if the code was not 0. Closing the host sends `GT4_REC_HOST_CMD` 1 (quit) and terminates the game after 3 s. Windows only. |
| `--game-args "ARGS"` | Extra game arguments after `--link NAME` (split on spaces, `"..."` groups). |
| `--replay FILE` | Plays a capture instead of a game. Headless runs (`--fixed-step`) advance one VBlank per rendered frame, so `--frames N --capture` is reproducible; otherwise it plays at 59.94 Hz. `--replay-loop` restarts at the end. |
| `--gs-ssaa N` | paraLLEl-GS super-sampling: 1 (faithful, default), 2, 4, 8, 16. |
| `--gs-hires` | With 4x or more: scan out at twice the resolution (1280 x 896) from the super-samples. |

- **Aspect**: the PS2 picture is a 4:3 TV image with non-square pixels (640 x 448), so `aspect.ratio`
  defaults to `4:3` in these modes; `--set aspect.ratio=...` overrides it. Widescreen belongs to the game
  (`docs/WIDESCREEN.md`).
- **"Original" preset**: shows the GS frame unaltered. The frame is uploaded as an sRGB texture (so the
  pipeline sees linear values), and with native resolution, no AA, the `original` (clamp) display transform,
  fixed exposure and no dither the output encoding gives the same 8-bit values back; effects set to
  `original` add nothing, because the game's image already contains its own glare and fog
  (`SceneSource::drawsOriginalEffects`). Checked by `host_replay` (below): the sampled pixels come back
  with their exact values (the check allows +-1).
- **Other presets** apply the pipeline to the frame: upscaling (FSR 1/2, Lanczos...), AA, tone mapping,
  bloom, sharpening. There are no motion vectors (zero) or depth for the GS frame yet, so the temporal paths
  (TAA, FSR 2) only see a static image; see "Next steps".
- **Headless live runs** are paced at 59.94 Hz. The 2D overlay readout shows the link status (`F1` hides it).
- **Keys** with a game: only `F1`-`F12` and `Alt+Enter` change settings; the rest is the pad.

### Input (pad 1, `GT4_REC_PAD` every host frame)

libpad2 DualShock 2 layout (18 bytes; `docs/IOP_SERVICES.md` section 7, `platform/gs/ps2_pad.h`):
buttons active low, sticks 0x00/0x80/0xFF, twelve pressure bytes.

| PS2 | SDL gamepad | Keyboard |
|---|---|---|
| Cross / Circle / Square / Triangle | south / east / west / north | X / C / Z / V |
| D-pad | D-pad | arrows |
| L1 / R1 | shoulders | Q / E |
| L2 / R2 | triggers: analogue pressure, digital from 25 % | 1 / 3 |
| L3 / R3 | stick buttons | |
| Start / Select | start / back | Enter / Backspace or right Shift |
| left / right stick | sticks (8 % dead zone) | J L I K (left stick) |

Face buttons of a digital pad report pressure 0 or 0xFF. Port 2 is reported disconnected.

## Link protocol as used

`include/gt4/link/protocol.h` is the contract (records of `Gt4LinkRecord` + payload, 16-byte aligned, two
single-producer rings). The host side, `platform/gs/link_host.*`:

- creates the section (header: magic, version, `g2h` ring at offset 4096 (96 MiB), `h2g` ring after it
  (8 MiB), `host_pid`), then starts the game; a monitor thread waits for the process and sets
  `game_state = 2`;
- copies every complete record out of the game -> host ring in one batch, releases the space (`tail`), then
  decodes them on the GS worker thread, so the game's producer never waits on GS work;
- writes host -> game records under a lock (pad from the main thread, GS events and images from the worker).

Records consumed: `GIF` (path 1/2/3), `VIF1`, `GS_PRIV`, `VSYNC`, `GS_READ`, `LOG` (printed as `[game] ...`);
`VIF0` (VU0 micro mode) and the audio records are counted and ignored here. Records produced: `PAD`,
`GS_EVENT`, `GS_IMAGE`, `HOST_CMD`. The payload details the host relies on are listed in `protocol.h` (no
new record types were needed). Capture files use the same framing, so `--replay` and the live link share
the decoder (`GraphicsStream`, `platform/gs/stream.*`).

### What the host needs from the game side

Today (`runtime/gs.cpp`, `runtime/link.cpp` in `gt4-native-game`) the game sends `GS_PRIV` for PMODE..BGCOLOR
when they change, `VSYNC {field, count}` each VBlank, VIF1/VIF0/GIF PATH3 streams from the DMA kicks, and
polls `PAD`/`HOST_CMD` at VBlank. Requests:

1. **SMODE1**: the game never writes it (the kernel's `SetGsCrt` does). The host assumes NTSC interlaced
   (`0x740834504`, BIOS value) and ignores zero writes of SMODE1/SRFSH/SYNCH1/SYNCH2/SYNCV. If the kernel
   implements `SetGsCrt`, sending its real values is welcome.
2. **GS events from PATH1/PATH2**: the game raises FINISH/SIGNAL itself by scanning PATH3 packets, but
   packets kicked by VU1 (XGKICK) or VIF1 DIRECT are only decoded on the host. The host sends
   `GS_EVENT {kind 0 FINISH / 1 SIGNAL / 2 LABEL, id = low 32 bits, value = register value}` for every one;
   the game should set CSR / SIGLBLID and raise INTC 0 from them (and not double-raise PATH3 ones; the
   host also reports PATH3 events). `link_poll()` currently ignores `GS_EVENT`.
3. **GS -> EE downloads** (`004AB930`/`004AB9C0` write BUSDIR): send `GS_READ {id, qwc}` after the GIF data
   that set TRXDIR = 1, then wait for `GS_IMAGE {id, qwc, qwc x 16 bytes}`. `link_poll()` reads at most 256
   payload bytes per record today: it needs a larger buffer (or a direct copy into guest memory) for images.
4. **VIF1 with TTE**: send the two upper words of each DMA tag before its data (as `walk_chain` does), as
   32-bit words; commands may span records.
5. **Interlace mode**: the game calls `sceGsResetGraph(0, 1, 2, 1)` (FFMD = 1). paraLLEl-GS treats FFMD = 1
   as "each field reads every line of a half-height buffer" (field rendering) and FFMD = 0 as "a
   full-height buffer read every other line". Whichever GT4 really draws, paraLLEl-GS follows the registers;
   if frames show only half the picture once the game renders, compare `DISPLAY.DH` and the buffer height.

## GS back end

`platform/gs/gs_backend.*` wraps **paraLLEl-GS** (Arntzen Software, LGPL-3.0+, pinned commit `cc6184af`
with Granite `16e7395f`; `cmake/HostDependencies.cmake`), a GS implemented in Vulkan compute shaders with
precompiled SPIR-V. It builds with MSVC x64 and GCC as a library (`PARALLEL_GS_STANDALONE`; no SDL, no
runtime shader compiler, no Fossilize, no RenderDoc hook).

- **Own Vulkan device**: Granite creates an instance and a device on the renderer's GPU (matched by name);
  the GS runs on the worker thread. The host and Granite share one volk: Granite keeps per-device function
  tables, and the renderer reloads its global pointers after the GS device is created
  (`Renderer::reloadVulkanFunctions`).
- **Requirements** (checked; otherwise `--game/--replay` fail with the reason and the GPU tests skip):
  descriptor indexing, timeline semaphores, buffer device address, 8/16-bit storage, int16, scalar block
  layout, subgroup operations with size control 4..64, 32 KiB shared memory. Works on the RX 7900 XTX and
  the integrated Radeon of the test machine.
- **Input**: `gif(path, data)` per path (paraLLEl-GS keeps per-path GIFtag state, so packets may be split),
  privileged registers into its register file (CSR writes clear the event bits), `vsync(field)`.
- **Scanout** at each `VSYNC`: paraLLEl-GS reads both circuits (PMODE, DISPFB1/2, DISPLAY1/2, BGCOLOR,
  SMODE1/2) with `force_progressive` and `anti_blur` (as PCSX2's integration): interlaced content becomes one
  progressive 640 x 448 frame, the two-circuit flicker filter is undone. The image is copied to the CPU
  (640 x 448 x 4 bytes, one fence per VBlank) and published to the main thread, which uploads it into the
  pipeline's scene. External-memory sharing would remove the copy (next steps).
- **Events**: SIGNAL, FINISH and LABEL writes reach `GS_EVENT`. **GS -> EE**: after TRXDIR = 1,
  `readTransfer(qwc)` returns the transfer FIFO (`GS_READ` -> `GS_IMAGE`).
- **Super-sampling** (`--gs-ssaa`, `--gs-hires`) is paraLLEl-GS's; 1x is the faithful default.
- A GIFtag's PRE/PRIM fields only apply in PACKED mode (hardware behaviour, followed by paraLLEl-GS).

### Fallback

The task allowed a minimal GS of our own if paraLLEl-GS proved unsuitable. It did not: the licence is
GPL-compatible (LGPL-3.0+, not MIT as first assumed; see `THIRD_PARTY_NOTICES.md`), it builds on 64-bit
Windows with MSVC 14.44 and on Linux, and it passes the pixel tests. No own GS was written.

## VIF1 + VU1

GT4's 3D runs through VU1 microprograms the game uploads with VIF1 `MPG`. They run natively, in C++
(`platform/gs/vu1_native.cpp`, described in `docs/VU1_PROGRAMS.md`), bit for bit as the reference
interpreter computes them. The interpreters stay behind the CMake option `GT4_VU1_INTERPRETER` (ON by
default): development tools (`GT4_VU1=interp|ref`, `GT4_VU1_DIFF=1|native`, `GT4_VU1_TRACE`,
`GT4_VU1_INVENTORY`) and the native code's fallback for the polygon clipper's rare record overflow. With the
option OFF only GT4's native programs run.

- **VIF1** (`platform/gs/vif1.*`): NOP, STCYCL, OFFSET, BASE, ITOP, STMOD (offset, difference, mode 3),
  MSKPATH3 (GT4 emits it, `004A1950`: PATH3 records that arrive while it is set are held and sent at the
  unmask, or at the VBlank if it is never lifted), MARK, FLUSHE/FLUSH/
  FLUSHA (nothing to wait for: VU1 runs synchronously), MSCAL/MSCALF/MSCNT (with the TOPS/ITOPS double-buffer
  swap), STMASK, STROW, STCOL, MPG, DIRECT/DIRECTHL (-> PATH2), and UNPACK in every format (S/V2/V3/V4 x
  32/16/8, V4-5) with sign/zero extension, masking (data/row/column/protect), skipping and filling writes,
  and FLG (+TOPS). A command may span records.
- **VU1** (`platform/gs/vu1.*`): 16 KiB micro / 16 KiB data memory; every upper instruction (ADD, SUB,
  MUL, MADD, MSUB with broadcast/I/Q/ACC forms, MAX, MINI, OPMULA, OPMSUB, ITOF/FTOI 0/4/12/15, ABS, CLIP,
  NOP) and lower instruction (LQ/SQ/LQI/SQI/LQD/SQD, ILW/ISW/ILWR/ISWR, integer ALU, MOVE, MR32, MFIR, MTIR,
  flag instructions, branches and jumps with delay slots, DIV/SQRT/RSQRT/WAITQ, the EFU set with
  MFP/WAITP, random numbers, XTOP/XITOP, XGKICK -> PATH1); the I bit; E-bit termination after the delay
  slot. Model: one pair per cycle; FMAC results 4 cycles after issue with stalls on dependent reads; MAC,
  status and clip flags visible to lower-slot instructions 4 cycles after the producing instruction; Q after
  7 (DIV, SQRT) or 13 (RSQRT) cycles and P after the EFU latency; a branch directly after the instruction
  that wrote its VI register reads the previous value; when both slots write the same VF register the lower
  one is dropped. Floats follow the VU: no denormals, infinities or NaNs (inputs clamp, results saturate to
  +-max with O, underflow gives a signed zero with U), arithmetic computed in double and rounded toward
  zero. XGKICK sends the whole packet at once.
- **Not modelled**: VU0 / VIF0 (records counted only), XGKICK running concurrently with later
  instructions, the D/T debug bits, VIF interrupts (`I` bit) and stalls, exact EFU results (host libm).
- **Provenance**: written for this project from the public documentation; PCSX2 (GPL-3.0+) consulted for
  undocumented timing details (`THIRD_PARTY_NOTICES.md`).

## Tests

All in `ctest` (`tools\build_windows.cmd` runs them):

| Test | What | GPU |
|---|---|---|
| `ps2gfx` | VU1 arithmetic (ADD, broadcast MUL, MULA+MADD, DIV+WAITQ+MULq, FTOI4), branch delay slot and the VI read quirk, clip-flag latency, overflow saturation and flags, XGKICK packet bytes; VIF1 UNPACK formats, row/column/protect masking, offset mode, skipping and filling writes, FLG/TOPS double buffering, a command split across records, DIRECT -> PATH2; the transform microprogram (MPG + UNPACK + MSCAL) kicking the expected PATH1 packet; record decoding (GIF, PRIV, VSYNC, GS_READ -> GS_IMAGE); MSKPATH3 holding PATH3; capture write/replay; pad encoding. Prints the interpreter's speed. | no |
| `gs_backend` | paraLLEl-GS by pixel: sprites and their exclusive edges, alpha blending, a textured triangle (PSMCT32, nearest), a CLUT texture (PSMT8 + PSMCT32 palette), a GS -> EE download, FINISH/SIGNAL events; a VU1 microprogram transforming a triangle and drawing it through XGKICK; a capture replayed through `GsHost` (the host's worker thread, lockstep). Writes the capture for the next test. | yes (77 = skipped) |
| `host_replay` | `gt4_host --headless --replay ... --preset original --capture` at 640 x 448, then the PPM checked by pixel (`tests/host_replay.cmake`). | yes (skipped without the capture) |

Without a suitable GPU (CI) the GPU tests are reported as skipped. Captures and images go to the build
directory (`gs_test_output/`), never into git; no game data is used by the tests.

Live link, checked by hand with the game of 2026-10-25 (which reaches VBlanks but does not draw yet):
`gt4_host --headless --frames 240 --game gt4_game.exe --game-args "--elf ..."` connects, receives 239
VBlanks and 15 privileged writes in 4.2 s, and the game exits with code 0 on the host's quit command; with
a missing ELF the game's exit code 2 is logged and the host returns 3.

## Performance

RX 7900 XTX, Release, Windows:

- **VU1 interpreter**: about 30 million instruction pairs per second (about 75 million VU cycles per second
  counting stalls) on the transform loop (`ps2gfx` prints it). The real VU1 runs at about 300 MHz, so a
  frame that keeps VU1 busy would take about four times its real duration: acceptable for development, a
  reason for native translation.
- **GS + scanout + CPU copy**: the replay test scene (two sprites, a VU1 triangle) runs at about 1.7 ms per
  VBlank including the fence wait and the 640 x 448 readback (1200 lockstep frames in 2.1 s); 3.2 ms with
  4x SSAA. The display pipeline adds 0.18 ms GPU per frame with the original preset at 1440 x 1080.
- paraLLEl-GS compiles some pipelines at first use (`Stalled compile` warnings in the log, 10-60 ms once).

## Next steps

1. Run real GT4 streams as soon as the game draws (captures under `build/`), and fix what they reveal in
   VIF/VU timing (XGKICK concurrency) and the interlace mode (see request 5). One ordering question needs
   real data: records are processed whole and in arrival order, so a PATH3 transfer kicked after a VIF1
   chain that unmasks PATH3 in its middle lands after the whole chain, not at the unmask point. If GT4's
   frames show late textures, the VIF1 decoder can pause at an unmask until the next record is known.
2. Share the scanout image with the renderer's device (Vulkan external memory, both devices are on the
   same GPU) instead of the CPU copy.
3. Motion vectors and depth for the temporal upscalers: with super-sampling, scan out high-resolution; for
   FSR 2 / TAA on game frames, derive motion from the VU1 inputs (`docs/RENDERING.md`, scene contract).
4. A faster VU1 path (predecoded instructions), then the native translation of GT4's microprograms that
   retires the scaffold.
5. GS events and downloads on the game side (requests 2 and 3), and SMODE1 from `SetGsCrt`.
