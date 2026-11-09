# Spike N3 step 0: which lit-road draws are lamp light

N3 draws the street lamps' light on the road itself (PLAN.md N3). GT4 bakes that light into a layer of decals drawn
over the road, and N3 must skip exactly the decals that are lamp light: never a road marking, never a logo. Step 0
found the rule from GT4's own data. Lane F, 2026-11-08 and 09; the architect set each step and accepted v4
(2026-11-09).

GT4's night set is closed: Clubman Stage Route 5 (`crs/route5_club`) and Special Stage Route 5 Reverse
(`crs/r_route5`). Seoul Central served as the day check. Captures, frames and images stay in `build/` (PLAN rule 5).

## The layer

The lit-road layer's draw state: textured, blend (Cs - Cd) As + Cd, no alpha test, z GEQUAL, no z write, lighting body
0. The light lives in the textures. Vertex colours are uniform per item (a tint, or 0x80 = 1.0 by day).

That state alone also matches road markings (lane dashes, edge lines, the start/finish checker), crossings and logos by
day, and a chain-link fence. Every term below exists because one of those passed without it.

## The rule (v4)

Decided per item at first sight, cached by (material, item). A frame without `kFrameNight` (FrameBegin flag 8, the
architect's contract) skips nothing (counted as `n3SkipGatedOff`).

| Term | Measured | Threshold |
|---|---|---|
| layer state (above) | | exact |
| vertex colour luminance, alpha | pools: luminance 32-125, alpha 128 | > 16, >= 100 (v2) |
| texture centre/rim | pools 0.42-1.09; F2b's glows >= 3 | < 3 (not a glow) |
| share of area with abs(n.up) >= 0.9 | pools 1.000 (steepest item 5.7 deg); fence 0.000 (90 deg) | >= 0.9 |
| footprint width | lane markings: items 0.13-0.60 m; pools: items 0.42-10.0 m, median 3.3-5.8 | >= 0.9 m |
| world size per texel, max(u, v) | start/finish checker 0.017 m; pools 0.039-0.068 m | >= 0.03 m |

- Up is world y; every lit-road item has an identity rotation (largest deviation 0.000). Normals are taken in world
  space, so either winding counts.
- Footprint width: the item's up-facing triangles are split into connected pieces (vertices welded at 1 mm); each
  piece's width is 2 x area / perimeter (a curved edge line stays narrow), area-weighted over the item.
- Width threshold: 1.5 x the widest marking item (0.60 m). At 0.9 m no marking item is skipped (0 of 84,356), and 17 of
  1,012 pool items stay baked: 208 of 360,630 area (0.058 %). A pool sliver that stays baked keeps GT4's look, which is
  the safe miss. A per-material average separates better (0.21 m against 3.4 m), but the first-sight cache can't wait
  for it.
- Texel size: from each up-facing triangle's UVs, |dP/ds| and |dP/dt|, divided by the texture's width and height. Lamp
  light textures are coarse and stretched over metres; painted markings carry detail. The only wide marking found, the
  start/finish checker on Route 5 Reverse, is 1.74 m wide at 0.017 m per texel. That gap (0.017 to 0.039) rests on one
  item, which is why the proof below is exhaustive.

## How it was found

1. v1 (texture brightness) and v2 (vertex colour luminance > 16, flat summary, alpha >= 100) caught every pool labelled
   by the road's luminance change. v2 failed held-out: a chain-link fence (64x64 grey, vertex colour (40, 33, 29)) on
   Route 5 Reverse.
2. v3 added the up-facing share; the gap runs from 5.7 to 85 degrees. Seoul by day still matched v3: its crossings and
   lane lines (hence `kFrameNight`).
3. Images of every material, skip on against off, retired the luminance labels: removing a white marking darkens the
   road too, so lane markings had come out 'pool' (Route 5 Reverse's largest look, 128x64 mean (109, 88, 70)).
   Vertex colours and texture summaries do not separate markings from pools.
4. Footprint width separated them on both courses, until a hunt with every wide texture skipped removed Route 5
   Reverse's checkered start/finish band. The texel term separates that band.

## The proof: both full laps

Recorded in B-spec (the game drives) from a fresh copy of the approved save: Clubman Cup race 4 (Clubman Stage Route 5)
and Race of NA Sports race 4 (Route 5 Reverse), with scene records, each a full lap plus the start stretch again (a
replay's first ~300 frames show textures not yet re-uploaded, and are left out of every count). A debug host judged
every item by v4 at first sight and skipped its texture from then on, against the same replay without the skip. Frames
every 25, and every 10 across each start/finish line.

Result: on both laps only lamp light changes. Every lane dash, edge line, kerb, logo and both start/finish checkers
stay.

| Lap | Items judged | Skipped | Identities | Kept although wide and flat |
|---|---|---|---|---|
| Clubman | 4,658 | 174 | 32 | the checker |
| Route 5 Reverse | 144,732 | 640 | 131 | the checker (3 identities) |

- Skipped: the amber pool (four vertex tints), the white pool, the white pool's 512x512 sibling and the tunnel's lamp
  light; on Route 5 Reverse also the amber pool in each section set.
- Every set-served skipped texture sits at a pool's place on the disc: the light set (tbp 539, and the 512x512 sibling
  at tbp 0) or the amber pool at tbp 0, cbp 32 of a section set. Both checkers (1.742 m, 0.0171 m per texel) were kept.
- The other skipped identities take the memory-address path (no texture set). Their summaries were taken before the
  texels arrived (means like (0, 0, 0)). 352 of the 415 such items repeat a set-served pool item's exact geometry
  (width and texel size to the millimetre), e.g. a 256x256 (0, 0, 0) item at 6.626 m and 0.0533 m per texel, judged
  again under the white pool's real texture later. v4's deciding terms (share, width, texel size, vertex colour) come
  from the geometry, so unread content can't make a marking pass. The real upload is a new identity, so a new
  material, judged again.
- Images: 389 sampled frames on Clubman and 437 on Route 5 Reverse, each base against skipped. Ranked by edge loss
  (sharp edges in base gone when skipped), the top frames were inspected at full size, along with every start/finish
  frame.

## Parked fallback: a texture locator

If v4 ever fails, the coordinator allows a short list of texture content hashes under six conditions (on the owner's
list). The disc side is mapped, and no list file exists:

- GT4.VOL lists from the user's ISO with a RoFS reader built from IOP_SERVICES.md 6.3 (18,352 files). Slots are four
  u16s (key offset and length, value offset and length); values are a type byte then little-endian u32s.
- Each course is one deflated file. In the native path a texture is its texture set's content plus its place in the set
  (tbp, cbp, format). Of the 2,685 sets sent on Clubman's lap, 2,635 appear verbatim in `crs/route5_club`.
- A list line would name the course file, the set's offset and its place: for Clubman, the amber pool's set at 0x3C4780
  (tbp 4, cbp 56, PSMT8, 128x128) and the white pool's at 0x3EDB80 (tbp 539, cbp 512, PSMT8, 256x256). On Route 5
  Reverse the amber pool sits at tbp 0 in 12+ per-section sets with their own content, so the list would be longer.
