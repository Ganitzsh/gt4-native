# The native renderer (R1)

`gt4_host` draws GT4 with the native renderer only: the game's GS command stream drawn by Vulkan, with the
pipeline's anti-aliasing, upscaling and post-processing on top. `render.gs` picks the resolution: `original`
(the `original` preset) one render pixel per GS pixel, the PS2's 640 x 448; `native` (every other preset) the
nearest whole scale to the host's resolution. It can change at any time. Until 2026-10-30 the `original`
preset showed paraLLEl-GS, a GS emulator; that is now only a test reference (`GT4_GS_REFERENCE`,
docs/GS_BACKEND.md), and the numbers against it below were measured while it ran beside the native renderer.
This page is the renderer's design and its state.

## State (2026-10-27)

Menus, car select, the pre-race screens and the race (mirror, colour grading, HUD) render from the live game
at any resolution and aspect (the game's own Hor+ 16:9 mode, 2560 x 1440 in about 8 ms of GPU time with the
high preset). At 1x a race frame has 92% of its pixels within 16 of paraLLEl-GS (64% within 4); the rest is
mostly the field lines, which the native renderer does not jitter.

- **Scale per axis** from what the display shows: the race draws 224-line fields (SMODE2 FFMD), so its
  lines get the output's full height (sy = output height / 224), and the anamorphic 16:9 frame gets more
  across than down. GT4's quarter-pixel XYOFFSET jitter between fields is dropped (it shook the picture);
  `GT4_GSN_SCALE=1 GT4_GSN_JITTER=keep` gives the comparable 1x picture.
