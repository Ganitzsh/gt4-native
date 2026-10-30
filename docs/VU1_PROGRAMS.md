# GT4's VU1 programs, natively

GT4 draws its 3D through vector unit 1: the EE packs vertices into VIF1 batches, the VU transforms,
lights, culls and clips them, and kicks GIF packets to the GS (PATH1). `gt4_host` has none of that hardware:
VIF1 is decoded into batches (`platform/gs/vif1.cpp`), and GT4's programs are native renderers
(`platform/gs/renderers3d.cpp`) that read a batch, compute in plain float arithmetic and hand the native
renderer's front end primitives with their GS state (`gs_primitives.h`, `GsFrontend::setPrim` / `vertex`): no
VU1 memory or registers, no XGKICK, no GIF packet encoded and parsed again. This page says what each program
does. Addresses are byte addresses in the micro image (the programs' names); no game data is reproduced here.

## The micro image

GT4 uploads one image at boot: 2,046 instructions in one VIF1 `MPG` burst (FNV-1a `8B6E3B5747FAFFF5` for the
NTSC-U v1.01 executable; `GT4_VU1_INVENTORY=DIR`, a reference-build tool, writes it to `DIR/images/` with a
table of every program started, its executed ranges and the GIFtag shapes it kicks). 1,590 of the
instructions run. The host keeps the uploaded image only to check this hash; another image draws nothing.

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

How a batch is assembled (`vif1.h` `BatchBuffer`): the VIF double-buffers, BASE (0) and BASE + OFFSET (435)
taking turns at each MSCAL, and UNPACK with FLG writes relative to the current one; so the host keeps two
batch buffers of 256 qwords and an MSCAL hands the renderer the one just filled. GT4 relies on what a buffer
held before in two ways, and the host keeps both:

- It sends lighting constants (201..216) once per model: later batches of the same buffer read them where
  they were left (in a GT Auto capture, 444 of 924 batches of body 0x0B48).
- It draws a batch a second time without sending its vertices again (in the race, entry 0x1BF8 with no
  lighting chain right after a chain that decoded the same buffer): the second draw reads the vertices as the
  first left them, positions and texture coordinates converted from integers, colours lit. So the lighting
  chain writes the vertex data it decodes back into the buffer (positions x, y, z; the ST lanes it set; the
  colour slot as normals or lit colours), and every renderer writes the batch's tag to qword 242, where the
  original's output packet starts (a key-frame batch reads its weight from 242 .w). The clipper's scratch
  memory and the output vertices beyond qword 242 are nobody's input and are not kept.

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

The polygon clipper's output is a strip of its own, drawn before the batch's strip (the batch's state header
first, then each clipped polygon in triangle order, then the batch): the ring from both ends (first, last,
second, second to last, ...), the first vertex with the ADC offset, colours rounded (+0.5), NLOOP the ring's
size. The original keeps the ring as records in VU memory (qword 928 on) and builds the packet at qword 878
or 903, which a polygon of ten vertices or more would overrun; a triangle clipped against six planes has at
most nine, so only a ring made non-convex by rounding gets there (random geometry does, 1,743 of 657,600
fuzz runs; the game's has not). The host draws such a polygon as clipped (`Renderers3d::Stats::largeClips`).

The rules of the VU's number format that shape the picture are written out in `renderers3d.cpp`: game
floats with exponent 255 are large numbers, not infinities or NaNs (the projection clamp of qword 199 holds
0x7FFFFFFF in lanes it does not clamp); denormals read as zero; a division by zero (a vertex on the eye
plane) gives the largest float with the quotient's sign, as do RSQRT and ERLENG of zero; results saturate
instead of overflowing; FTOI4 / FTOI0 truncate toward zero and saturate at the 32-bit limits, and the GS takes
bit fields of those words (X, Y the low 16 bits; Z bits 4..27 of XYZF2; fog bits 4..11 and ADC bit 15 of the w
word, so that a vertex both hidden by the clipper and culled, 3072 added twice, is drawn again as on the
PS2); the culling tests read the zero and sign flags, where a result below the smallest normal float counts as
zero; CLIP compares x, y, z against |w|.

## Lighting bodies

All work in place for NLOOP vertices, normals in the colour slot unless noted, results clamped and converted
to integers. `d = max(L n, 0)` per light, from the matrix in qwords 201..203 (a second set in 209..211).

| Body | Model |
|---|---|
| 0x00B0 | None |
| 0x0118, 0x0290, 0x0418 | Ambient + two diffuse terms + two terms to the 4th / 8th / 16th power. 0x0290 starts its sum with MADDA, so the first two products of the next vertex's N.L are included (a quirk of the original, kept; after the last vertex, the next qword of the buffer as it is) |
| 0x05B0, 0x0788, 0x0960 | Four diffuse terms + four from the second set to the 4th / 8th / 16th power |
| 0x0B48 | Four diffuse terms |
| 0x0CA8, 0x0EA0 | Specular from the eye: the eye vector reflected about the normal, against one light, to the 8th / 16th power; alpha 128 |
| 0x10A8 | Reflective paint (normals in the ST slot): colour darkened where the surface turns away, ST a sphere map of the reflection |
| 0x12C8 | Like 0x10A8, with ST from a quadrant map of the reflection (u = quadrant + ratio, v = t^3 + 2t) |
| 0x1508 | Prelit colours scaled and offset |
| 0x1608 | Constant colour, ST a planar projection of the position |
| 0x16F8 | Fade: alpha from a per-object table; alpha 0 ends the program with an empty packet |

## Exactness and checks

The renderers compute in host single precision, rounding to nearest where the VU truncates, so they are not
the VU bit for bit; the reference is the bit-exact C++ port they replace (`tools/vu1ref/vu1_exact.cpp`, which
computes with `VuF`, the VU's float rules, and matches the reference interpreter bit for bit on every run of a
race, car select and GT mode's menus and on 657,600 random batches). It and the interpreters build only with
the CMake option `GT4_VU1_INTERPRETER` (docs/GS_BACKEND.md, "The VU1 reference").

- `GT4_VU1_DIFF=1` compares both outputs batch by batch as GS register writes and vertices. Captures of the
  race route recorded live on 2026-10-30 (`GT4_HOST_RECORD`, 300 VBlanks each):
  - the race start (the track flyover, 337,079 batches, 11.7 million vertices): 95.2% of the batches
    identical or within rounding (an eighth of a pixel, Z by 64 of 2^24 plus 1/8192 of Z, colours by 2, ST by
    1e-3, the same ADC); 0.56% of the vertices beyond, nearly all of them hidden (ADC) points far outside the
    screen; 1,690 drawn ones (0.014%): clipped-polygon corners on the guard band whose Z differs by about
    100 of 2^24 (the viewport's Z is near 2^28 before its offset, where a float's step is 32), and back-face
    decisions on zero-area triangles; 401 coordinates that sit at 0 and wrap (0 against 0xFFFF);
  - the Sunday Cup event screen with the car (381,654 batches, 17.5 million vertices): 99.2% of the batches
    within rounding, 3,722 vertices (0.02%) beyond, all a back-face decision (ADC) on a zero-area triangle.
- The pictures replayed from those captures (`gt4_host --headless --replay ... --size 640x448`, every tenth
  frame of 300, against the build before the change): race 97.4% of the pixels identical, 99.86% within 4
  per channel, 99.993% within 16 (preset `original`; `high` 96.8%, 99.82%, 99.98%); event screen 94.4%,
  99.998%, 99.9995% (`high` 94.0%, 99.97%, 99.996%). What differs: texels and Gouraud colours one step apart,
  and edges moved by a sixteenth of a pixel (a position the VU truncates to x.9375 lands on x.0), most visibly
  on the HUD's text, which GT4 draws through these programs too. The same build drawing with the exact port
  (`GT4_VU1=exact`) reproduces the old frames exactly.
- `tests/vu1_native_fuzz.cpp` (ctest `vu1_native_fuzz`, needs `GT4_VU1_IMAGE=path` to the image): random batches
  through every entry, every lighting block and table entry, both buffers, and the grids. The exact port
  against the interpreter (all identical but the clip records that wrap), and the renderers against the
  interpreter, per ITOP: the grids identical, 1.7% of the drawn vertices beyond rounding, from random geometry's
  knife edges (zero-area triangles; triangles behind the eye, where a point the clipper puts on one plane,
  z = -w with w < 0, lies on another too, z = |w|, and rounding decides whether it is clipped again). Colours are
  left out there: random colour words overflow when lit, and the GS keeps the low byte of a large integer.

Replaying 300 VBlanks of the race start headless takes 8.4 s with the renderers, 12.3 s with the exact port.
