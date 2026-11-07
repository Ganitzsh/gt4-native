// The render resolution, upscaling, anti-aliasing and textures: render.*, texture.* (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

void renderOptions(std::vector<Option>& o) {
    o.push_back(enumOption("render.gs", &Settings::gsRenderer, kGsRenderers,
        "The resolution the game's graphics are drawn at: original = the PS2's own (640 x 448, one pixel per "
        "PS2 pixel; `emulated` is accepted for it), native = the render resolution."));
    o.push_back(enumOption("render.quality", &Settings::quality, kQuality,
        "Render resolution relative to the output: native 1.0x, ultra_quality 1.3x, quality 1.5x, balanced "
        "1.7x, performance 2.0x, ultra_performance 3.0x smaller per axis; custom uses render.scale."));
    o.push_back(intOption("render.scale", &Settings::renderScale, 50, 200,
        "Render scale in percent of the output resolution when render.quality = custom. Above 100 is "
        "supersampling (downsampled with a Lanczos filter)."));
    o.push_back(enumOption("render.upscaler", &Settings::upscaler, kUpscalers,
        "Upscaler from render to output resolution: bilinear, bicubic (Catmull-Rom), lanczos (2 lobes), "
        "fsr1 (AMD FSR 1 EASU, spatial) or fsr2 (AMD FSR 2, temporal; it also does the anti-aliasing and at "
        "quality = native it is FSR Native AA)."));
    o.push_back(floatOption("render.sharpness", &Settings::sharpness, 0.0f, 1.0f,
        "Contrast-adaptive sharpening strength (AMD RCAS) after upscaling; 0 = off."));
    o.push_back(enumOption("render.aa", &Settings::aa, kAa,
        "Post-process anti-aliasing: fxaa, smaa (SMAA 1x), smaa_t2x (SMAA with 2-frame temporal "
        "supersampling) or taa (temporal AA; it also upsamples when rendering below the output). Ignored "
        "when render.upscaler = fsr2."));
    o.push_back(intOption("render.msaa", &Settings::msaa, 1, 8,
        "Multisample anti-aliasing samples for the 3D scene (clamped to the device).", {1, 2, 4, 8}));
}

void textureOptions(std::vector<Option>& o) {
    o.push_back(enumOption("texture.filter", &Settings::textureFilter, kFilters,
        "3D texture filtering: point, bilinear (nearest mip) or trilinear."));
    o.push_back(intOption("texture.anisotropy", &Settings::anisotropy, 1, 16,
        "Anisotropic filtering level for 3D textures (1 = off).", {1, 2, 4, 8, 16}));
    o.push_back(floatOption("texture.lod_bias", &Settings::lodBias, -3.0f, 3.0f,
        "Extra texture LOD bias added to the automatic one (negative = sharper, more shimmer)."));
    o.push_back(boolOption("texture.auto_lod_bias", &Settings::autoLodBias,
        "Bias texture LOD by log2(render / output width) when upscaling, so textures keep output-resolution "
        "detail; a further -1 with FSR 2 (AMD's recommendation) and -0.5 with TAA."));
    o.push_back(enumOption("texture.ui_filter", &Settings::uiFilter, kFilters,
        "Filtering of the 2D layer (HUD, menus, sprites): point keeps pixel art sharp."));
    o.push_back(boolOption("texture.hd_pack", &Settings::hdTextures,
        "Replace the game's textures with a texture pack's (PCSX2 format, SCUS-97328) when one is installed."));
    o.push_back(stringOption("texture.pack_path", &Settings::texturePack, "auto | <folder>",
        "The texture pack's folder (its PNG files, in any subfolders). auto: textures/SCUS-97328 next to the "
        "host, in the settings folder or in the local application data folder (gt4-native), then PCSX2's "
        "own textures folder."));
}

}  // namespace gt4::settings_table
