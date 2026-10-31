# GT4 drawing through a native renderer interface: inventory and plan

State of 2026-10-30. The run time has no interpreter, no VU0 or VU1 model and no GS emulator
(docs/NATIVE_ONLY.md). What is still GS-shaped is the game's own graphics code. The pgl library (0x494500..0x4AC920,
gt4-decomp `src/network/unit_00494578/`) and its callers build DMA chains, VIF codes and GIF packets in guest RAM.
`runtime/gs.cpp` walks those chains, and the host's front end (`platform/gsnative/gs_frontend.*`) parses GIF tags
and GS register writes back into draws.

This page covers three things:
- what produces those bytes, measured on the route through GT mode;
- a native interface that the game code would call instead of writing packets;
- an ordered migration in which every step keeps the picture identical.

Function addresses are NTSC-U v1.01. Where a function's code comes from:

| Label | Meaning |
|---|---|
| **matched** | the decomp's matched source (`src/...`) |
| **draft** | a provisional draft (`build/game/draft_natives.txt`) |
| **game_src** | this repository's native rewrite (`game_src/func_X`) |
| **runtime** | a runtime replacement |

"pgl/" stands for `src/network/unit_00494578/` in gt4-decomp.

## Measuring: GT4_GS_WRITERS

`runtime/gs_writers.cpp` is a debugging aid. It is off unless the variable is set; set, it is inert outside its
window.

**Setting it:**
- `GT4_GS_WRITERS=FROM,FRAMES`: a window opens at VBlank FROM.
- `GT4_GS_WRITERS_DIR=DIR`: where the report goes (`DIR/gs_writers_FROM.txt`).
- `GT4_GS_WRITERS_IMAGE=1`: also watch texture pixels.

**What it records.** Every byte the DMA channels deliver towards the GS, attributed to the game function that wrote
it:
- `runtime/gs.cpp` feeds the tool every DMA tag and every delivered byte, with its guest address, in channel order.
- A VIF parser and a GIF parser per path put each 32-bit word in a category: DMA tag, VIF code, a 3D batch's
  vertices, its GIF tag (qword 192), its constants (193..216) or its GS state packet (217..), DIRECT (PATH2) and
  PATH3 GIF tags, A+D writes by register group, PACKED/REGLIST vertices, IMAGE pixels.
- A shadow map of guest memory holds the last writer of every word.

**How it finds the writer.** During the window the tool write-protects every page that held packet data, plus the
pgl context page. A write to such a page faults into a vectored handler, which:
1. names the game function of the faulting instruction (`native_name_for`) and its caller;
2. opens the page and single-steps the instruction;
3. marks the words the store wrote (its operand size, and any word that changed);
4. protects the page again.

For a register value packed by the state flush `004AB318`, it also records who last wrote that register's slot in
the pgl shadow (the "setter").

**Phases.** All counted in frames (the VIF1 chain kicked by `004A11F8`): 2 frames to learn the pages, 2 to warm the
shadow, then FRAMES measured.

**The report.** Tab-separated lines:
- `site`: writer, caller, kind (direct, via-runtime, via-crt, or for words not written in the window: ref-by,
  stale, static, unwatched), category, bytes;
- `regs` and `prims`: the GS registers and primitive types each site wrote;
- `value`: distinct values of the state registers;
- `setter`: who set the packed shadow registers;
- `kick`: bytes per kicking function.

**Limits:**
- **Slowdown.** The game runs about ten times slower in the window (88,000 to 470,000 faults for 5 measured
  frames). The
  fault time is taken off the bus clock (`kernel_add_stall`), but a frame still spans 1 to 6 VBlanks, and loading
  (real time) gets ahead. So the report counts frames, not VBlanks: per-VBlank figures varied 3 to 10 times
  between runs, per-frame figures agree.
- **One window per run.** Guest RAM is the executable's shared `.guest` section. Changing the protection of one of
  its pages gives the main view a private copy, so the mirrors at 0x20000000/0x30000000 stop seeing it. After a
  window, 20 to 80 pages differed; the next GS->EE download, whose list `004AB930` reads through the mirror, then
  wrote to address 0. Measure one screen per run.
- **Unseen writes.** Writes through the mirrors are not seen (none matter here). The disc is read through a copy
  while a window is open, because ReadFile into a protected page fails.

**Commands.** The route, one window per run, from `C:\Users\julie\gt4-native-game`:

```
GT4_GS_WRITERS=FROM,5 GT4_GS_WRITERS_DIR=... GT4_PAD_SCRIPT="1500:start,1700:triangle,1800:down,2200:right,2500:cross,
  2900:cross,3300:cross,3700:cross,4100:cross,4500:right,4700:cross,5200:cross,5600:cross" gt4_host --headless --game ...
```

