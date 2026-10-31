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
- **Temporal anti-aliasing in the scene stage** (2026-10-31; "The scene stage" below): motion vectors from
  GT4's 3D renderers, TAA on the 3D scene before GT4's post effects and HUD. The pipeline adds no AA of its own
  then, and its FSR 2 is still FSR 1 for these frames (the frame it gets is the game's, HUD included).
- Debugging: `GT4_GSN=stats`, `GT4_GSN_DRAWS=BP|all`, `GT4_GSN_XFER` (transfers, CLUT loads, XYOFFSET),
  `GT4_GSN_TEXDUMP=TBP`, `GT4_GSN_DUMP=VBLANK` (targets), `GT4_GSN_LIMIT=K`, `GT4_GSN_VRAM=N` (front-end memory),
  `GT4_GSN_TRIGGER=PATH` (live sessions: when that file appears, that frame's lines and targets),
  `GT4_VRAM_DUMP=FRAME:PACKET,...|end` (the memory model at a packet) and `GT4_GS_READ_DUMP=DIR` (what each GS ->
  host read returned). A screen deep in the game is best
  studied offline: `GT4_HOST_RECORD=PATH,FROM,COUNT` (FROM = `trigger:FILE`: when FILE appears) records what the host receives over those VBlanks, with
  the VIF1 batch buffers, the 3D renderers' constants and the GS state at FROM first (captures from before
  2026-10-30 hold VU1's memory and registers instead, and still replay), and `gt4_host --replay PATH --frames N` then plays it at either
  resolution (`--preset original` / `high`), the same frames every time.

The menus' 2D keeps a centred 4:3 area in the game's 16:9 mode (game-side, gt4-native-game
WIDESCREEN.md). Open: COLCLAMP = 0 (wrapping), Z formats as frames and textures (GT4's Z24
pass), dithering, the GPU cost of the colour grading (one dispatch per batch would do). GT mode's map is
broken in the game port itself (paraLLEl-GS shows the same shards): a game-side bug.

## The scene stage

GT4 draws a 3D scene into a 640-wide target with its Z buffer, then reads it for its post effects (glare,
colour grading) into the displayed buffer, where the HUD goes. The front end marks the point where the scene is
complete (a Light command: a target of at least 512 x 384 with 64 or more depth-tested draws, first read by
another; docs/LIGHTING.md). Modern engines put their scene effects there, and so does the back end
(`gsn_scene.cpp`, `execute`): motion vectors, then the lighting of docs/LIGHTING.md, then the resolve.

- **Motion vectors.** Each setup of GT4's 3D renderers (`platform/gs/renderers3d.cpp`) is an object with its
  own transform to clip space. It is matched with the previous frame's by what its first batch draws (GIF tag,
  strip lengths, GS state header) and its rank among the setups that draw the same; its vertices then also carry
  where the previous transform put them (`GsVertex::motion`: homogeneous window coordinates over this frame's w,
  so that they interpolate exactly across the screen; the polygon clipper interpolates them as positions). On the
  Laguna Seca capture: 78 objects a frame, 87% of them found. At the scene's Light command its 3D draws are drawn
  again (`gsn_velocity.vert/.frag`, the position invariant with `gsn.vert`) into a motion vector image with the
  depth test EQUAL against the Z the scene left: only what the scene shows writes, and an alpha-tested fence
  needs no texture (where it was not drawn, the Z is what is behind). 2D and transparent draws (blended without a
  Z write) keep the surface under them; objects not found have none (the resolve's clamp takes care of them).
- **Jitter.** With `render.aa = taa` (and for FSR 2), a scene's depth-tested draws before its Light command
  are offset by a Halton (2, 3) sub-pixel step (`DrawConstants::jitter`), 8 phases, the same for every scene
  of the frame.
- **Resolve** (`gsn_taa.comp`): as the pipeline's TAA without the upsampling: 3x3 reconstruction of the
  jittered samples, a 5-tap Catmull-Rom history at the motion vector of the nearest surface around (GT4's
  larger Z is nearer), variance clipping in YCoCg, blending on reversibly tonemapped values; GT4's alpha is the
  current frame's. Each scene target keeps its history (two images; a scene not resolved the frame before
  starts again; histories not used for 10 seconds go).
- **The other effects** (`effects.*` settings; each a compute pass over the scene's area, in this order after
  the lighting, with the view space rebuilt from GT4's Z, `gsn_view.glsl`):
  - *Reflections* (`effects.reflections`, `gsn_ssr.comp`): screen-space reflections, before the resolve.
    Surfaces facing up and grey (asphalt, concrete, paint) reflect what the scene shows above them with
    Schlick's Fresnel term (F0 0.04: the road ahead at grazing angles, the cars on it); the ray is marched in
    screen space from 2.5 pixels out (its own pixel would hit itself), 1/z interpolated, refined by bisection,
    and perturbed per pixel and frame for a slightly rough lobe the resolve integrates. Low to ultra: 20 to 44
    steps, 55 to 100 % of the Fresnel term.
  - *Fog* (`effects.sky_fog`, `gsn_fog.comp`): height fog in the course's own fog colour (FOGCOL of the scene's
    draws), integrated along the view ray from a camera 1.5 m up, before the resolve: the distance turns hazy and
    the sky above the horizon stays clear. GT4's own linear fog stays as drawn.
  - *Motion blur* (`effects.motion_blur`, `post.shutter`; off in every preset, `gsn_motionblur.comp`): after the
    resolve, McGuire et al. 2012's reconstruction along each pixel's motion, so the player's car (still on the
    screen) stays sharp and does not smear into the streaking road.
  - *Depth of field* (`effects.dof`, `post.aperture`, `post.focus_distance`; off in every preset,
    `gsn_dof.comp`): a thin lens of the scene's field of view, autofocus on the median distance at the centre of
    the screen, a golden-angle gather where a farther sample may not bleed over a nearer pixel.
  - The race's rear-view mirror is another camera drawn into the same target: the front end reports such an inset
    view (a smaller scissor than the main view's, `Lighting::inset`), and these effects and the lighting leave it
    as drawn (the resolve covers it: its motion vectors are its own).
- Debugging: `GT4_TAA_VIEW=1` shows the motion vectors (grey still, through GT4's own grading), `GT4_SSR_VIEW=1`
  the reflections (red the reflectivity, green a hit, blue a miss); the summary line counts the renderers'
  objects and those found in the frame before.

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
- `gs_frontend`: the GIF parser (PACKED, REGLIST, IMAGE), the 3D renderers' primitives (`setPrim`, `vertex`:
  GT4's VU1 programs as native code give vertices directly, docs/VU1_PROGRAMS.md), both drawing contexts, the
  vertex queue (ADC, strips and fans, sprites) and the register state, turned into a frame list: draws with their state,
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
