# Native rendering, part 2: no GS model, no VBlanks

docs/GRAPHICS_API_PLAN.md took the PS2's command encoding out of the game (GIF packets, VIF codes, DMA, the
interrupt and DMA controllers). It kept the PS2's model on purpose: the game still names textures and targets by
their address in a 4 MB graphics memory, uploads texture sets into it every frame, and the host still rebuilds
that memory (swizzling, page ownership, palette loads, reinterpretations) and runs VU1's vertex work on the CPU;
the game is paced by a 59.94 Hz "VBlank" and draws 224-line fields in races. This part removes all of it, in four
steps, each checked against today's pictures (the Laguna Seca race capture `build/rec/race.cap`, live sessions).

## Step 1: native resources

### Textures

GT4 keeps its textures in texture sets (the objects 00499728 binds; layout from 00498828 and 00499508):

| Offset | What |
|---|---|
| +0x10 | its position in GS memory (the slot cache's, in 64-block units) |
| +0x12 | its size there (the same units) |
| +0x14 | the number of texture records |
| +0x16 | the number of uploads |
| +0x18 | the texture records, 40 bytes each: TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP (GS register values at the set's position) |
| +0x1C | the uploads, 12 bytes each: EE address of the pixels, then the transfer's destination and size |

The pixels are stored as the GS wants them: an 8- or 4-bit texture is kept as 32-bit blocks, uploaded as such and
read through its own format (GT4_GSN_XFER on the race: 4,806 of 5,427 uploads are 32-bit, while draws read 780
8-bit and 429 4-bit textures and 60 32-bit ones). Every frame the sets are re-sent into a slot cache (1 to 2.6 MB).

Natively:
- **A set is sent once per content.** 00499508 sends a set record (`gfx::TexSetRecord`: a new id, its uploads, its
  slot size) instead of the uploads. The runtime's frame walker hashes the uploads' bytes and layout when the frame
  is sent (the game changes some sets in place: menu palettes fade, the same upload carried 03 03 03 then 07 07 07)
  and sends the bytes only for a content the host does not have.
- **Both ends keep the same sets** (`gfx::SetSlots`, in the order of the set records): a set lives until another is
  uploaded over its slot, a content until its last set goes; the walker then forgets it was sent, the host drops it.
  Nothing is ever asked back.
- **A draw names its set.** The runtime keeps the sets in graphics memory by slot (`runtime/native/pgl.cpp`); any
  TEX0 (or TEX2) flushed with its image inside one names that set, whatever bound it (0049CC48 binds a record, the
  pglu calls, the mirror's own TEX0s). A frame buffer set at a slot ends the set there.
- **The host decodes it once per identity** (`GsFrontend::setTexture`): the draw's fields moved from the slot to
  where the content was first sent, decoded from a private copy of the content (`assetMemory`), keyed by (content,
  fields, palette entries, TEXA where used). The address path decoded a set again each time the slot cache moved it.
- **Graphics memory still gets the content**, from the host's copy at the slot: palettes are loaded through the
  CLUT buffer (CLD, CSA, a 4-bit palette loaded whole as 8-bit by 0049CC48), the game writes palettes of its own into
  set pages afterwards, and the effects read targets through palettes there. A set texture whose pages were written
  since goes by address. This memory goes with the native palettes of step 3.
- `GT4_SET_UPLOADS=1` (runtime) also sends the old uploads, `GT4_NATIVE_TEXTURES=0` (host) uses them: the same
  recording both ways, to compare.

Measured on a Laguna Seca recording (100 frames, 1080p): the native path is pixel-identical to the address path; the
front end's thread 16.6 ms a frame against 19.2. Every texture that is not a target or a reinterpretation of one
comes from a set in the race (and in the menus but the movie's GSBuffer).

### Graphics memory, lazily

Graphics memory (`gs_memory`) gets a set's bytes only where something reads or writes them there (a page at a
time, `GsFrontend::materializePage`): an upload or a draw over part of a set's page, a read of it by address, a
transfer, a recording's start. Palette loads from a page that still holds a set read the set's content. Both ends
keep a content for the 512 contents dropped after it (the slot cache brings the same sets back every frame), so
its bytes cross once. On the race recording 130 pages a frame reach memory, against every set's pages before.

### Targets

Targets are GPU images already (the renderer samples them with an offset); what names them is page ownership,
bookkeeping of which target last wrote which 8 KiB page. Its readers are the post effects (reinterpretations,
glare, reflections): step 3 replaces those with passes of their own, which name their images natively. The
game's GSBuffers (00105728 binds one as a texture, 001056A0 as a frame buffer) and the movie's are the remaining
named images to give the effects.

## Step 2: 3D on the GPU

Done in two parts (2026-10-31):

- **Homogeneous output, the GPU clips.** The renderers' vertices carry homogeneous window coordinates (`x w`,
  `y w`, `Z w`, `w`; `GsVertex::h`, `gsn::Vertex::w`) and the vertex shader puts them in clip space, so hardware
  clipping replaces the VU's polygon clipper and guard band; all varyings are `noperspective`, the GS's
  interpolation. Culling is the sign of the clip-space determinant of (x, y, w), which agrees with the VU's
  projected test in front of the eye and holds across the eye plane. (`GT4_VU_CLIP=1`: the VU's clipping.)
- **The renderers in a compute shader** (`gsn_models.comp`, one workgroup per batch): the lighting chain (the
  decompression routines, the 15 bodies), the transform, fog and ADC, culling, the motion vectors from the
  previous frame's constants; it writes the batch's triangles into the frame's vertex buffer, after the CPU's
  vertices. The stream thread hands a batch whole (`gfx::ModelJob` and its 256 qwords); the front end works out
  its state, targets and texture once and reserves its vertices. A batch drawn again without new vertices runs
  the chain that decoded them again (`BatchBuffer::vertexWrites`). (`GT4_GPU_MODELS=0`: the CPU renderers.)

On the race recording: 97.5% of the pixels identical to the CPU renderers, 99.97% within 4 per channel (edges a
subpixel apart); the stream thread's renderers 7.2 ms a frame -> 0.5 ms; live, 60 fps at 1080p.

- **Model packets once per content.** The runtime's frame walker hashes each model packet and sends its bytes
  only when the host lacks them (`gfx::ContentFifo`, the last 8192 on both ends); the host decodes a packet once
  per (content, VIF state) and replays its effect on the batch buffers after that (`Vif1::batchModel`; 97% of the
  packets replayed; `GT4_VIF_MODELS=0`: decoded every time). A 100-VBlank race recording went from 690 MB to
  119 MB; the stream thread from 13 ms a frame to 2.8 ms.

What is left of the PS2's 3D path: the batch buffers' layout (the job the GPU reads is the batch as the VU saw
it), the VIF decoder for a packet's first decode, and the CPU renderers as the reference (`GT4_GPU_MODELS=0`)
and for the screen grids, whose packets are post effects (step 3).

## Step 3: native effects

GT4's post effects that worked through the GS's memory tricks become passes of their own, sent as effect records
(`gfx::EffectRecord`, in order with the draws):

- **The race's colour grading** (004A7D90 / 004A7FA8 -> 004A81A8, done 2026-10-31). The PS2 read the 24-bit frame
  as 8-bit indices in three additive sprite passes through the curve palette (one channel each, modulated by a
  column of a saturation matrix: x I + (1 - x) luma), summed into a scratch buffer, then loaded that buffer as
  palettes and drew an index ramp through them back into the frame. Per pixel: out_k = min(255, sum_c
  (curve[byte_c].k * weight_c.k) >> 7). `gsn_grade.comp` computes that per render pixel; identical to the GS's way
  at 1x (the race recording), and no reinterpretation is left in the race. `GT4_EFFECTS_BOTH=1` (runtime) sends both
  ways with the GS's between markers; the host draws one or the other (`GT4_NATIVE_EFFECTS=0`: the GS's).
- Glare (003BADF8), the blend passes (004A3998) and the reflections are plain draws of targets into targets: the
  renderer draws them as such. The reinterpretation path stays as the fallback for an effect not seen yet (004A9470,
  grid 0x3EA0, did not run in the race or the menus).

## Step 4: timing

- **60 Hz** (done 2026-10-31): the runtime's frame clock steps the game at 60 Hz, not 59.94 (a 60 Hz display no
  longer repeats a frame every 16 s; the sound mixes 800 samples a frame; alarms keep 262.5 H-blanks a frame).
- **Progressive races** (done): `display.race_frames = progressive` (the default) makes the game apply its 480p mode
  where it would use fields (00436640): whole 448-line frames, twice the vertical detail, 16-bit frame buffers (the
  displayed target is chosen by its format's layout, since the 480p frames sit at the 480i ones' addresses).
- **Presentation decoupled** (done): between two game frames each display refresh draws the last frame list again
  with its moving 3D's transforms blended by the time passed (`gsn_models.comp`: the previous frame's and this
  one's, as the motion vectors already matched them), shown from the buffer the list draws into (the game draws a
  back buffer while it shows the front one: t = 0 is the state on screen, 1 the new one, so a new list continues
  exactly where the last presentation was). Measured live at 144 Hz: every presented frame moves on (frame-to-frame
  change 1.0-1.5) where without it a repeat (0.26) and a jump (3) alternate. 2D, particles drawn by the CPU's paths
  and objects not seen in the previous frame step at 60 Hz. GPU cost: the frame's work per refresh (1080p high: about
  5 ms).
- **Any shape** (done): the content aspect reaches the game at start-up and every frame (`aspect.ratio = window`: the
  window's), so Hor+ 3D and the anchored HUD follow 21:9 and 32:9 (checked live) as well as 16:9 and 16:10. Not yet:
  the rear-view mirror and the minimap keep their 16:9 placement.
- Left: VBlank in the interfaces. The frame clock still delivers the game's frame callbacks as interrupts (the
  kernel suspends the running guest thread), and the link's record is named VSYNC.

## State

| Step | State |
|---|---|
| 1 | in progress. 2026-10-31: texture sets are native: sent once per content, named by the draws, decoded once (pixel-identical to the address path). Graphics memory gets set bytes lazily. Targets: with step 3 |
| 2 | done 2026-10-31: the renderers run on the GPU (homogeneous output, hardware clipping, a compute shader); model packets go once and decode once. Live race 60 fps at 1080p; front end 10 ms, stream 3 ms a frame |
| 3 | in progress. 2026-10-31: the race's colour grading is a pass of its own; no reinterpretation left in the race |
| 4 | in progress. 2026-10-31: 60 Hz clock, progressive races, presentation interpolated to any refresh rate, any aspect ratio; left: the frame callbacks as interrupts |