FROM per screen: 1600 L's Home (garage with the car), 2000 GT mode's map, 2700 the Beginner events list, 3500 the
Sunday Cup event screen with the car, 4300 the pre-race screen, 6000 the race (B-spec, Autumn Ring Mini Reverse).

## What the GS receives, per frame

KB per frame (5 frames per screen). The first number of each cell is what game code wrote in that frame. The second
is everything delivered, including data that DMA REF tags point at and nothing wrote in the window.

| Screen | DMA tags | VIF codes | batch headers | batch vertices | PATH2 GIF | PATH3 set-up | PATH3 pixels | written | total |
|---|---|---|---|---|---|---|---|---|---|
| L's Home | 17 / 17 | 15 / 39 | 70 / 77 | 0 / 687 | 50 / 50 | 6 / 6 | 0 / 1426 | 158 | 2304 |
| map | 25 / 29 | 6 / 11 | 11 / 13 | 0 / 56 | 47 / 47 | 92 / 92 | 0 / 2571 | 182 | 2820 |
| event list | 4 / 5 | 3 / 3 | 1 / 1 | 0 / 0 | 38 / 38 | 5 / 5 | 0 / 962 | 51 | 1014 |
| event screen (car) | 26 / 26 | 25 / 78 | 117 / 133 | 0 / 1905 | 54 / 55 | 6 / 6 | 0 / 1324 | 228 | 3526 |
| pre-race | 20 / 20 | 18 / 95 | 56 / 69 | 1 / 615 | 41 / 41 | 8 / 8 | 0 / 1992 | 144 | 2839 |
| race | 23 / 23 | 22 / 105 | 76 / 91 | 3 / 601 | 19 / 19 | 9 / 9 | 0 / 1809 | 152 | 2657 |

What this means:

1. **The game writes 50 to 230 KB of GS-shaped bytes a frame. The other 1 to 3.4 MB are referenced.**
   - 3D vertices are VIF packets prebuilt inside the model files. `0049D328` links them with REF tags; nothing
     writes them per frame.
   - Texture pixels are re-sent from EE memory every frame, 1 to 2.6 MB. GS memory is a slot cache (`004AA788`,
     `004AAA20`) that evicts and re-uploads sets within a frame.

   So a native interface carries little data. The vertex and texel bytes can be referenced by EE address.
2. **Render state is the largest single producer on the 3D screens.**
   - It goes through one packer, `004AB318`: 3 to 63 KB a frame, 42% of the written bytes in the race, 34% on the
     pre-race screen, 5 to 21% elsewhere. In 3D it lands in qword 217 of the batches; in 2D it is PATH2 A+D.
   - The rest is DMA/VIF glue (7 to 50 KB), 2D primitives (13 to 46 KB of PATH2 GIF), texture-transfer set-up (5 to
     92 KB; 92 on the map) and 3D constants.
3. **There were no GS->EE reads in these windows.** The luminance probe and the photo reads did not run there.
   **Glare, colour grading and the blend passes (`004A3620`) did not appear in the race window** (course flags).
   Their producers are known from the code (below) but not measured.

## Producers by kind

Bytes per frame are written bytes, as min-max over the screens where the producer appears. "Refs" are the bytes its
tags make the DMA read from elsewhere.

### A. Frame, chain and synchronisation (GS-free once the stream is native)

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 004A1A00 | sets the packet buffer (ctx+0x1F0..) | matched pgl/func_004A1A00.c | - |
| 004A1BB0 | frame head: PATH3 NEXT slot, VIF1 BASE/OFFSET/STCYCL/STMOD/STMASK; marks every GS register dirty | matched | 36 |
| 004A1C98 | frame end: state flush, END tags on both chains | matched | 24 |
| 004A11F8 | kick: VIF1 (ch 1) and PATH3 (ch 2) chains via 004ABA50 | matched | - |
| 001077F8, 00107878, 001079C8 | frame swap (double buffer D_006184C0), kick | matched (src/startup/unit_00101400/) | - |
| 004A1F60 | VIF1 FLUSH\|I NUM n (the VIF side of a PATH3 handshake) | matched | 0.2-1.4 K |
| 004A1FA8 | PATH3 NEXT tag with IRQ user bits (the GIF side) | draft | 0.1-0.9 K |
| 004A2038 | paired handshake, modes 0/1/2 (after a texture bind or upload) | matched | via the two above |
| 004AA4A0, 004AAA20, 004AAD78 | GS slot cache: release / allocate with eviction sync (FLUSH\|I NUM 2) | draft, draft, matched | < 40 |
| 0049FF38, 004A1140, 004ABA50, 004AB930, 004AB430/548/668/6D8 | DMA start/reset, VBlank and DMA handlers | runtime (runtime/gs.cpp) | - |

GS features: none for the picture. They exist to order PATH3 uploads against the VIF1 stream when GS memory slots
are reused within a frame (docs/GS_BACKEND.md "VIF1 and GIF (PATH3) in step").

