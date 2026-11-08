# What is still PS2-shaped, and what can go

Status: design note for review, 2026-11-08. Owner's direction: the codebase ends up with no PS2 concept beyond
what the recompiled game logic itself needs. This note lists every PS2-shaped mechanism still in `runtime/` and
`platform/`. Each one is **kept** (the game logic depends on it), **replaced** (by what, in which lane or
ticket) or **can be lifted** (a PS2 limit the game doesn't need). Tickets are in PLAN.md (P1-P6, and the lanes' own
tickets named here).

Already gone (docs/NATIVE_ONLY.md, docs/NATIVE_V2.md): VU0 and VU1 microprograms and their models, VU0 macro, MMI
and system instructions in game code, DMA, VIF and GIF packets from the game, the interrupt and DMA controllers,
hardware registers (their blocks are reserved without memory), the 59.94 Hz clock, interlaced fields as the default,
and interrupts delivered at arbitrary instructions in the default mode.

## 1. What the modern renderer path removes

With `render.scene = hybrid` or `modern`, a bound view's tagged model draws are no longer drawn by the GS path. The
modern view draws them from the mesh cache and writes GS colour and Z only where the GS path still needs them (B7,
B8, CONTRACTS a.5 rule 7). For those draws this removes:
- GS-state drawing and the VU1 renderers' output;
- the GS Z emulation;
- per-frame vertex decoding (the mesh cache decodes once).

