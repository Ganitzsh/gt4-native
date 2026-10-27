# GT4's VU1 programs, natively

GT4 draws its 3D through vector unit 1: the EE packs vertices into VIF1 batches, the VU transforms,
lights, culls and clips them, and kicks GIF packets to the GS (PATH1). `gt4_host` runs those programs in C++
(`platform/gs/vu1_native.cpp`); this page says what each one does. Addresses are byte addresses in the
micro image; no game data is reproduced here.

## The micro image

GT4 uploads one image at boot: 2,046 instructions in one VIF1 `MPG` burst (FNV-1a `8B6E3B5747FAFFF5` for the
NTSC-U v1.01 executable; `GT4_VU1_INVENTORY=DIR` writes it to `DIR/images/` with a table of every program
started, its executed ranges and the GIFtag shapes it kicks). 1,590 of the instructions run. The native code
checks the hash and leaves any other image to the interpreter.

| Part | Address | What it does |
|---|---|---|
| Setup | 0x0000 | Loads the transform (vf28..31), viewport scale/offset with fog in w (vf25, vf26), the clamp and fog start (vf24) and the guard band (vf23) from the batch |
| Lighting chain | 0x0048 | Jumps to ITOP: one of 15 blocks, each a five-entry table (a vertex-decompression routine) and a body |
| Decompression | 0x1810 .. 0x1A38 | Integer positions and 4.12 texture coordinates to floats; byte normals scaled by 1/127 (in the colour or ST slot); two key frames blended by a weight |
| Lighting bodies | 0x0118 .. 0x16F8 | See below |
| Entry table | 0x1B08 .. 0x1EC0 | 24 groups of five instructions: [setup], [lighting], one of six renderers, [state packet] |
| Renderers | 0x1EC8, 0x2058, 0x2070, 0x2300, 0x27A8, 0x2AD8 | Triangle strips: transform, perspective, fog, ADC, back-face culling, clipping |
| Clipper prelude | 0x2E08 | Guard-band test and back-face test in homogeneous coordinates |
| Polygon clipper | 0x2FA8 .. 0x3D48 | Sutherland-Hodgman against the guard band's six planes; one strip packet per clipped triangle |
| Screen grids | 0x3D50, 0x3EA0 | Full-screen meshes for post effects, kicked piece by piece with patched fields |

## The batch

Quadwords from TOP (the VIF double-buffers at 0 and 435):

| Qwords | Contents |
|---|---|
| 0..63 | Positions |
| 64..127 | ST (x, y); the w words hold the strip count (64) and each strip's length (65..) |
| 128..191 | Colours (integers), or normals for lit models |
| 192 | GIFtag of the packet (NLOOP = the vertex count) |
| 193..200 | Setup constants |
| 201..216 | Lighting constants |
| 217 | An optional GS state packet, kicked before the strips |
| 242.. | The output packet: tag, then ST, RGBA, XYZF2 per vertex |
| 870..1023 | Scratch, beyond both buffers: kick address, clipper state, two clip packets (878, 903), clip records (928..) |

A strip's length is the distance in output quadwords from its first to its second-to-last vertex: the loops
run two vertices before the first end test and one after. A strip has at least three vertices (with two the
loops never end).

## Renderers

Every vertex: `P = M p` (MULAx..MADDw with w = 1), `q = 1/P.w`, `s = min(P, clamp) q`, `ST = (s, t, 1, 0) q`,
`XYZF2 = FTOI4(offset + s * scale)`; colours are copied. The first two vertices of each strip use the offset
with 2048 or 3072 added to fog, which sets ADC (the GS does not draw the triangle a vertex with ADC completes).

- **0x1EC8**: no culling. **0x2058 / 0x2070**: back-face culling by the sign and zero flags of the cross
  product of the projected edges (kept when z > 0, resp. z < 0); the operand order alternates with the
  vertex's index in its strip, as the loop handles two vertices per pass.
- **0x2300**: clipping, no culling. **0x27A8 / 0x2AD8**: clipping and culling (a culled vertex gets 3072 added to
  its fog; from the second vertex of the packet on, ST's w is 3072 q). For each vertex j >= 2 of a strip, its
  triangle: all three vertices outside one frustum plane, not drawn; else if inside the guard band (the points
  scaled by vf23), drawn as is (the GS clips it); else 0x27A8/0x2AD8 repeat the back-face test in homogeneous
  coordinates, and what remains goes to the polygon clipper, whose packet replaces the triangle.

The polygon clipper keeps its vertex records in VU memory, and the native code does too: the strip packet at
qword 903 overruns them for polygons of nine vertices or more, and the emit loop then reads what it wrote.
The record allocator is not reset within a call; when it would run past qword 1023 (random geometry only, so
far) the native code gives the batch to the interpreter.

## Lighting bodies

All work in place for NLOOP vertices, normals in the colour slot unless noted, results clamped and converted
to integers. `d = max(L n, 0)` per light, from the matrix in qwords 201..203 (a second set in 209..211).

| Body | Model |
|---|---|
| 0x00B0 | None |
| 0x0118, 0x0290, 0x0418 | Ambient + two diffuse terms + two terms to the 4th / 8th / 16th power. 0x0290 starts its sum with MADDA, so the first two products of the next vertex's N.L are included (a quirk of the original, kept) |
| 0x05B0, 0x0788, 0x0960 | Four diffuse terms + four from the second set to the 4th / 8th / 16th power |
| 0x0B48 | Four diffuse terms |
| 0x0CA8, 0x0EA0 | Specular from the eye: the eye vector reflected about the normal, against one light, to the 8th / 16th power; alpha 128 |
| 0x10A8 | Reflective paint (normals in the ST slot): colour darkened where the surface turns away, ST a sphere map of the reflection |
| 0x12C8 | Like 0x10A8, with ST from a quadrant map of the reflection (u = quadrant + ratio, v = t^3 + 2t) |
| 0x1508 | Prelit colours scaled and offset |
| 0x1608 | Constant colour, ST a planar projection of the position |
| 0x16F8 | Fade: alpha from a per-object table; alpha 0 ends the program with an empty packet |

## Exactness and checks

The native code computes with `VuF` (`platform/gs/vu_float.h`): the VU's float rules (no denormals,
infinities or NaNs; products and sums truncated, with the hardware adder's limited guard bits), in the
program's operation order, so its output is the reference interpreter's bit for bit. EFU results use host
single precision as the interpreters do.

- `GT4_VU1_DIFF=native`: every program also runs in the reference interpreter (`vu1_ref.cpp`) from the same
  state; the PATH1 packets and the data memory below the scratch area must be identical. A race (93,670 runs)
  and car select (281,368 runs) are identical.
- `tests/vu1_native_fuzz.cpp` (ctest `vu1_native_fuzz`, needs `GT4_VU1_IMAGE=path` to the image): random
  batches through every entry, every lighting block and table entry, both buffers, and the grids; 87,680 runs
  identical.
- `GT4_VU1=interp` runs everything in the interpreter, `GT4_VU1=ref` in the reference interpreter.

`vu1.cpp` (the interpreter with the VU's pipeline timing) computes in double precision and truncates; it
differs from the reference in the last bit now and then, which matters only where a test sits on a knife
edge (the homogeneous back-face test of zero-area joining triangles).

Replaying 250 frames of car select headless takes 24 s with the interpreter, 6.2 s with the native programs.