### B. Texture and CLUT uploads

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 00499728 | bind a texture set: slot lookup (004AA788), allocate (004AA650), rebase TBP/CBP (00498828), upload, handshake | matched | - |
| 00499508 | per texture set: CNT + REF chain on PATH3, one per record | matched | 0.3-1.4 K; refs 0.1-2 MB pixels |
| 004A4CA8 | one record: the A+D transfer packet | matched | 0.1-1.5 K |
| 004A4CF0 | BITBLTBUF/TRXPOS/TRXREG/TRXDIR | draft | 0.4-31 K (map) |
| 004A4F48 | direct EE->GS upload chain (map tiles: 001FE5B0, 128x128 in 16x16 tiles); also 004997B0 (icons, photo, HUD), 00105A68, 00107C80 | game_src | 12 K on the map; refs 340 KB |
| 004A50C0 | the other branch of 001FE5B0 | game_src | not seen |
| 004A4910 | TEXFLUSH dirty | matched | - |
| 004A8CB8, 004A8F78, 004A9228 | grading-curve palette, the 8x32 T8 index ramp, 9470's palette | draft, matched, draft | not seen |

GS features:
- IMAGE host->local into GS memory.
- Destination formats: PSMCT32 for 6,100 of 6,570 transfers. Textures and CLUTs are uploaded as 32-bit blocks and
  read in another format. The rest are PSMT8 (0x13) and PSMT4 (0x14).
- Sizes: mostly 16x16 (4,630 transfers), then 32x16, 32x32, 8x8; 59 distinct.
- Swizzled addressing, CLUTs (CSM1, CSA), TEXFLUSH.
- Uploads into pages a render target owns (the grading ramp).

### C. Render state (ALPHA, TEST, FRAME, ZBUF, SCISSOR, XYOFFSET, TEX*, CLAMP, fog...)

The pgl context at 0x70002000 keeps a shadow of the GS registers:
- shared registers at +0x120;
- context 1/2 at +0x170..0x1D8;
- dirty bits at +0x1D8.

Setters write the shadow and set dirty bits. One flush packs the dirty registers as A+D. A few 2D functions also
write registers directly into their packet and mark the shadow dirty again (E, F).

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 004AB350 | the flush: picks dirty registers (tables D_0062A040/50/60) | matched | - |
| 004AB318 | packs them as A+D | game_src | 3-63 K (3D batches up to 60 K, 2D 1-4 K) |
| 0049E5F8 | 3D: CNT + UNPACK to qword 217 + A+D GIF tag | matched | 0.3-27 K |
| 0049FE28 | 2D: CNT + FLUSH + DIRECT + A+D GIF tag | matched | 0.2-2 K |
| 0049CC48 | bind a texture record: TEX0, TEX1 (+LOD bias), MIPTBP1/2, CLAMP, TEX2 | draft | setter of TEX1 (2,100 values/frame), CLAMP |
| 0049CEA0 | model per-packet event: texture index -> 0049CC48, material -> 0049E228 | matched | - |
| 004A1638 / 004AB040 | pglEnable / pglDisable: TME, DATE, ATE, depth test, ABE, DTHE, FGE, AA1, COLCLAMP, clipping, scissor, lights | matched | TEST setter |
| 004A26F0, 004A28B8, 004A2750, 004A27C8, 004A2858 | depth func, alpha func (raw / float), AFAIL, DATM | matched (2750 draft) | - |
| 004A2908, 004A2808 | blend func (ALPHA A/B/C/D + FIX) | matched | ALPHA setter |
| 004A2980, 004A29A8, 004A3020 | colour mask (FBMSK), depth mask (ZMSK), depth range (VU1 Z scale) | matched | - |
| 004A2478 -> 004A2558, 004A2BC8, 004A2D20 -> 004A2E98 | draw buffer (FRAME), XYOFFSET with field jitter, viewport, SCISSOR | matched | FRAME, XYOFFSET, SCISSOR setters |
| 004A2B10, 004A2B58, 004A2C58, 004A2A10 | sub-pixel jitter, supersample pattern, field mode | draft, draft, matched, matched | - |
| 004A4100, 004A4418, 004A4550, 004A4810 | texture image (TBP/TBW/PSM, MIPTBP), size, glTexParameter, clamp region | matched | - |
| 004A4050, 004A3F98, 004A5688, 004A5FE0, 004A25F0, 004A0228 | fog colour, SCANMSK/ZBUF, MIPTBP/TEXA defaults | matched (3F98, 5688 draft) | - |
| 004AA168, 004AA1D8, 004A6F10 | colour (RGBAQ for 2D), sprite Z / fog | game_src, draft, matched | - |

Values seen (all screens):
- **ALPHA, 10 equations:**
  - 0x44: (Cs-Cd)As+Cd
  - 0x48: CsAs+Cd
  - 0x58: CsAd+Cd (reflection by destination alpha)
  - 0x64: lerp by FIX
  - 0x68: CsFIX+Cd
  - 0x62: Cd-CsFIX
  - 0xA9: CdFIX
  - 0x21 and two FIX variants
