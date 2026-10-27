# The native renderer (R1)

`gt4_host` shows GT4 two ways. The `original` preset keeps paraLLEl-GS: the PS2's GS emulated bit for bit at
640 x 448, its frame handed to the display pipeline. Every other preset will use the native renderer: the
same GS command stream drawn by Vulkan at the host's resolution, with the pipeline's anti-aliasing,
upscaling and post-processing on top. This page is its design and its state.

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

Front end (CPU, on the GS worker thread), `platform/gsnative/`:

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
  one depth image per Z buffer. Texture reads of memory a target owns sample the target.
- **Textures**: decoded from the memory model into RGBA8 images, cached by address, format, palette and
  upload generation.
- **Pipelines** keyed by blend (dual-source for As and FIX), alpha test and its fail mode, depth test and mask,
  colour mask, fog, primitive type; DATE through the stencil buffer (the alpha bit copied into stencil before
  a DATE run, updated by the draw itself with shader stencil export); a clamp pass after blends that can
  exceed 255 (COLCLAMP).
- **Display**: PMODE / DISPFB / DISPLAY of both circuits composed into the scene's colour target.

## Order of work and acceptance

1. Memory model and transfers; every format round-trips and matches paraLLEl-GS's memory.
2. Front end: the frame list of a captured frame matches `gsframe.py`'s decoding.
3. Back end at 1x: each captured frame within a measured tolerance of paraLLEl-GS (per-pixel difference
   statistics, the worst regions listed); the passes above one by one.
4. Render scale, then widescreen (game code: the monitor matrix, docs/WIDESCREEN.md), MSAA and texture
   filtering options, motion vectors for the temporal paths.
