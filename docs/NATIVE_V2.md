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
- **A set is converted once per content.** When the game binds a set whose content the host has not seen, it sends
  the set (its records and uploads, the pixel bytes) with a content hash; the host applies the uploads to a private
  scratch layout and decodes each record into a texture (RGBA8 with its mip levels). This is the PS2 texture format
  decoded as an asset, once, not a graphics memory kept per frame.
- **Content, not address, names it.** The game modifies some sets in place (menu palettes fade: the same upload
  carried 03 03 03 then 07 07 07), so the identity is the content hash taken when the set is bound for upload, as
  the PS2 saw it then. The game remembers which hashes the host has and sends the bytes only for new ones.
- **A draw names its texture as (set, record).** 0049CC48 binds a record by its address; the set's records are
  registered when the set is bound, so the bind becomes a native handle in the drawing state.

A view of the game's memory from the host (the executable's shared .guest section mapped in the host) would read
static data without copies, but not data the game changes after recording it (the palettes above: 540,000 of 1.5
million uploads differed by the time the host looked), so content travels by hash.

What the host does with a set (`GsFrontend::setTexture`): the set's uploads go into a private copy of the layout
(`assetMemory`, loaded per content), the palette goes in as the bind loads it (0049CC48: a 4-bit texture's palette
at a CSA other than 0 is loaded as a whole 8-bit palette at CSA 0, then the texture picks its 16 entries), and the
record decodes, once per (content, record, TEXA where the format uses it). A recording that starts mid-run carries
the sets bound before it (`exportSets`). `GT4_NATIVE_TEXTURES=0` names them by address as before.

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
| 1 | in progress. 2026-10-31: texture sets are native (the race capture is pixel-identical to the address path; live race at 60 fps, 1080p). Next: the remaining textures by address (fonts, menus), targets, then the GS model goes |
| 2 | - |
| 3 | - |
| 4 | - |
