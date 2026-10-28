# Rendering: the display and image-quality pipeline

`gt4_host` (`-DGT4_BUILD_HOST=ON`) takes "a frame rendered by the game layer" and turns it into the image on
the screen: anti-aliasing, upscaling, HDR post-processing, the display transform, aspect-ratio fitting, the 2D
layer and presentation. Until the game's own renderer exists (milestone R1, `docs/HW_ACCESS.md`) the frame comes
from a procedural test scene (`platform/render/test_scene.cpp`, no game data) that exercises every stage: fine
geometry (fence wires, gantry trusses), textures seen at grazing angles, moving cars with clear-coat paint,
very bright emitters, a large depth range, an analytic sky and a 2D HUD. The R1 renderer plugs into the same
interface (`platform/render/scene.h`, see "Scene contract"). With `--game` or `--replay` the scene is the PS2's
displayed frame from the GS back end (`platform/render/gs_scene.cpp`, `docs/GS_BACKEND.md`): the game's own
image, which the `original` preset shows unaltered.

Rendering only observes game state (`docs/PLATFORM.md`): the scene advances only through `TestScene::tick()`
at the game's fixed 60 Hz; frames interpolate between ticks, and no setting, frame rate or GPU feeds back into
the simulation.

## Running it

```bat
tools\build_windows.cmd Release            :: VS 2022 + Ninja, -DGT4_BUILD_HOST=ON, builds and runs ctest
build\host-windows-Release\gt4_host.exe    :: interactive window (keys below); Escape opens the settings menu
gt4_host --headless --size 2560x1440 --frames 300 --preset ultra --capture build\captures\ultra.ppm
gt4_host --list-gpus | --list-displays | --list-options | --help
py tools\host_session.py run --exe build\host-windows-Release\gt4_host.exe --host-arg=--headless ^
   --script tests\scenarios\features.json --no-audio      :: one capture per feature, HTML report
```

- **Headless** (`--headless`): the whole pipeline renders into an offscreen image of `--size WxH` (default:
  the window size, 1280x720). No window, surface or swapchain is created, so any GPU can be used (default: the discrete one)
  and no desktop is needed. Captures, session commands and reports are the same as in windowed runs.
- **Sessions** (`tools/host_session.py`, `--session-dir`): a virtual gamepad plus a command queue (`axis`,
  `button`, `reset`, `capture NAME`, `set KEY VALUE`, `wait FRAMES`, `resize W H`, `tone HZ S`, `quit`). The
  report lists the GPU, every setting change with the resulting pipeline, GPU frame times and the captures as
  PNG. `--host-arg=...` passes options to gt4_host. Windowed runs started from Windows session 0 (services, WSL)
  are launched on the logged-in desktop through `tools/host_windows.ps1`, because no window can present in
  session 0. `tests/scenarios/controller.json` is the input/resize/audio diagnostic (`--verify-diagnostics`);
  `tests/scenarios/features.json` captures every feature for before/after comparison.
- Headless runs and sessions advance exactly one tick per frame (`--fixed-step`), so captures are reproducible.
- **Validation**: set `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`. Messages are printed and make a session
  report fail. 53 settings combinations (every AA mode, upscaler, quality mode, MSAA count, effect tier,
  tonemapper, aspect ratio and preset) run validation-clean on the RX 7900 XTX.

Interactive keys: F1 readout, F2 preset, F3 anti-aliasing, F4 upscaler, F5 render quality, F6 aspect ratio,
F7 tonemapper, F8 HUD layout, F9 vsync, F10 MSAA, F11 or Alt+Enter fullscreen, B bloom, N depth of field,
M motion blur, H shadows, R reflections, F fog, T texture filter, Y anisotropy, `[` `]` render scale, `-` `=`
sharpness, G grain, V vignette, C chromatic aberration, P pause; Shift reverses a cycle. The arrows or the left
stick swing the chase camera; Space or the south button brightens the lights. Gamepad: Start preset, Back
readout, D-pad anti-aliasing and render quality, shoulders upscaler, north button aspect ratio.

## The settings menu