- **TEST:** 18 distinct in 3D, 9 in 2D: alpha test GREATER/GEQUAL with AREF 0x20/0x7F, AFAIL KEEP/FB_ONLY/ZB_ONLY,
  DATE/DATM, depth GEQUAL/ALWAYS.
- **TEX0:** PSMCT32 with TCC; PSMT8/PSMT4 with CLUTs; targets as textures.
- **FRAME:** 21 distinct in 2D and 13 in 3D: buffers at FBP 0x00, 0x8C, 0xD2, 0x118, 0x15E, 0x1EC; FBW 2-10;
  PSMCT32 and PSMCT24; FBMSK for alpha-only and RGB-only writes.
- **ZBUF:** Z24/Z16 with ZMSK on and off.
- **Others:** FBA, PABE, COLCLAMP, DTHE, TEXA, PRMODECONT constant.

Setters with no name in the report are values that were not rewritten in the window: the library marks registers
dirty again every frame (004A1BB0) and after every direct write.

### D. 2D primitives (sprites, text, rectangles, HUD)

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 00491FC0 | one font glyph: binds the font page (00499728, 0049CC48), flushes state, then 00499948 or 004A6668 | matched (src/engine/unit_0046A050/) | - |
| 00499948 | textured quad through the current matrix (VU0 0x700): rotated or scaled glyph; REGLIST PRIM TRISTRIP TME FST | game_src | 3-34 K (the largest 2D producer) |
| 004A6538 <- 004A6668 (float, pixels), 004A6498 (integer, 1:1) | one textured SPRITE, REGLIST (glyphs, mirror paste, copies, probe, photo, UI) | draft <- matched | 7 K in the race |
| 004A6788 | textured sprite in 32-pixel strips (buffer copies: 00105AD8, 00105C48, glare, reflection blur, supersample final, menus) | draft | 0.7-3.7 K |
| 004A6290, 004A6140, 004A3498 | flat SPRITE fill in 64-pixel strips / one / full-screen white with FIX blend | draft, matched, draft | 0.1-0.5 K |
| 0049A150 -> 0049A200, 0049A360, 0049A4F8, 0049A6A0 | menu "draw arrays" through VU0 0x678: untextured, textured (ST, Q), per-vertex colour | game_src | 0.3-3.3 K |
| 003E7F08 -> 004A5B90 | particles (smoke, dust): sorted sprites as direct GIF | game_src, matched | < 0.1 K here |
| 0049FD50 -> 0049FAC8 (+ 0049F938, 0049F9C8, 0049FA20, 004A72C0) | VU1 immediate draw arrays (64 vertices, strip/fan): race HUD (003AF6E0, 003AFAE0), sparks, vignette 003BBBC0, photo pause | game_src / matched | 0.1-1.5 K + 1-1.4 K of EE vertex arrays |
| 0049C2A8, 0049C788, 0049C140 (+ 004854F8, 0047EEE8) | strobe vector shapes: VU1 batches of 2D triangles/strips, texture matrix | game_src (+ matched) | < 0.3 K |
| 00497838 | pre-built VIF shape with per-draw translation/scale/colour (icon-like meshes) | draft | < 0.1 K |

GS features:
- SPRITE and TRISTRIP with TME, FST (UV) or ST/Q, IIP, ABE.
- Sub-pixel 12.4 coordinates with the field jitter at ctx+0x288; Y scaled by 8 in field mode.
- Context 1. RGBAQ with Q = 1.
- 32/64-pixel strips, made for the GS texture cache, not for the picture.

### E. Clears and render-target switches

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 004A3230 | pglClear(bits): 1 colour, 2 depth, 4 the second buffer (61 call sites: env map, reflection, mirror, showroom...) | matched | - |
| 004A30B0 | the clear: SPRITE strips of 64 pixels with TEST 0x32001 (Z only), 0x30000 or 0 and ZBUF at the clear depth | draft | 0.3-3 K |
| 004A3308 | the second buffer (ctx+0x24C) as target: FRAME, SCISSOR, XYOFFSET | draft | < 0.1 K |
| 004A3910 | XYOFFSET of the main buffer again | draft | 0.1-0.7 K |
| 004A3078 / 004A3088 | clear colour / depth | matched / not seen | - |

GS features: the Z-only clear (an alpha test that always fails, with AFAIL = ZB_ONLY), scissored rectangles, field
offsets.

### F. Render-to-texture effects

From the code. Only the copies and fills (004A6788, 004A6290, 004A3620) were measured.

