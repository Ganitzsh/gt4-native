# GT4's frames in gt4_host

`gt4_host` displays what the game draws: it takes the graphics stream of `gt4_game.exe` (the game's own code
on the PS2 memory map, `docs/ARCHITECTURE.md`) over the shared-memory link, decodes its VIF1 stream into 3D
batches that GT4's renderers (native code, `docs/VU1_PROGRAMS.md`) turn into primitives for the native
renderer (`docs/NATIVE_RENDERER.md`), which draws them with Vulkan into the display pipeline
(`docs/RENDERING.md`). The game never renders itself; the host never runs game logic.
No GS is emulated: paraLLEl-GS, the GS emulator the host used until 2026-10-30, is only a test reference now
(`GT4_GS_REFERENCE`, below).

```
gt4_game.exe (32-bit)                         gt4_host.exe (64-bit)
 DMA-kick replacements walk the DMA chains     GameLink (platform/gs/link_host.*): section, process, rings
 GIF PATH3, VIF1 stream, GS_PRIV, VSYNC  ==>    copies records out of the ring (worker thread)
                                                 GraphicsStream (stream.*)
 pad state, GS events, GS_IMAGE          <==      VIF1 (vif1.*) -> 3D batches (UNPACK into two batch buffers,
                                                   MSCAL), DIRECT -> PATH2
                                                  3D renderers (renderers3d.*) -> primitives with GS state
                                                  front end (gsnative/gs_frontend.*): primitives, PATH2/3, privileged
                                                   registers, SIGNAL/FINISH/LABEL, GS -> host reads
                                                 GsHost (gs_host.*) -> every frame list, in order
                                                   -> GsnScene (render/gsn_scene.*): Vulkan, at a render scale
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

- **Aspect**: the PS2 picture is a 4:3 TV image with non-square pixels (640 x 448), so `aspect.ratio`
  defaults to `4:3` in these modes; `--set aspect.ratio=...` overrides it. Widescreen belongs to the game
  (`docs/WIDESCREEN.md`).
- **"Original" preset** (`render.gs = original`): the native renderer at the PS2's own resolution, one
  render pixel per GS pixel, shown unaltered: native resolution, no AA, the `original` (clamp) display
  transform, fixed exposure and no dither give the drawn 8-bit values back; effects set to `original` add
  nothing, because the game draws its own glare and fog (`SceneSource::drawsOriginalEffects`). Checked by
  `host_replay` (below): the sampled pixels come back with their exact values (the check allows +-1).
  `render.gs = emulated`, from before, is read as `original`.
- **Other presets** (`render.gs = native`) draw at the nearest whole scale to the render resolution and apply
  the pipeline: upscaling (FSR 1/2, Lanczos...), AA, tone mapping, bloom, sharpening. There are no motion
  vectors (zero) for the game's frames yet, so the temporal paths (TAA, FSR 2) only see a static image.
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
   Done since: `gs_event()` applies them, masked by the IMR register the game writes directly
   (`0x12001010`: 004A0808 writes `~0x200` to unmask FINISH; the `GsPutIMR` syscall writes it too). FINISH
   written by VIF1 DIRECT packets is raised by the runtime as it sends them (`link_local_finish`), and the
   host's later report dropped: the host draws behind the game (0.8 s during a race load), and GT4 starts
   its GS -> EE downloads on that FINISH (004AB668 and the DMA handler 004AB6D8 both count to 2). Reported
   late, the download landed after the game had stopped waiting and reused the buffer (the loading
   screen's course photo: noise, then a crash or a corrupted race).
3. **GS -> EE downloads** (`004AB930`/`004AB9C0` write BUSDIR): send `GS_READ {id, qwc}` after the GIF data
   that set TRXDIR = 1, then wait for `GS_IMAGE {id, qwc, qwc x 16 bytes}`. `link_poll()` reads at most 256
   payload bytes per record today: it needs a larger buffer (or a direct copy into guest memory) for images.
4. **VIF1 with TTE**: send the two upper words of each DMA tag before its data (as `walk_chain` does), as
   32-bit words; commands may span records.
5. **Interlace mode**: the game calls `sceGsResetGraph(0, 1, 2, 1)` (FFMD = 1): each field reads every line
   of a half-height buffer (field rendering); the native renderer gives such a field's lines the full output
   height (`docs/NATIVE_RENDERER.md`).

## GS commands: the native renderer

The worker thread feeds every GIF packet (PATH1 from VU1, PATH2 from VIF1 DIRECT, PATH3) and privileged
register write to `gsn::GsFrontend` (`platform/gsnative/`), which keeps GS memory and the drawing state and
makes one frame list per VBlank; `GsnScene` (`platform/render/gsn_scene.*`) draws the lists with Vulkan on the
render thread. Design and state: `docs/NATIVE_RENDERER.md`. What the host owes the game besides pictures:

- **Events**: SIGNAL, FINISH and LABEL, as packets of any path write them, go to the game as `GS_EVENT
  {kind, id, value}` (the game applies CSR, IMR and SIGLBLID: `runtime/link.cpp` `gs_event`).
- **GS -> host reads** (`GS_READ` -> `GS_IMAGE`): GT4 renders car thumbnails at a race's loading (six
  opponents, front 96 x 64 and side 64 x 32, and the player's car 64 x 64, into 16-bit targets) and reads
  them into EE memory. A read of pages a draw wrote, of one colour target in its own layout, is served by
  the GPU: the front end cuts the frame list at the read (`GsFrontend::cut`), the worker hands it with the
  rectangle to the render thread (`GsHost::takeReadRequest`) and waits; the renderer draws every list not
  drawn yet, copies the rectangle back and packs it in the target's format (`GsnScene::read`), one GS pixel
  per render pixel at its own sample point. Other reads come from the memory model. A replay has no game
  to answer, so its reads stay in memory. Against paraLLEl-GS on the race route: the 38 reads give the same
  pictures, 89 % of the pixels identical, 99.6 % within 4/32 per channel (`GT4_GS_READ_DUMP=DIR` writes
  what each read returned).
- **Every frame list is drawn**, in order, even when the render thread falls behind (the host queues them;
  the renderer executes all pending ones before showing the newest): later frames and reads see what each
  one drew. Past 64 queued lists (a minimised window) the oldest lists' draws are dropped, their texture
  uploads kept.
- **State save** (`GT4_HOST_RECORD` from a later VBlank): GS memory comes from the front end's model.

### paraLLEl-GS as a test reference

`cmake -DGT4_GS_REFERENCE=ON` (off by default) fetches **paraLLEl-GS** (Arntzen Software, LGPL-3.0+, pinned
commit `cc6184af` with Granite `16e7395f`; `cmake/HostDependencies.cmake`) and builds `gt4_gs_reference`
(`platform/gs/gs_backend.*`, a GS implemented in Vulkan compute shaders on its own device) for two tests:
`gs_backend` (paraLLEl-GS by pixel) and `gs_native` (the native renderer's GS memory model against
paraLLEl-GS's after the same transfers). gt4_host never links it; without the option the host's volk
loader is `gt4-volk`.

## VIF1 and GT4's 3D renderers

GT4's 3D ran through VU1 microprograms the game uploads with VIF1 `MPG`. On the host they are native renderers
(`platform/gs/renderers3d.cpp`, described in `docs/VU1_PROGRAMS.md`): no VU1 memory, registers, interpreter or
XGKICK in `gt4_host` (`docs/NATIVE_ONLY.md`).

- **VIF1** (`platform/gs/vif1.*`): decodes the game's VIF codes into what they carry. UNPACK in every format
  (S/V2/V3/V4 x 32/16/8, V4-5) with sign/zero extension, masking (STMASK, STROW, STCOL: data/row/column/protect),
  STMOD (offset, difference, mode 3), skipping and filling writes (STCYCL); FLG writes relative to the current
  batch buffer: BASE and BASE + OFFSET take turns at each MSCAL/MSCALF (MSCNT goes on where the last renderer
  ended). Each buffer keeps what it was last sent and what the renderers decoded into it (GT4 re-sends only
  what changes, and draws some batches twice: `docs/VU1_PROGRAMS.md`). An MSCAL hands the batch to the
  renderers (`Vif1::onBatch`). DIRECT/DIRECTHL go to the GS as PATH2; MSKPATH3 (GT4 emits it, `004A1950`: PATH3
  records that arrive while it is set are held and sent at the unmask, or at the VBlank if it is never lifted);
  ITOP, OFFSET, BASE, MARK, NOP, FLUSHE/FLUSH/FLUSHA (nothing to wait for). MPG: the uploaded image is kept
  only to check it is GT4's (another image draws nothing). A command may span records.
- **3D renderers** (`platform/gs/renderers3d.*`): the entry table, the setup constants (kept for the entries
  that skip the setup), the lighting chain (decompression and 15 lighting bodies), the six strip renderers with
  back-face culling and the polygon clipper, and the screen grids, in plain float arithmetic; where a VU number
  rule matters for the picture the code states it (`docs/VU1_PROGRAMS.md`). Output through `PrimitiveSink`
  (`gs_primitives.h`): runs of vertices with PRIM from the batch's GIF tag (ST/Q, RGBA, XYZ/F and ADC as the
  GS registers receive them), and the GS packets the game wrote (a batch's state header, a screen grid's
  template with its stepped fields), whole. The host's front end takes the vertices through
  `GsFrontend::setPrim` / `vertex`.
- **Provenance**: VIF written for this project from the public documentation; PCSX2 (GPL-3.0+) consulted for
  undocumented details of UNPACK (`THIRD_PARTY_NOTICES.md`). The renderers were written from GT4's micro image.

### The VU1 reference (development builds)

`cmake -DGT4_VU1_INTERPRETER=ON` (OFF by default) builds `tools/vu1ref/` into the host: a VU1 memory model fed by
what VIF1 decodes (`Vif1::Observer`), the interpreter (`vu1.cpp`, with the VU's pipeline timing), a second
interpreter (`vu1_ref.cpp`) and the bit-exact C++ port of GT4's programs that the host ran until 2026-10-30
(`vu1_exact.cpp`, VU arithmetic in `vu_float.h`). `tools/vu1ref/vu1_reference.*` puts them beside the native path:

- `GT4_VU1=exact|interp|ref`: draw with that reference (GIF packets on PATH1, the old path); a micro image
  that is not GT4's always runs in the interpreter (the tests' toy programs).
- `GT4_VU1_DIFF=1` (`=interp`, `=ref`): every batch also runs in the reference; both outputs are compared as GS
  register writes and vertices (what the front end makes its draws and vertex lists of): identical, within float
  rounding (an eighth of a pixel and 1/4096 of the distance from the window's centre, Z by 64 + Z/8192 of
  2^24, colours by 2, ST by 1e-3, the same ADC), or beyond; a table per entry and lighting body at exit.
  `GT4_VU1_DIFF_DETAIL=1` prints the first differences, `GT4_VU1_DIFF_DUMP=ENTRY` a differing batch's inputs as
  the buffer and as VU memory hold them. The frame is drawn from the native output.
- `GT4_VU1_INVENTORY=DIR`: the interpreter runs everything and records each program (`vu1_inventory.h`).
- `tests/vu1_native_fuzz.cpp` (ctest `vu1_native_fuzz`, needs `GT4_VU1_IMAGE=path`): random batches through every
  entry and lighting block; the exact port must match the interpreter, the native renderers are compared and
  reported per ITOP.

## Tests

All in `ctest` (`tools\build_windows.cmd` runs them):

| Test | What | GPU |
|---|---|---|
| `ps2gfx` | VIF1 UNPACK formats, row/column/protect masking, offset mode, skipping and filling writes, FLG double buffering into the batch buffers, a command split across records, DIRECT -> PATH2; GT4's strip renderer on a hand-made batch (header packet first, window coordinates, Z, fog, ADC, colours, STQ); record decoding (GIF, PRIV, VSYNC, GS_READ -> GS_IMAGE); MSKPATH3 holding PATH3; capture write/replay; pad encoding. `GT4_VU1_INTERPRETER` builds add the interpreter's instructions and timing quirks, XGKICK, and a toy transform microprogram (MPG + UNPACK + MSCAL) kicking the expected PATH1 packet, and print the interpreter's speed. | no |
| `vu1_native_fuzz` | `GT4_VU1_INTERPRETER` builds, with `GT4_VU1_IMAGE`: random batches, the bit-exact port against the interpreter and the native renderers against the interpreter (see above). | no |
| `gs_host` | the native front end's events (FINISH, SIGNAL, LABEL with their IDs), a GS -> host read from memory and one of drawn pixels asked of the GPU (the frame list cut at it), a capture replayed through `GsHost` (the worker thread, lockstep) into one displayed frame list per VBlank. Writes the capture for the next test. | no |
| `host_replay` | `gt4_host --headless --replay ... --preset original --capture` at 640 x 448 (the native renderer at 1x), then the PPM checked by pixel (`tests/host_replay.cmake`). | yes (77 = skipped) |
| `gs_backend`, `gs_native` | `GT4_GS_REFERENCE` builds only: paraLLEl-GS by pixel (sprites, blending, textures, a CLUT, a download, events, a VU1 program), and the native GS memory model against it. | yes (77 = skipped) |

Without a suitable GPU (CI) the GPU tests are reported as skipped. Captures and images go to the build
directory (`gs_test_output/`), never into git; no game data is used by the tests.

Live link, checked by hand with the game of 2026-10-25 (which reaches VBlanks but does not draw yet):
`gt4_host --headless --frames 240 --game gt4_game.exe --game-args "--elf ..."` connects, receives 239
VBlanks and 15 privileged writes in 4.2 s, and the game exits with code 0 on the host's quit command; with
a missing ELF the game's exit code 2 is logged and the host returns 3.

## Performance

RX 7900 XTX, Release, Windows:

- **3D renderers**: replaying 300 VBlanks of the race route headless at 640 x 448 takes 8.4 s with the native
  renderers, 12.3 s with the bit-exact VU1 port they replace (2026-10-30). The interpreter (reference builds)
  runs about 30 million instruction pairs per second.
- **Native renderer**: see `docs/NATIVE_RENDERER.md` (a race frame at 2560 x 1440 in about 8 ms of GPU time).

## Next steps

1. One ordering question needs real data: records are processed whole and in arrival order, so a PATH3 transfer kicked after a VIF1
   chain that unmasks PATH3 in its middle lands after the whole chain, not at the unmask point. If GT4's
   frames show late textures, the VIF1 decoder can pause at an unmask until the next record is known.
2. Motion vectors for the temporal upscalers: the 3D renderers have each vertex's clip-space position and
   could give the previous frame's too (`docs/RENDERING.md`, scene contract).
3. SMODE1 from `SetGsCrt`.
4. The GS command layer itself (`docs/NATIVE_ONLY.md` layer 5): the game's graphics library drawing through
   a native renderer interface instead of GS packets.