Escape, or the pad's Guide button (or Select + Start), opens the settings menu over the picture: the options that
apply to what is shown (over the game: preset, renderer, render resolution and scale, upscaler, sharpening,
anti-aliasing, anisotropic filtering, window, v-sync, frame rate limit, aspect ratio, menu layout, grain, vignette,
chromatic aberration, dither, the readout), each with a description, then Resume and Quit. Up/Down (D-pad, left
stick, W/S) choose, Left/Right change, L1/R1 or PgUp/PgDn jump between sections, Enter or the south button
accepts, Escape, Backspace, Circle or Start close it. Changes apply at once and are saved to the settings file.

Over a live game the menu pauses it: the host sends `GT4_REC_HOST_CMD` 2 and the game's kernel stops its bus
clock, so no VBlank, alarm or timer comes due (sound stops with the VBlanks that drive it); 3 resumes it, and the
time spent paused is left out of the clock. The pad reaches the game again once every button is released, so
the button that closed the menu does not also act in the game. The aspect ratio and the menu layout are passed
to the game when it starts: they are marked `*` and apply at the next start. `GT4_HOST_KEYS=FRAME:KEY,...`
(escape up down left right return backspace pageup pagedown f1) presses keys at host frames, for unattended
checks of the menu, also without a window.

## Settings

One struct (`platform/sdl/settings.h`), one versioned INI file, command-line overrides and presets. Every
option with its values and effect is in the reference at the end, and `gt4_host --list-options` prints it.

- **File**: `settings.ini` in SDL's per-user preference directory (Windows `%APPDATA%\gt4-native\host\`, Linux
  `~/.local/share/gt4-native/host/`). Interactive runs create it with the defaults, read it and save every
  change made with the keys. Scripted runs (`--frames`, `--smoke-test`, `--session-dir`, `--headless`) start
  from the defaults and never write it, unless `--settings PATH` names a file.
- **Format**: `version = 1`, then `[section]` blocks of `key = value`, each preceded by a comment with the
  allowed values. Unknown keys and bad values are reported and keep their defaults; a file written by a newer
  version is read as far as possible. Migrations from older versions belong in `loadSettings()`.
- **Precedence**: defaults, then the file, then `--preset NAME` / `--set section.key=value` in command-line
  order, then `--gpu`. Sessions change settings with `set KEY VALUE`.
- **Presets** (`graphics.preset`): applying one sets its members; changing a member afterwards makes it
  `custom`, and setting the members back finds the preset again. Personal-taste effects (depth of field, motion
  blur, grain, vignette, aberration, focus) are not preset members, except that `original` turns them off.

| Preset | Render | AA and upscaler | Textures | Effects | Display transform |
|---|---|---|---|---|---|
| `original` | native | none, bilinear | bilinear, 1x AF | every effect `original` (PS2-faithful) | clamp, fixed exposure, no dither |
| `low` | performance (50 %) | FXAA + FSR 1, RCAS 0.3 | trilinear, 4x AF | low, no AO | AgX punchy, auto exposure |
| `medium` | quality (67 %) | FSR 2, sharpening 0.25 | trilinear, 8x AF | medium, AO low | AgX punchy, auto exposure |
| `high` (default) | native | TAA, RCAS 0.2 | trilinear, 16x AF | high, AO medium | AgX punchy, auto exposure |
| `ultra` | native | FSR 2 native AA, sharpening 0.15 | trilinear, 16x AF | ultra, AO high | AgX punchy, auto exposure |

- **Applied live**: everything except `display.high_dpi` (a window creation flag; restart). Changing the
  render size, MSAA, the AA path or FSR 2 recreates the affected targets (the GPU is idled once);
  `display.gpu` recreates the renderer on the new device; texture options recreate the material sampler;
  display options go through the display manager.

## Display

`platform/sdl/display.cpp`, `platform/sdl/renderer.cpp`.

- **Window modes** (`display.mode`): `windowed` (resizable, `window_width` x `window_height` points, centred on
  `display.monitor`), `borderless` (SDL's desktop fullscreen: a borderless window covering the monitor, no mode
  change, fast Alt+Tab) and `exclusive` (`fullscreen_width` x `fullscreen_height` at `refresh_rate`; 0 = the
  desktop size and the highest refresh rate available at that size). F11 / Alt+Enter toggle windowed and
  borderless. `--list-displays` prints the monitors and their modes.