- **Textures**: GS bilinear (four taps, each wrapped by CLAMP's modes, 4-bit weights at 1x); the GS's own
  mipmaps (MIPTBP) at the GS's level of detail, (log2(1/Q) << L) + K, less the scale's log2 (New York's
  railings are mipmapped cut-outs: screen-derivative levels aliased them); above 1x, blit-made mip chains
  for other 3D textures, trilinear and anisotropic, with the alpha test reading level 0 (averaged levels make
  fences solid); sprite filtering kept inside the sprite's texels above 1x. Uploads that leave memory
  unchanged keep the decodes; the cache has a budget (animated menu palettes once filled the GPU).
- **The pipeline** keeps the game's grading: no tone mapping, automatic exposure or extra bloom on these
  frames (the game draws its own glare); sharpening, film grain and the like still apply.
- **No motion vectors yet**: the pipeline uses SMAA and FSR 1 in place of TAA and FSR 2 for these frames.
- Debugging: `GT4_GSN=stats`, `GT4_GSN_DRAWS=BP|all`, `GT4_GSN_XFER` (transfers, CLUT loads, XYOFFSET),
  `GT4_GSN_TEXDUMP=TBP`, `GT4_GSN_DUMP=VBLANK` (targets), `GT4_GSN_LIMIT=K`, `GT4_GSN_VRAM=N` (front-end memory),
  `GT4_GSN_TRIGGER=PATH` (live sessions: when that file appears, that frame's lines and targets),
  `GT4_VRAM_DUMP=FRAME:PACKET,...|end` (the memory model at a packet) and `GT4_GS_READ_DUMP=DIR` (what each GS ->
  host read returned). A screen deep in the game is best
  studied offline: `GT4_HOST_RECORD=PATH,FROM,COUNT` (FROM = `trigger:FILE`: when FILE appears) records what the host receives over those VBlanks, with
  the VU1, VIF1 and GS state at FROM first, and `gt4_host --replay PATH --frames N` then plays it at either
  resolution (`--preset original` / `high`), the same frames every time.

The menus' 2D keeps a centred 4:3 area in the game's 16:9 mode (game-side, gt4-native-game
WIDESCREEN.md). Open: motion vectors, COLCLAMP = 0 (wrapping), Z formats as frames and textures (GT4's Z24
pass), dithering, the GPU cost of the colour grading (one dispatch per batch would do). GT mode's map is
broken in the game port itself (paraLLEl-GS shows the same shards): a game-side bug.

## What GT4 asks of the GS

From the packets of whole frames (car select: 13,263 primitives in ~60 passes, `GT4_GIF_LOG`):

- **Textures in GS memory, swizzled**: CLUT textures (4-bit and 8-bit indices, 32-bit palettes) uploaded as
  32-bit blocks and read back in another format, so the renderer needs a model of GS memory with every
  pixel format's swizzle, not a texture per upload.
- **Render to texture**: an environment map rendered every frame into a 128 x 256 target, then drawn into a
  256 x 256 texture the car paint samples; the 640 x 448 frame read back as a texture for glare (downsampled
  into 320-wide targets, blurred with fixed-alpha blends) and composited.
- **Destination alpha**: the car body writes its reflectivity into the frame's alpha; the reflection pass adds
  `Cs * Ad` (blend factor = destination alpha) and is limited by the destination alpha test (DATE) so it is
  applied once.
- **Two 640-wide frames** (24-bit and 32-bit) combined at the end, and the display's two read circuits.
- Alpha test (GREATER/GEQUAL, `keep`), depth GEQUAL, fog, modulate texturing, gouraud strips, sprites.

## Design

Front end (CPU, on the GS worker thread), `platform/gsnative/` (it also reports SIGNAL / FINISH / LABEL to the
game and serves GS -> host reads, drawn pixels through the back end: docs/GS_BACKEND.md):

- `gs_memory`: 4 MiB of GS local memory; addresses of every format (PSMCT32/24/16/16S, T8, T4, T8H, T4HL,
  T4HH, Z32/24/16/16S) from their page/block/column layouts; host-to-local, local-to-local and local-to-host
  transfers; texture decoding (CLUT CSM1/CSM2 and CSA, TEXA for 16 and 24-bit alpha). Checked against
  paraLLEl-GS's memory after the same transfers.
- `gs_frontend`: the GIF parser (PACKED, REGLIST, IMAGE), both drawing contexts, the vertex queue (ADC,
  strips and fans, sprites) and the register state, turned into a frame list: draws with their state,
  uploads, copies, and the display registers at VSYNC.

Back end (Vulkan, recorded by the display pipeline as a scene source):

- **Targets**: one image per GS frame buffer (FBP, FBW, PSM) at the render scale, R16G16B16A16 float holding
  colour / 255 and alpha / 128, so that blend factors such as `Ad` and `1 - As` keep the PS2's 0..2 range;
  one depth image per Z buffer. Texture reads of memory a target owns sample the target. At most 96 at once:
  past that, the slot of the target unused for longest (not in this frame or the last) takes the new buffer
  and the back end starts a new image for it (`Target::generation`). The loading screen's course photo
  renders its tiles at a series of addresses; without the reuse, the race after it found every slot taken
  and its draws were dropped.
- **Textures**: decoded from the memory model into RGBA8 images, cached by address, format, palette and
  upload generation.
- **Pipelines** keyed by blend (dual-source for As and FIX), alpha test and its fail mode, depth test and mask,
  colour mask, fog, primitive type; DATE through the stencil buffer (the alpha bit copied into stencil before
  a DATE run, updated by the draw itself with shader stencil export); a clamp pass after blends that can
  exceed 255 (COLCLAMP).
- **Display**: PMODE / DISPFB / DISPLAY of both circuits composed into the scene's colour target.

## GT4's tricks, and how the native renderer follows them

Found frame by frame against paraLLEl-GS (`GT4_VRAM_DUMP` for its memory, `GT4_GSN_DUMP` for the targets,
`GT4_GSN_DRAWS=all` and `GT4_GSN_XFER` for the front end's view):

- **Colour grading through palettes (race).** After the 3D frame is composited into the 24-bit display
  buffer, each 64 x 32 block is read as 8-bit indices (`T8` over the 32-bit pixels: one byte, one channel)
  through a palette that holds the grading curve, three sprites per block summing R, G and B into a scratch
  target; the block is then written back by drawing an uploaded index ramp (8 x 32, `T8` at 0x38E4)
  through palettes loaded from the scratch target. So palettes can live in a render target: a CLUT load from
  a page a 32-bit target owns is read on the GPU (`clutWritten`, `gsn_reinterpret.comp`), and an index
  texture in memory with such a palette is a reinterpretation too. Page ownership follows the drawn
  rectangle, not whole rows: the ramp and the curve sit in the scratch target's second page, which is never
  drawn.
- **Sub-pixel reinterpretation.** Those tricks move whole GS pixels; at a render scale above 1 a
  reinterpreted texture has scale x scale texels per GS texel, each reading the same sub-pixel of the pixels
  it comes from, and the draws that sample it (`kSnap`) take their GS pixel's texel and their own
  sub-texel. The colour grading thus applies at full resolution, as the game meant it, with no
  GT4-specific detection.
- **Supersampled cars (showroom, dealers).** A car on a turntable is drawn four times with half-pixel
  jitter, each pass averaged into an accumulation target by copies in 32- and 64-pixel strips (xy - 0.5 with
  uv on texel edges, so the GS reads texel centres). Render pixel j holds what the GS draws at the point
  j / scale, so a target's texel coordinate is read at u - 0.5 + 0.5 / scale (`fromTargetPosition`): read at
  u itself, every copy landed on the boundary between two render pixels and strips rounded differently,
  which turned the car into a staircase, at 1x too (the field targets are drawn at 2x vertically).
- **GIF tags across transfers.** An IMAGE tag can end one DMA packet and its pixels arrive in the next (the
  ramp's upload): each path keeps its tag in progress.
- **Z only.** The mirror and the scene clear Z inside a rectangle with a sprite whose alpha test always fails
  with AFAIL = ZB_ONLY; every AFAIL mode is a second pass with the test inverted and the masks it allows.
- **One index image, several palettes.** Road textures are a 4-bit image loaded with an 8-bit CLUT and drawn
  with different CSA; a texture is cached per palette, and content that changes within a frame gets a new
  id (the back end uploads before the frame's draws).

## Order of work and acceptance

1. Memory model and transfers; every format round-trips and matches paraLLEl-GS's memory.
2. Front end: the frame list of a captured frame matches `gsframe.py`'s decoding.
3. Back end at 1x: each captured frame within a measured tolerance of paraLLEl-GS (per-pixel difference
   statistics, the worst regions listed); the passes above one by one.
4. Render scale, then widescreen (game code: the monitor matrix, docs/WIDESCREEN.md), MSAA and texture
   filtering options, motion vectors for the temporal paths.