| Function | What | Code |
|---|---|---|
| 00105728 | binds a GSBuffer as the texture (TEX0 TBP/TBW/PSM, TW/TH, REGION_CLAMP; 67 callers) | matched |
| 00105AD8, 00105C48 | GSBuffer copy 1:1; display copy with field offset and deflicker pass (ALPHA 0x64) | matched |
| 004A3998 -> 004A3620 (+004A3498, 004A3308, 004A3910) | buffer blend passes, 10 types: brighten/darken, add, lerp (crossfade, motion blur, the **supersampled car average 004A3998(6, 1/(i+1))** of 001D6378), field merge | matched -> draft |
| 003BADF8 (<- 0033F1C8) | **glare**: reduce (004A6788), bright pass (004A6290, ALPHA 0x62), N x N jittered additive taps (0x68) into 320-wide targets, add back | matched |
| 004A7D90 -> 004A81A8 (VU1 grid 0x3D50) | **colour grading**: the 24-bit frame read as PSMT8 through the curve CLUT, three passes summed into a scratch target, the index ramp drawn back with CLUTs loaded from the target (CBP inside a render target), context 2, PRIM CTXT | matched -> game_src |
| 004A9378 -> 004A9470 (VU1 grid 0x3EA0) | the 640x224 frame read as PSMT8, three planes into the alpha of three buffers (purpose unknown) | draft -> game_src |
| 003411D0 | road reflection: course and cars into a GSBuffer, ping-pong half-pixel blur, composite | game_src |
| 0044E660, 0044EB10 | car environment map (sphere mapper): the course drawn three times into two offscreen buffers; the paint samples them as the external texture set 0x1FF | matched |
| 00342470, 00342630 | paste a GSBuffer with alpha test NOTEQUAL 0 and depth; full-buffer fade/darken | matched |
| 001D6378 | showroom supersampled car: n jittered passes (004A2C58), averaged, final 004A6788 copy | matched |
| 003BBBC0 | vignette ring through 0049FD50 | matched |

GS features — the tricks a native interface must keep:
- a texture is a GS address that a draw may have written;
- reads in another pixel format (T8 over 32-bit pixels);
- CLUTs loaded from target pages;
- destination alpha (blend C = Ad, DATE);
- FBMSK channel writes;
- FIX blends above 1;
- context 2;
- half-pixel and jittered offsets.

### G. 3D batches (the VU1 path, native renderers on the host)

| Function | What | Code | Bytes/frame |
|---|---|---|---|
| 0049D328 (<- 0049D308, ModelSet2::_render 00455708) | per shape: REF tags to the model file's VIF packets, with ITOP/MSCAL in the tag words | game_src (matched source) | 0.8-22 K; refs 56 KB-1.9 MB |
| 0049E548 | setup constants: transform x shape scale, viewport, clamp, guard band (UNPACK 8 qw at 193) | game_src | 0.3-8 K |
| 0049D080 / 0049D1A0 | lighting kind / REF of the lighting block, key-frame weight (qword 242) | matched | 0.2-1.3 K |
| 0049EA80 -> 0049E698, 0049E918, 0049EB90, 0049EE00; 0049F170, 0049F0F0, 0049F448, 0049F4C8, 0049F1F8, 0049CFE8 | light matrices and colours in object space (qwords 201..216) | game_src / matched | 0.1-7 K |
| 0049E480, 0049E228 | composite matrix; material colours | game_src | - |
| 0049DA60 -> 0049D580, 0049D728, 0049DB08, 0049DCB0 | skinned/dynamic vertices computed per frame (driver, pit crew) | game_src | not seen |
| 0049BBB0, 0049C0D8 -> 0049BE10, 0049BBF0 | immediate mode: light glows (003B8EA0), billboard sets | matched; 0049BE10 and 0049BBF0 game_src (batch records since 2026-10-31: the frame walk had dropped them, night courses lost their light glows) | not seen |
| 0049E5F8 | the batch's GS state (C) | matched | above |
| 004A81A8, 004A9470 | the screen-grid batches (F) | game_src | not seen |

GS features: the batch's GIF tag (PRIM with TME/FGE/ABE/IIP), the state packet (C), ADC, fog. The rest is the
renderers' input, already native on the host (docs/VU1_PROGRAMS.md).

### H. Image reads (GS -> EE)

| Function | What | Code |
|---|---|---|
| 004A5178 (+ 004ABB88) | BITBLTBUF/TRXPOS/TRXREG/FINISH/TRXDIR = 1 by DIRECTHL, handshakes, {qwc, EE address} list | matched |
| 001059F8 | GSBuffer rectangle: luminance probe 00330AE8 (race), photo tiles 00333A00/00333E50, CarIconMaker, 00104B68 | matched |
| 003C7AE0, 003308E8, 00497EF8 | Z32 8x4 depth samples (photo focus), a 64-wide colour block | matched |
| 004AB930 | the download itself | runtime |

None ran in the measured windows. The car thumbnails at a race's loading are known users (docs/GS_BACKEND.md).