- **Presentation** (`display.vsync`): `on` = FIFO, `off` = IMMEDIATE (falls back to MAILBOX, then FIFO),
  `adaptive` = FIFO_RELAXED, `mailbox` = MAILBOX, each only where the surface offers it (FIFO always exists).
  `display.fps_limit` paces presentation on the CPU (`SDL_DelayPrecise`) and never changes the 60 Hz tick.
  `display.output_bits = 10` picks an A2B10G10R10 swapchain where offered (less banding). Two frames are in
  flight.
- **High DPI**: SDL 3 declares per-monitor-v2 DPI awareness on Windows. With `display.high_dpi` the swapchain
  has the monitor's full pixel count and the 2D layer scales with the content rectangle.
- **HDR output** (`display.hdr`) is reserved. The pipeline is HDR internally; HDR10 output needs an
  `HDR10_ST2084` (or scRGB) swapchain, a PQ encode after a display-referred tonemap with a paper-white level,
  metadata, and a 2D layer composited at paper white. A later item.
- **Device selection**: every Vulkan device is examined: Vulkan 1.3 with dynamic rendering and
  synchronization2, `VK_KHR_push_descriptor`, a graphics+compute queue and, with a window, a queue that can
  present to the window's surface *and* successful surface capability, format and present-mode queries (a GPU
  cannot present to a display driven by another adapter: on the project's test machine the RX 7900 XTX returns
  `VK_ERROR_UNKNOWN` for the virtual display of a remote-play tool, the integrated GPU works). Devices that fail
  are listed with the reason, and among the rest a discrete GPU is preferred. `--gpu INDEX|NAME` or
  `display.gpu` forces one; if it cannot present, the error names the monitor the window is on. Headless runs
  need no surface and default to the discrete GPU. The chosen device, its driver name and version are logged.
- **Moving between monitors or adapters**: on a display change, an out-of-date or lost surface, or a failing
  capability query, the surface is recreated and checked again; if the GPU still cannot present, the renderer is
  rebuilt with a new device choice (with `display.gpu = auto`).
- **Implicit layers** (overlays of launchers, recorders, remote-play tools) are tolerated: the available layers
  are logged, and if instance creation fails it is retried with implicit layers disabled
  (`VK_LOADER_LAYERS_DISABLE=~implicit~`).
- Exclusive fullscreen changes the monitor's mode. On a virtual display of a remote-play tool, Windows restores
  the *registry* mode afterwards, which can differ from the resolution the streaming tool had set (and the tool
  may end its session); use borderless there.

## Pipeline as built

`platform/render/pipeline.cpp`. Compute shaders unless noted; push descriptors, synchronization2 and dynamic
rendering, Vulkan 1.3 core. Shaders are GLSL compiled to SPIR-V at build time by glslang (fetched at a pinned
commit) and embedded in the executable (`cmake/Host.cmake`).

```
scene (test scene / R1)   RGBA16F linear radiance + RG16F motion + D32 reverse Z at the render size, optional MSAA
 -> MSAA resolve          average of reversibly tonemapped samples (Karis), nearest depth and its motion vector
 -> exposure              256-bin log histogram (centre and lower-frame weighted), 50-95 % mean, eye adaptation
 -> temporal path         FSR 2.2.1 or TAA / TAAU (render size -> output size)
 -> HDR effects           depth of field, motion blur, bloom (at the "post" size: output if temporal, else render)
 -> tonemap               exposure, bloom composite, display transform, sRGB encode (perceptual values)
 -> spatial AA            FXAA 3.11 or SMAA 1x / T2x (only without a temporal path)
 -> spatial resample      FSR 1 EASU, bilinear, Catmull-Rom or Lanczos-2 up; Lanczos-2 down (supersampling)
 -> sharpening            AMD RCAS (FSR 2 sharpens with its own RCAS)
 -> output pass (raster)  into the content rectangle of the swapchain: chromatic aberration, vignette, film
                          grain, triangular dither; black bars around it
 -> 2D layer (raster)     HUD, menus, text at output resolution: never jittered, blurred or tonemapped
```

Why this order: temporal reconstruction needs the raw, jittered, linear HDR scene; depth of field and motion
blur work on scene-referred values with depth; bloom needs HDR energy; FXAA, SMAA, EASU and RCAS are designed for
perceptual (gamma-encoded) input; lens effects and dither belong to the display; the 2D layer comes last so it
stays sharp at any render scale. FSR 2 gets the exposure texture, so it works on the same exposed values as the
rest.

