# Lighting: ambient occlusion and the enhanced grade

Two options of the settings menu (GRAPHICS) relight the game's 3D scenes, neither of which the PS2 had:

- **Ambient occlusion** (`effects.ambient_occlusion`: off, low, medium, high, ultra; `original` is off):
  geometry that hides the sky darkens what it hides (under and around cars, wheel arches, tyre walls,
  kerbs, corners). Low to ultra take 4, 6, 8 or 12 directions and darken 50, 70, 85 or 100 %.
- **Lighting** (`effects.lighting`: original, enhanced): the scene graded as a modern renderer's filmic
  response would make it: more contrast around middle grey (in log space) with a soft shoulder that keeps
  highlights from clipping, vibrance (muted colours gain the most), cool shadows and warm highlights.

FSR and the other upscalers only resample the finished image: they add no lighting.

## Where it happens

GT4 draws a race's 3D scene into a 640-wide 32-bit target with its Z buffer, then its post effects (glare,
colour grading) read that target into others and compose it into the 24-bit display buffer, where the HUD
is drawn. The front end (`platform/gsnative/gs_frontend.cpp`, `sceneRead`) counts each target's
depth-tested perspective draws; when another target first reads one as a texture after 64 or more of them,
and it is at least 512 x 384 (not the environment and reflection maps), it records a Light command there,
with the Z buffer and the area the scene's draws covered. So the scene is complete, and nothing of the HUD
or of GT4's own post effects is in it yet. Menus with 3D (showrooms, the car at Home) get it the same way.

The back end (`platform/render/gsn_scene.cpp`, `relight`) runs two compute passes over that area at the
render scale and copies the result back into the target:

1. `gsn_ao.comp`: each pixel's view-space position from GT4's Z (its 3D renderers write Z = 1.6e6 / view
   distance in metres, measured on the race capture), its normal from the nearer neighbours, then per
   direction the highest occluder above the surface within 1.2 m (horizon-based), rotated per pixel.
   The field of view is GT4's monitor presets' (tan 0.433 up and down) times the display's aspect.
2. `gsn_light.comp`: the occlusion blurred over 4 x 4 pixels at about the same distance, applied in
   linear light, then the grade.

`GT4_LIGHT_VIEW=1` shows the occlusion instead of the scene, `2` the view distance in bands (red under
5 m, green under 20 m, blue under 80 m), `3` the Z buffer's log2.