## Proposed native interface

The interface sits at pgl's level: the game keeps calling pgl, and pgl's packet writers become calls of
`gt4::gfx`. Callers of pgl do not change: the many functions that draw through `004A6668`, `004A3230`, `0049D308`
and the like stay as they are. Only the roughly 100 functions listed above, nearly all in pgl, change.

The interface keeps the PS2's model where GT4's tricks depend on it:
- GS memory addresses identify textures, palettes and targets;
- pixel formats are real formats that can be reinterpreted;
- the blend equation is the GS one.

It drops the GS's command encoding:
- no GIF tags, A+D, registers, DMA tags, VIF codes or handshakes;
- no strips made for the texture cache.

```cpp
namespace gt4::gfx {

// --- identities (GS memory is still the namespace: textures, CLUTs and targets alias by address)
enum class Format : uint8_t { C32, C24, C16, C16S, T8, T4, T8H, T4HL, T4HH, Z32, Z24, Z16, Z16S };
struct Surface { uint16_t base; uint8_t width64; Format format; };          // FBP/TBP/CBP in blocks, BW, PSM
struct Rect { int16_t x0, y0, x1, y1; };                                     // GS pixels, end exclusive

// --- data
void upload(Surface dst, Rect r, const void *pixels);                       // EE -> GS: textures and CLUTs (B)
uint32_t readback(Surface src, Rect r, void *dst);                           // GS -> EE (H); fence id
void fence_wait(uint32_t id);

// --- targets and render state (C, E), one `State` per drawing context (1 or 2)
struct Target { Surface color; uint32_t writeMask; Surface depth; bool depthWrite; Rect scissor;
                int32_t originX16, originY16; };                            // XYOFFSET in 1/16 pixel
enum class Compare : uint8_t { Never, Always, Less, LessEqual, Equal, GreaterEqual, Greater, NotEqual };
enum class AlphaFail : uint8_t { Keep, ColorOnly, DepthOnly, RgbOnly };
struct AlphaTest { Compare op; uint8_t ref; AlphaFail fail; };
struct DestAlphaTest { bool enable; bool passWhenSet; };
struct DepthTest { Compare op; };                                            // Always when off (ZTE stays 1)
enum class BlendInput : uint8_t { Source, Dest, Zero };                     // A, B, D
enum class BlendFactor : uint8_t { SourceAlpha, DestAlpha, Fixed };         // C
struct Blend { bool enable; BlendInput a, b, d; BlendFactor c; uint8_t fixed; };  // (A - B) * C + D, alpha 0x80 = 1
enum class Wrap : uint8_t { Repeat, Clamp, RegionClamp, RegionRepeat };
struct Sampler { Wrap u, v; uint16_t umin, umax, vmin, vmax; bool linearMag, linearMin; uint8_t mipFilter,
                 levels; int16_t lodK; uint8_t lodL; Surface mips[6]; };
enum class TexFunc : uint8_t { Modulate, Decal, Highlight, Highlight2 };
struct Texture { Surface image; uint8_t log2w, log2h; bool hasAlpha; TexFunc func;
                 Surface clut; uint8_t clutOffset; bool clutCsm2; uint8_t texa0, texa1; bool texaAem; };
struct State { Target target; AlphaTest alpha; DestAlphaTest dest; DepthTest depth; Blend blend;
               Texture texture; Sampler sampler; bool fog; uint32_t fogColor; bool dither; bool colorClamp;
               bool forceAlpha /*FBA*/; bool perPixelBlend /*PABE*/; };
void set_state(int context, const State &s);                                 // only what changed travels

// --- drawing (D, E, F)
enum class Prim : uint8_t { Points, Lines, LineStrip, Triangles, TriStrip, TriFan, Sprites };
struct Vertex { int32_t x16, y16; uint32_t z; float s, t, q; uint16_t u16, v16; uint32_t rgba; uint8_t fog; bool kick; };
struct DrawFlags { bool gouraud, textured, fog, blend, antialias, texelCoords; uint8_t context; };
void draw(Prim p, DrawFlags f, const Vertex *v, uint32_t n);
void clear(int context, Rect r, uint32_t rgba, uint32_t z, bool color, bool depth);
void copy(int context, Rect dst, Surface src, Rect srcRect);                 // a textured rectangle with the current
                                                                            // state: replaces the strip loops
// --- 3D batches (G): the native renderers' input, no UNPACK/ITOP/MSCAL
struct BatchConstants { float transform[4][4]; float viewportScale[4], viewportOffset[4], clamp[4], guard[4]; };
struct Lighting { uint16_t body; uint8_t decompress; float lights[16][4]; };
struct Shape { const void *packets; uint32_t count; /* {EE address, qwc, event} items */ };
void draw_batch(uint16_t renderer /*0x1EC8...*/, const BatchConstants &, const Lighting *, const void *packet,
                uint32_t qwc, uint32_t primTag);
void draw_grid(uint16_t program /*0x3D50, 0x3EA0*/, const void *block);      // the screen grids, as today

// --- frame
void begin_frame();     // replaces the chain head and the swap's buffer set-up
void end_frame();       // replaces the END tags and the kick; the host draws it at the next VBlank, as now
}  // namespace gt4::gfx
```