Formats: HDR targets RGBA16F; motion vectors RG16F in UV units; depth D32 reverse Z with an infinite far plane
(1 at the near plane, 0 at infinity: better precision, and what FSR 2 is configured for). Post-tonemap images
stay RGBA16F holding sRGB-encoded values, so nothing is quantised before the output pass. The swapchain is UNORM
(8 or 10 bits) and the output pass writes encoded values exactly; with an sRGB swapchain format it writes linear
values.

### Scene contract (what R1 provides)

`SceneSource` (`platform/render/scene.h`):

- `prepare(SceneFormats)`: create the pipelines for the formats and the MSAA sample count.
- `recordPrepass(command, SceneView)`: shadow maps, environment maps, uploads.
- `recordMain(command, SceneView)`: draw into the bound targets at `renderExtent`: colour 0 = linear HDR
  radiance (RGBA16F, scene-referred, below 32768, never NaN), colour 1 = motion vector, depth = reverse Z
  (`VK_COMPARE_OP_GREATER_OR_EQUAL`, cleared to 0).
- `camera()`: the unjittered view and projection, the previous unjittered view-projection, the near plane, the
  vertical field of view and a `cut` flag (the camera jumped: temporal history resets).
- `drawsOriginalEffects()`: true when the colour is the game's own displayed image (the GS frame), whose glare
  and fog are already drawn: effects set to `original` then add nothing in the pipeline.
- `SceneView` gives the scene its sizes, the content aspect ratio (for the Hor+ projection), the sub-pixel jitter
  in pixels and in clip units (pre-multiply the projection by a translation of `jitterClip`), the frame index and
  slot, and the **material sampler** every 3D texture must use (filter, anisotropy, and the LOD bias of the
  upscaler).

Motion vectors are `previous_uv - current_uv` of the *unjittered* positions, per pixel, in UV units, at the
render size. The PS2 game has none, so R1 must produce them:

1. The camera part: for each pixel, reproject its depth with the inverse current and the previous
   view-projection. Exact for static geometry (track, buildings, sky).
2. Moving objects (cars, wheels, driver, flags, crowd) need their previous transform: R1 keeps the previous
   frame's object matrices (the per-object inputs of the VU1 microprograms) and emits per-vertex previous clip
   positions, matching objects across frames by their draw identity.
3. Effects without stable geometry (particles, smoke, heat haze, glare sprites) go into FSR 2's reactive mask,
   surfaces whose shading moves differently from their geometry (reflections, scrolling textures) into its
   transparency/composition mask: R8 images at the render size returned by `SceneSource::reactiveMask()` and
   `transparencyMask()`. The 2D layer is drawn after the temporal pass and needs neither.

### Anti-aliasing

| Setting | Technique |
|---|---|
| `render.aa = fxaa` | FXAA 3.11 "quality" (Lottes 2011) on the tonemapped image; subpixel 0.75, edge threshold 0.166 |
| `render.aa = smaa` | SMAA 1x, the reference implementation (Jimenez et al. 2012, `third_party/smaa`): luma edges, ultra preset, neighbourhood blending in linear light |
| `render.aa = smaa_t2x` | SMAA T2x: two-frame ±0.25 px jitter with the matching subsample indices, velocity-weighted reprojected resolve |
| `render.aa = taa` | TAA / TAAU: Halton(2,3) jitter (more phases when upsampling); 3x3 Gaussian (Blackman-Harris-like) reconstruction of the jittered samples at output resolution; depth-dilated motion vectors (Karis 2014); 5-tap Catmull-Rom history (Jimenez 2016); variance clipping in YCoCg (Salvi 2016) towards the mean; reversible tonemap weighting against fireflies (Karis 2013); blend factor adapted to speed |
| `render.upscaler = fsr2` | AMD FSR 2.2.1: temporal upscaling and anti-aliasing in one; at `render.quality = native` it is FSR native AA |
| `render.msaa` = 2, 4, 8 | MSAA on the scene (clamped to the device) with the custom resolve; combines with every other mode |
| `render.scale` > 100 | Supersampling: up to 200 % per axis, Lanczos-2 low-pass downsample |