It does **not** remove the following yet. Each has the ticket that replaces it later, so this is a plan, not a
permanent residue:
- **the GS path itself.** `original` keeps it as the PS2 picture. In `hybrid` and `modern` it still draws:
  - untagged draws, and views that aren't substituted (split, cut, fallbacks): fewer as A6, B9 and the
    `viewFallbacks` reasons are worked down;
  - env-map faces, the mirror and the reflection views: B9;
  - the sky: B14 (sky, aerial perspective, froxel fog);
  - GT4's post effects: B15, modern mode (GT4's grade and glare skipped, the pipeline's bloom, exposure and tone map),
    with N3 and N4 for the flare and lamp draws;
  - the HUD: E6 (an output-resolution layer), E7 (SDF text for GT4's font pages), E9 (HUD scale, safe area);
  - GT4's own menus keep drawing through the GS path: E6 and E7 change how their 2D and text look (output
    resolution, SDF text), and P3b makes their draw state native, but nothing replaces them. Lane M adds new
    settings pages that are native pages in GT4's menu flow, not an overlay. Their logic, navigation, input focus,
    sounds (GT4's own menu sounds) and frame pacing run in the game process, as native C++ hooked into matched
    code (the project flow, the quick menu's option path, the race pause). They are entered from GT4's own menus:
    its Options items, Start on the GT mode map, the race pause. GT4's scripted menus on the disc are not edited.
    Only the pixels are host-owned (generated assets and SDF text), drawn from records the game sends (lane M's
    `MENU_DESIGN.md`, tickets M1-Mn, once the coordinator approves them);
- **reads of graphics memory** that the game logic uses (luminance probe, photo tiles, car icons): R9 decides each,
  served from the modern targets where they exist;
- **GS-shaped records**: the `gfx` records still carry GS register images (TEX0, ALPHA, TEST, ZBUF...) and name
  surfaces by GS address and format: P3b (and P3a for the frame list's DMA-tag links);
- **the GS memory model on the host**, which the GS path needs for targets, reads and reinterpretations (textures
  are already native, NATIVE_V2 step 1): P2a-P2c.

## 2. Mechanisms left, and their verdicts

| Mechanism | Where | Verdict | By what / ticket |
|---|---|---|---|
| Guest RAM at PS2 addresses (identity-mapped 32-bit section) | `runtime/guest.{h,s,cpp}`, `tools/game_link.py` | **kept** | The game's code and data hold 32-bit absolute addresses (pointers in its structures, tables in the loaded ELF, trampolines at original function addresses). Moving them would mean a 64-bit port of the game logic, which is out of scope. |
| PS2 pointers as host pointers; the 32-bit game process; gcc 2.9's ABI | `runtime/`, the game build | **kept** | The same reason. The 64-bit host stays a separate process behind the link. |
| RAM size 32 MB, heap end 0x01E00000 | `kRamSize`, `gt4_sys_GetMemorySize` (32 MB), `gt4_sys_EndOfHeap` (0x01E00000, `kernel.cpp:1179-1189`) | **can be lifted**, if something uses it | Spike R17 (P5): what sizes the game's heaps (newlib's heap from `EndOfHeap`, the game's own pools such as 008744D0), and what would use more memory. The limits: below 0x20000000 while the uncached mirrors exist; up to the scratchpad at 0x70000000 without them; and the 32-bit process's 4 GB. |
| Uncached mirrors at 0x20000000 and 0x30000000 | `guest.cpp:99`, `frame.cpp:45` | **can be lifted** if no game code uses them now | R17 counts their remaining users (`GT4_HW_AUDIT`-style audit). If none, the mirrors go, and with them the 512 MB ceiling. |
| Scratchpad at 0x70000000 (16 KB) | `guest.h` | **kept** | Plain memory at the address game code uses (matrix sets, `func_0049D328`). It costs nothing. |
| Hardware register blocks (reserved, no memory) | `guest.h` `kHwBlocks`, `runtime/hw_audit.cpp` | **lifted already** | Only the audit tool gives them memory. Its removal waits for the last draft (layer 4). |
| The original ELF loaded into guest RAM | `runtime/image.cpp` | **kept** | The game's data (tables, also some in `.text`), read from the user's disc. Code bytes stay as data. |
| Trampolines at original function addresses; missing functions | `image.cpp`, `missing.cpp` | **kept** until layer 4 ends | Function pointers in the game's data are PS2 addresses. The trampolines stay as long as such pointers exist (always). The "missing" stops end with the drafts. |
| The 60 Hz game frame (the former VBlank) | `kernel.cpp` frame clock, `runtime/gs.cpp` | **kept** as the game's time base | The game logic steps once a frame (physics, timers, the six frame callbacks). Presentation is already decoupled (interpolated to any refresh, NATIVE_V2 step 4). Names: `GT4_REC_VSYNC` and the "VBlank" counters stay as names for the captures' sake. |
| Preemptive delivery fallback (a callback waiting more than 50 ms freezes the thread, as an interrupt did) | `kernel.cpp:258-273` | **replaced** | P4: find the threads that make no syscall for 50 ms (17 of 8,911 deliveries in a session to a race), give them a safe point, then delete the freeze path and `GT4_FRAME_DELIVERY=preemptive`. |
| The EE kernel API (threads, priorities, semaphores, alarms in H-blank units, DIntr/EIntr, syscalls) | `kernel.cpp`, `sdk.cpp`, `eeops.cpp` | **kept** at its API | The game logic calls it and depends on its scheduling (one thread at a time, by priority). The implementation is already host threads and events. |
| COP0 reads (Count, the performance counters) | `eeops.cpp` | **kept** | Shims for game code. They cost nothing. |
| PS2 float semantics (24-bit x87 rounding; ps2float) | `runtime/main.cpp`, `runtime/ps2float/` | **kept** | The game's numerics (physics, replays, ghosts in imported saves) follow the PS2's single precision. |
| SIF RPC and the IOP services (the game's RPC servers answered natively) | `runtime/iop.cpp` | **kept** at the RPC API for now | The game's client code builds the PS2 protocol buffers. Replacing the client libraries at their call sites (CD, pad, sound) is possible but gains nothing until the sound moves (next row). |
| SPU2 voice model (ADPCM, ADSR, pitch, ENVX/ENDX replies, 2 MB SPU RAM) | `runtime/audio.cpp`, D5 moves it to the host | **kept**, then **can be replaced** at the sound driver's API | The game's sound driver (game logic) writes a register image each frame and frees voices by ENVX/ENDX. Spike R18 (P6), after D5: whether a native voice API under the driver's own calls (play, pitch, volume, release), with the same timing, can replace the register image. |
| CD/DVD (`sceCd`, GT4.VOL) | `iop.cpp`, the ISO reader | **kept** | The game's data comes from the user's disc image. D10 measures loading and maps the reads. |
| Memory card (libmc on folders; PS2 save files) | `runtime/memcard.cpp`, `save_folder.cpp` | **kept** | The game's save code and its data format. D3 hardened it. |
| Pad (libpad2's 18-byte frames, pressure) | `runtime/iop.cpp`, `platform/gs/ps2_pad.h`, `platform/input/` | **kept** at the pad API | GT4's input layer reads PS2 pad bytes. Devices map onto it (D12-D16). |
| IPU and the movie format | `runtime/native/movie.cpp` | **replaced** already | FFmpeg decodes PSS. The PSS files are the disc's data. |
| GS-shaped `gfx` records (GS register images, surfaces named by GS address and format; the frame list's DMA tag layout as block links) | `include/gt4/gfx.h`, `runtime/native/pgl.cpp`, `runtime/frame.cpp` | **replaced** | P3a: block links as offsets (any time); P3b, after P2a: draw state in renderer terms (blend, depth, texture and target handles). The GS encoding moves into the GS back end, which `original` still uses. pgl's API (the game logic's calls) is unchanged. |
| The host's GS memory model (targets by address, reinterpretation, swizzling) | `platform/gsnative/gs_memory.*`, `gs_frontend.*` | **replaced** | P2a: targets keyed by surface; P2b: native palettes; P2c: reinterpretations as explicit copies. `original` runs the same front end, so its path changes too, identical at every step. A GS view of memory stays for the game's readers until R9 decides each. |
| Legacy GIF / VIF1 / VIF0 records and their stream path | `protocol.h` (`GT4_REC_GIF`, `VIF1`, `VIF0`, and kind 4, the old GS_PRIV), `GraphicsStream::record`'s cases, `platform/gs/vif1.cpp`'s stream and DIRECT path | **replaced** (dead in the live game) | P1: delete the record kinds (their numbers kept reserved, so an old capture is refused), their stream cases and `vif1.cpp`'s stream path. The GS back end's packet input stays: the 3D renderers pass the game's packets on as PATH1, and state restore replays the saved GS registers as one A+D packet. `vif1.cpp`'s model-packet decoder stays too: the model files' vertex data is the game's data. |
| VIF UNPACK layout of model packets | `vif1.cpp` `batchModel`, the mesh cache (B5) | **kept** as a data format | The game's model files are in this format. The mesh cache decodes it once. |
| The display record in the graphics library's encoding (PMODE, SMODE2, DISPFB, DISPLAY) | `runtime/gs.cpp`, the host | **kept** | It is what the game's library computes. The host converts it once a frame. |
| Interlaced fields (`display.race_frames = fields`) | game option, `runtime/gs.cpp` | **kept** as an option | `progressive` is the default; fields stay for the PS2 picture. |

## 3. What stays, in one sentence

What stays is the game's own logic and data: its 32-bit address space at PS2 addresses, the original executable's
data, the 60 Hz frame and the EE kernel API, PS2 float rounding, and the SDK APIs it calls (CD, memory card, pad, SIF,
and the sound driver's SPU2 model until R18 says otherwise), plus the GS path, which `original` keeps as the PS2
picture.