### Transport

A call appends a typed record to the current frame's command buffer:
- During the transition, that buffer is the pgl packet buffer itself, written at ctx+0x1F8 behind a VIF command
  value the hardware does not define (0x08..0x0F, which GT4 never emits). The VIF code says how many qwords
  follow. The records then stay ordered with whatever is still a packet: DIRECT, batches, handshakes and PATH3
  releases.
- `runtime/gs.cpp` forwards each record in stream order as a new link record `GT4_REC_GFX` (include/gt4/link).
- The host decodes it into the same front-end operations that GIF parsing produces today.

Uploads and reads keep pointers into EE memory (the DMA's REF semantics). The host copies the bytes when it reaches
the record, as now. Later the host can cache uploads by (EE address, content hash), which removes the 1 to 2.6 MB of
re-sent texels per frame.

### Host side

`GsFrontend` already turns GIF into `DrawState` + vertices + targets + uploads (`FrameList`). Step 1 below splits it:
- `gsn::Commands`: the native interface above, which builds the FrameList;
- `GifDecoder`: GIF/registers -> Commands.

Both inputs then meet in one place, and the GS register model shrinks to what the decoder needs.

## Migration plan

Every step must keep the picture identical. Two checks, taken from the VU1 work:

1. **Diff mode (live, per draw).** `GT4_GFX_DIFF=1`:
   - the game emits both the old packet and the new record;
   - the host builds the FrameList from each and compares them draw by draw: state, target, texture source and
     version, vertices, and upload order relative to draws;
   - it reports the first differences per producing record type.

   This is the equivalent of `GT4_VU1_DIFF`, and the gate for each step.
2. **Pixel replays.** A capture recorded in diff mode (`GT4_HOST_RECORD=PATH,FROM,300`) holds both
   representations. `gt4_host --headless --replay PATH --frames 300 --size 640x448 --preset original --capture ...`
   with `--gfx-source=packets` and then `=api` must give identical PPMs, every tenth frame. Run it on captures of:
   - the map;
   - L's Home;
   - the event screen with the car;
   - the pre-race screen;
   - the race start and racing;
   - a course with glare and colour grading;
   - the showroom (supersampled car);
   - photo mode (reads).

   The existing `host_replay` test keeps checking the old path.

| Step | What changes | Functions | Acceptance |
|---|---|---|---|
| 0 | done: GT4_GS_WRITERS, this inventory | runtime only | - |
| 1 | Host: `GsFrontend` split into `gsn::Commands` (the interface) and `GifDecoder` (GIF -> Commands). Pure refactor. | 0 game; host ~1,000 lines | existing captures replay byte-identical (`host_replay`, the 300-VBlank race and event captures) |
| 2 | Transport: `gt4::gfx` encoder in the runtime (records into the pgl packet buffer behind the reserved VIF code), `GT4_REC_GFX`, host decoder, `GT4_GFX_DIFF` and `--gfx-source` | 0 game; runtime + host | a no-op record type round-trips; replays identical |
| 3 | Render state (C): 004AB350/004AB318 emit `set_state` records (shadow -> native enums: ALPHA -> Blend, TEST -> AlphaTest/DestAlphaTest/DepthTest, FRAME/ZBUF/SCISSOR/XYOFFSET -> Target, TEX0/TEX1/TEX2/CLAMP/MIPTBP/TEXA -> Texture/Sampler); 0049E5F8 and 0049FE28 stop emitting the A+D packet (the batch's qword 217 becomes a record placed before its MSCAL). The ~35 setters stay as they are (they write the shadow). | 4 (004AB350, 004AB318, 0049E5F8, 0049FE28) | diff mode: identical DrawState for every draw; replays identical. Removes 3-63 KB/frame of A+D |
| 4 | Texture and CLUT uploads (B) and the handshakes (A): 00499508, 004A4CA8, 004A4CF0, 004A4F48, 004A50C0, 004997B0, 004A8F78, 004A8CB8, 004A9228 emit `upload` at the point the VIF1 stream would release the PATH3 transfer (the bind, where 004A2038 already writes its FLUSH\|I). 004A2038, 004A1F60, 004A1FA8, 004AA4A0, 004AAA20, 004AAD78 become no-ops. The PATH3 chain goes empty. | ~15 | diff mode: upload content and order relative to draws (page generations at each draw); replays identical, the map included (its uploads broke once: 004A4F48) |
| 5 | Clears and targets (E), 2D primitives (D): 004A3230/004A30B0/004A3308/004A3910 -> `clear` + `set_state`; 00499948, 004A6538, 004A6668, 004A6498, 004A6788, 004A6290, 004A6140, 004A3498, 0049A200/360/4F8/6A0, 004A5B90 (particles), the strip loops kept as strips first | ~16 | diff mode identical; replays identical. Removes 13-46 KB/frame of PATH2 GIF |
| 6 | Strips to one primitive: 004A6788, 004A6290, 004A3620, 004A3498, 004A30B0 draw one rectangle instead of 32/64-pixel strips (`copy`, `clear`). Not identical by construction: at a render scale > 1 the strip edges round differently (docs/NATIVE_RENDERER.md, supersampled cars) | 5 | replays at 1x identical; at 2x/4x compared to the strip version: the differences must be only along strip edges, then none |
| 7 | Render-to-texture effects (F) through the interface: 004A3620/004A3998, glare 003BADF8 (uses D/E pieces), the reflection blur, 00105AD8/00105C48. The colour grading and 9470 grids (004A81A8, 004A9470) keep their GS formulation (T8 reads of a target through target-loaded CLUTs, context 2) expressed as `set_state` + `draw_grid` | ~8 | diff mode on a course with glare and grading; replays identical; showroom and photo captures |
| 8 | 3D batches (G): 0049D328 emits `draw_batch` per item (the model packets referenced, the constants as structs). 0049E548, 0049D1A0 and the lighting builders (0049EA80 family, 0049F0F0/0F170/0F448/0F4C8/0F1F8, 0049CFE8) fill structs instead of UNPACK packets. Also the immediate paths 0049FAC8/0049F938/0049F9C8/0049FA20/004A72C0, 0049C2A8/0049C788/0049C140, 00497838, 0049BBB0/0049C0D8/0049BBF0, and the dynamic vertices 0049DA60. The host's renderers take the struct directly; VIF1 UNPACK decoding is left for nothing. | ~30 | diff mode compares each batch's renderer input (positions, ST, colours, constants, entry, lighting body) and output vertices; replays identical (race start, event screen) |
| 9 | Image reads (H): 004A5178/004ABB88 -> `readback` + fence; 001059F8, 003C7AE0, 003308E8, 00497EF8 wait on the fence instead of the FINISH/IRQ handshake; 004AB930 goes | ~6 | car thumbnails at a race's loading, luminance probe, photo mode: the read pixels identical (`GT4_GS_READ_DUMP`) |
| 10 | The stream is native: 004A1A00, 004A1BB0, 004A1C98, 004A11F8 and the swap call `begin_frame`/`end_frame`. `runtime/gs.cpp`'s chain walker, the VIF/GIF interrupt handshakes and the DMA handlers leave the run time; the host's GIF decoder and VIF1 decoder move behind a reference option, like `GT4_GS_REFERENCE`. | ~8 + runtime | the whole route and a race in diff mode against a reference build; replays identical |
| 11 | Optional, not pixel-preserving by design: textures by EE identity (no slot cache, no re-upload), targets as named images, the GS memory model kept only for reinterpretations | - | separate acceptance |

Sizes:
- Steps 3 to 10 change about 90 game functions. Most are small pgl functions: 32 to 900 bytes of MIPS, 2,664 the
  largest (004A9470).
- 22 of them run natively from provisional drafts today: 004A4CF0, 004A1FA8, 004AA4A0, 004AAA20, 004A6538,
  004A6788, 004A6290, 004A3498, 004A3620, 004A30B0, 004A3308, 004A3910, 0049CC48, 004A2750, 004A5688, 004A8CB8,
  004A9228, 004A9378, 00497838, 004A2B10, 004A2B58, 004AA1D8. For these, the rewrite replaces a draft anyway.
- Steps 1 and 2 are host and runtime work with no game change.
- Steps 3 and 4 alone remove the GS register encoding and PATH3 from the interface for everything but 2D packets.

Order and risks:
- **Order.** State (3) first: it is the largest share and has a single choke point. Then textures (4), because
  their handshakes are what ties PATH3 to VIF1. Then 2D (5) and the effects (6, 7), which depend on both. Then 3D
  batches (8), which already have native renderers. Then reads (9) and the stream (10).
- **Latency.** The frame built at N is drawn at N+1. Records live in the double-buffered packet buffer
  (D_006184C0), so no new buffering is needed until step 10.
- **Effects not yet measured.** Glare, colour grading, 004A3998 and the reads did not appear in the windows. Before
  step 7, measure them with GT4_GS_WRITERS on a course whose flags enable them, and in the showroom.
- **Drafts.** A behavioural difference in one of the 22 drafts would look like an interface bug (`004AB318` and
  `004A4F48` were both draft bugs). Diff mode compares against the packets the same draft wrote, so it cannot catch
  those. Matching or eeref-checking the draft first (docs/DIFFERENTIAL_TESTING.md) is the cure.