### Upscaling, sharpening and texture LOD

`render.quality` sets the per-axis ratio: native 1.0, ultra_quality 1.3, quality 1.5, balanced 1.7,
performance 2.0, ultra_performance 3.0 (AMD's FSR ratios), or `custom` = `render.scale` 50-200 %. The upscaler is
FSR 2 (temporal), TAAU (`render.aa = taa`), or a spatial one after the spatial AA: FSR 1 EASU (a GLSL port of
AMD's 32-bit path; the vendored FSR 2 copy of `ffx_fsr1.h` only compiles its EASU path as HLSL), bilinear,
bicubic (Catmull-Rom) or Lanczos-2 (deringed to the nearest 2x2 texels). `render.sharpness` drives AMD RCAS
(0..1 maps to 2..0 stops) after the spatial path, or FSR 2's own RCAS. With `texture.auto_lod_bias` the material
sampler's LOD bias is `log2(render width / output width)`, minus 1 more with FSR 2 (AMD's recommendation) or 0.5
with TAA, plus `texture.lod_bias`.

FSR 2 integration: the runtime and Vulkan backend are vendored unmodified (`third_party/README.md`); the
precompiled shader permutations of the SDK are replaced by our build-time GLSL compilation with this host's
options (HDR input, render-resolution unjittered motion vectors, inverted infinite depth), reflected at run time
by `platform/render/fsr2_shaders.cpp`, which also leaves out resources a pass declares but does not use. Two
storage-image format qualifiers of the SDK's GLSL are corrected at build time to the formats its runtime creates.

### Texture filtering

`texture.filter` point / bilinear (nearest mip) / trilinear and `texture.anisotropy` 1-16x (clamped to the device
limit), all through the one material sampler the scene uses for game textures. The 2D layer has its own filter
(`texture.ui_filter`; point keeps pixel art crisp).

### Effects and quality tiers

Every effect has `original` (what the PS2 game draws: the faithful baseline; with R1, the game's own GS output for
that effect), `off`, and `low` to `ultra` enhancements.

| Effect | original | low .. ultra |
|---|---|---|
| Shadows | blob shadows under the cars | 1024 px 2x2 PCF; 2048 px 3x3 PCF; 2048 px 16-tap rotated Poisson; 4096 px PCSS (Fernando 2005); texel-snapped, normal offset |
| Reflections | one sharp environment map | 64..512 px cube map, GGX-prefiltered (Karis 2013, with the PDF-based source mip of Krivanek & Colbert 2008), split-sum BRDF; clear-coat lobe on car paint |
| Bloom / glare | bright-pass glare over 3 levels, added | energy-conserving bloom: 13-tap downsample with a Karis average on the first level, 3x3 tent upsample (Jimenez 2014), 4..8 levels, mixed by `post.bloom_intensity` |
| Depth of field | (none) | thin-lens CoC from the field of view and `post.aperture`, autofocus or `post.focus_distance`; half-resolution scatter-as-gather bokeh (Gustafsson 2018) on a Vogel disc, 16/32/64/96 samples |
| Motion blur | (none) | reconstruction filter (McGuire et al. 2012) with tile max and neighbour max velocity, soft depth compare and jittered samples (Jimenez 2014), 8..24 samples, `post.shutter` |
| Sky and fog | linear distance fog | exponential height fog integrated along the ray (low), plus sun in-scattering (medium and up) |
| Heat haze, particles, ambient occlusion | schema only: applied by the R1 game renderer (AO is an enhancement, planned as GTAO) | |

Display transform (`post.tonemapper`): `original` (clamp, like the PS2 framebuffer), `reinhard` (extended, on
luminance), `aces` (Hill's RRT+ODT fit), `agx` and `agx_punchy` (Sobotka's AgX with Wrensch's polynomial fit;
punchy is the default look), `pbr_neutral` (Khronos PBR Neutral: accurate base colours, suits car paint).
`post.auto_exposure` with `post.exposure` compensation in EV; `post.film_grain` (zero-mean, signal-dependent),
`post.vignette` (cos^4 falloff), `post.chromatic_aberration` (lateral), `post.dither` (triangular PDF,
Gjoel 2016).

### Aspect ratio and the 2D layer

`aspect.ratio` (`window`, 4:3, 16:9, 16:10, 21:9, 32:9) defines the content rectangle; other window shapes get
black bars. 3D is Hor+: the vertical field of view is fixed and wider ratios see more to the sides. The 2D layer
is authored on a 640 x 480 virtual canvas and mapped by `aspect.hud`: `anchored` (each element keeps to its
edge), `centered` (a centred 4:3 area) or `stretched` (the 4:3 layout stretched, like the PS2 on a widescreen TV),
inset by `aspect.safe_area`. `docs/WIDESCREEN.md` is the plan for the game itself.

## Performance

GPU time per frame in milliseconds (timestamp queries around the whole frame: shadows, scene and every post pass)
on an AMD Radeon RX 7900 XTX (driver 26.10.1), headless, test scene, 400 frames per run:

| Configuration | 1920x1080 | 2560x1440 | 3840x2160 |
|---|---:|---:|---:|
| original preset | 0.19 | 0.28 | 0.55 |
| native, AA off | 0.29 | 0.42 | 0.79 |
| native, FXAA | 0.34 | 0.49 | 0.97 |
| native, SMAA | 0.38 | 0.56 | 1.14 |
| native, SMAA T2x | 0.40 | 0.61 | 1.29 |
| native, TAA (high preset) | 0.38 | 0.57 | 1.26 |
| native, MSAA 4x + TAA | 0.47 | 0.75 | 1.81 |
| FSR 2 native AA | 0.63 | 1.02 | 2.25 |
| FSR 2 quality | 0.48 | 0.75 | 1.59 |
| FSR 2 performance | 0.46 | 0.66 | 1.37 |
| FSR 1 performance + FXAA | 0.28 | 0.36 | 0.71 |
| TAAU balanced | 0.31 | 0.45 | 0.93 |
| SSAA 200 % | 1.66 | 2.91 | 6.45 |
| ultra preset | 0.68 | 1.09 | 2.38 |
| ultra + depth of field + motion blur (both ultra) | 1.72 | 3.09 | 7.41 |

The test scene is light (about 2,600 instances); the numbers show the pipeline's own cost. The integrated Radeon
renders the default preset at 1280x720 in about 8 ms.

## Limitations and next steps

- R1 replaces the test scene: motion vectors and the FSR 2 reactive mask for the game's geometry and effects
  (see the scene contract); its 2D GS primitives go through the 2D layer mapping (`docs/WIDESCREEN.md`).
- Heat haze, particles and ambient occlusion are settings without an implementation in the test scene.
- HDR10 output, FSR 2's auto-generated reactive mask, and FSR 3 / frame generation are not done.
- Window moves between adapters are handled but could not be exercised on the test machine (one active display).

## Settings reference

Generated from `gt4_host --list-options` (defaults are the `high` preset).

#### [display]

| Option | Default | Values | Effect |
|---|---|---|---|
| `display.mode` | `windowed` | windowed \| borderless \| exclusive | Window mode: a resizable window, borderless fullscreen at the desktop resolution, or exclusive fullscreen with its own resolution and refresh rate. |
| `display.gpu` | `auto` | auto \| <index> \| <part of the name> | Vulkan device. auto prefers a discrete GPU among the devices that can present to the window's display (headless: among all devices). --gpu overrides it; gt4_host --list-gpus lists the devices. |
| `display.monitor` | `0` | 0..15 | Display used for fullscreen and for placing the window (gt4_host --list-displays). |
| `display.window_width` | `1280` | 320..16384 | Windowed width in points. |
| `display.window_height` | `720` | 200..16384 | Windowed height in points. |
| `display.fullscreen_width` | `0` | 0..16384 | Exclusive fullscreen width in pixels; 0 keeps the desktop mode. |
| `display.fullscreen_height` | `0` | 0..16384 | Exclusive fullscreen height in pixels; 0 keeps the desktop mode. |
| `display.refresh_rate` | `0` | 0..1000 | Exclusive fullscreen refresh rate in Hz; 0 picks the highest rate for the resolution. |
| `display.vsync` | `on` | on \| off \| adaptive \| mailbox | Presentation: on = FIFO (no tearing), off = IMMEDIATE (tearing, lowest latency), adaptive = FIFO_RELAXED (tears only when late), mailbox = MAILBOX (no tearing, newest frame wins). Falls back to FIFO when the surface lacks the mode. |
| `display.fps_limit` | `0` | 0..1000 | Frame-rate cap in frames per second; 0 = no cap. The game simulation stays at its own 60 Hz. |
| `display.output_bits` | `8` | 8 \| 10 | Swapchain precision: 8 or 10 bits per channel (10 reduces banding where the display supports it). |
| `display.high_dpi` | `on` | on \| off | Render at the full pixel density of high-DPI displays (otherwise the OS scales the window). |
| `display.hdr` | `off` | on \| off | Reserved for HDR10 output (not implemented yet; ignored). |

#### [aspect]

| Option | Default | Values | Effect |
|---|---|---|---|
| `aspect.ratio` | `window` | window \| 4:3 \| 16:9 \| 16:10 \| 21:9 \| 32:9 | Content aspect ratio. Other window shapes get black bars (letterbox or pillarbox). 3D is Hor+: the vertical field of view stays fixed and wider ratios see more to the sides. |
| `aspect.hud` | `anchored` | anchored \| centered \| stretched | 2D layout: anchored = HUD elements keep to the edges of the content area, centered = 2D stays in a centred 4:3 area, stretched = the 4:3 layout is stretched (the PS2 on a widescreen TV). |
| `aspect.safe_area` | `0` | 0..0.15 | HUD inset from the content edges, as a fraction of the content height. |

#### [render]

| Option | Default | Values | Effect |
|---|---|---|---|
| `render.quality` | `native` | native \| ultra_quality \| quality \| balanced \| performance \| ultra_performance \| custom | Render resolution relative to the output: native 1.0x, ultra_quality 1.3x, quality 1.5x, balanced 1.7x, performance 2.0x, ultra_performance 3.0x smaller per axis; custom uses render.scale. |
| `render.scale` | `100` | 50..200 | Render scale in percent of the output resolution when render.quality = custom. Above 100 is supersampling (downsampled with a Lanczos filter). |
| `render.upscaler` | `fsr1` | bilinear \| bicubic \| lanczos \| fsr1 \| fsr2 | Upscaler from render to output resolution: bilinear, bicubic (Catmull-Rom), lanczos (2 lobes), fsr1 (AMD FSR 1 EASU, spatial) or fsr2 (AMD FSR 2, temporal; it also does the anti-aliasing and at quality = native it is FSR Native AA). |
| `render.sharpness` | `0.2` | 0..1 | Contrast-adaptive sharpening strength (AMD RCAS) after upscaling; 0 = off. |
| `render.aa` | `taa` | off \| fxaa \| smaa \| smaa_t2x \| taa | Post-process anti-aliasing: fxaa, smaa (SMAA 1x), smaa_t2x (SMAA with 2-frame temporal supersampling) or taa (temporal AA; it also upsamples when rendering below the output). Ignored when render.upscaler = fsr2. |
| `render.msaa` | `1` | 1 \| 2 \| 4 \| 8 | Multisample anti-aliasing samples for the 3D scene (clamped to the device). |

#### [texture]

| Option | Default | Values | Effect |
|---|---|---|---|
| `texture.filter` | `trilinear` | point \| bilinear \| trilinear | 3D texture filtering: point, bilinear (nearest mip) or trilinear. |
| `texture.anisotropy` | `16` | 1 \| 2 \| 4 \| 8 \| 16 | Anisotropic filtering level for 3D textures (1 = off). |
| `texture.lod_bias` | `0` | -3..3 | Extra texture LOD bias added to the automatic one (negative = sharper, more shimmer). |
| `texture.auto_lod_bias` | `on` | on \| off | Bias texture LOD by log2(render / output width) when upscaling, so textures keep output-resolution detail; a further -1 with FSR 2 (AMD's recommendation) and -0.5 with TAA. |
| `texture.ui_filter` | `bilinear` | point \| bilinear \| trilinear | Filtering of the 2D layer (HUD, menus, sprites): point keeps pixel art sharp. |

#### [effects]

| Option | Default | Values | Effect |
|---|---|---|---|
| `effects.shadows` | `high` | original \| off \| low \| medium \| high \| ultra | Shadow maps: low 1024 px 2x2 PCF, medium 2048 px 3x3 PCF, high 2048 px 16-tap Poisson, ultra 4096 px with contact-hardening (PCSS). |
| `effects.reflections` | `high` | original \| off \| low \| medium \| high \| ultra | Environment reflections on cars: original = one sharp environment map; low..ultra = cube map of 64..512 px prefiltered for roughness (GGX); off = a dimmed sharp map only. |
| `effects.bloom` | `high` | original \| off \| low \| medium \| high \| ultra | Bloom/glare: original = the PS2-style glare (bright pass over 3 levels, added); low..ultra = physically based bloom (energy-conserving mip chain, no threshold) over 4..8 levels. |
| `effects.dof` | `off` | original \| off \| low \| medium \| high \| ultra | Depth of field: thin-lens circle of confusion with a golden-angle bokeh gather, 16..96 samples. |
| `effects.motion_blur` | `off` | original \| off \| low \| medium \| high \| ultra | Motion blur: per-pixel reconstruction filter with tile max velocity (McGuire et al. 2012), 8..24 samples. |
| `effects.heat_haze` | `high` | original \| off \| low \| medium \| high \| ultra | Heat haze (screen-space refraction over hot air and exhausts). Schema only: applied by the R1 game renderer; the test scene has none. |
| `effects.particles` | `high` | original \| off \| low \| medium \| high \| ultra | Particles (smoke, sparks, dust): count and soft-particle depth fade. Schema only: applied by the R1 game renderer; the test scene has none. |
| `effects.sky_fog` | `high` | original \| off \| low \| medium \| high \| ultra | Sky and fog: original = linear distance fog (the GS fog of the PS2); low = exponential height fog; medium..ultra = height fog with sun in-scattering. |
| `effects.ambient_occlusion` | `medium` | original \| off \| low \| medium \| high \| ultra | Ambient occlusion (an enhancement; the PS2 had none), planned as ground-truth AO (GTAO) with 4..16 directions. Schema only for now: not rendered yet. |

#### [post]

| Option | Default | Values | Effect |
|---|---|---|---|
| `post.tonemapper` | `agx_punchy` | original \| reinhard \| aces \| agx \| agx_punchy \| pbr_neutral | HDR to display: original (clamp, as the PS2), reinhard (luminance), aces (Hill fit), agx and agx_punchy (Sobotka, Wrensch fit), pbr_neutral (Khronos PBR Neutral). |
| `post.exposure` | `0` | -6..6 | Exposure compensation in EV. |
| `post.auto_exposure` | `on` | on \| off | Automatic exposure from a luminance histogram, with smooth eye adaptation. |
| `post.bloom_intensity` | `0.05` | 0..1 | Bloom mix (fraction of energy scattered by the lens). |
| `post.film_grain` | `0` | 0..1 | Film grain strength (0 = off). |
| `post.vignette` | `0` | 0..1 | Vignette strength (0 = off). |
| `post.chromatic_aberration` | `0` | 0..1 | Lateral chromatic aberration strength (0 = off). |
| `post.dither` | `on` | on \| off | Triangular-noise dither before quantising to the display format (removes banding). |
| `post.shutter` | `0.5` | 0..1 | Motion blur exposure time as a fraction of the frame (0.5 = a 180-degree shutter). |
| `post.aperture` | `1.4` | 0.5..22 | Depth of field f-number. The focal length follows from the field of view on a 24 mm tall sensor, so wide views have little blur and narrow (replay, telephoto) views a lot; smaller numbers blur more. |
| `post.focus_distance` | `0` | 0..10000 | Depth of field focus distance in metres; 0 = autofocus on the centre of the screen. |

#### [graphics]

| Option | Default | Values | Effect |
|---|---|---|---|
| `graphics.preset` | `high` | original \| low \| medium \| high \| ultra \| custom | Applies a set of the options above: original (PS2-faithful), low, medium, high, ultra. Changing any member afterwards makes it custom. |

#### [scene]

| Option | Default | Values | Effect |
|---|---|---|---|
| `scene.animate` | `on` | on \| off | Test scene: run the animation. |
| `scene.fov` | `50` | 20..110 | Test scene: vertical field of view in degrees (Hor+). |

#### [debug]

| Option | Default | Values | Effect |
|---|---|---|---|
| `debug.overlay` | `on` | on \| off | On-screen settings readout. |
