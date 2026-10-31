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

### Targets

The game's GSBuffers (00105728 binds one as a texture) and its frame buffers become named images at the render
resolution; a texture read from a target names the target and the rectangle.

### What goes from the host

The GS memory model (`gs_memory`), page ownership, the slot-cache uploads, the texture cache keyed by memory,
CLUT loads. The reinterpretations (an 8-bit read of a 32-bit target through a palette) stay until step 3.

## Step 2: 3D on the GPU

The model packets (VIF UNPACK formats inside the model files), the setup constants and the lighting blocks become
native data: vertex buffers made once per model, per-object transforms and lights per draw; vertex shaders do the
renderers' work (transform, clip, the lighting bodies, the key-frame blend). `platform/gs/renderers3d.cpp` and the
VIF1 decoder go.

## Step 3: native effects

Glare, colour grading (the 24-bit frame read as 8-bit indices through the curve palette), the car reflection maps,
the road reflection, the blend passes (004A3998) and the screen grids become render passes of their own; the
reinterpretations and palette tricks go.

## Step 4: timing

The simulation keeps its fixed 60 Hz step; rendering is decoupled (interpolated, any refresh rate). Races draw
progressive frames instead of 224-line fields; "VBlank" leaves the interfaces (the frame clock, the link's VSYNC
record, the host's counters).

## State

| Step | State |
|---|---|
| 1 | in progress. 2026-10-31: texture sets are native: sent once per content, named by the draws, decoded once (pixel-identical to the address path). Next: targets and GSBuffers (the movie) as named images |
| 2 | - |
| 3 | - |
| 4 | - |
