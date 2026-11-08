// The modern renderer's scene options (settings_table.h; docs/modern/ARCHITECTURE.md section 7).
#include "settings_table.h"

namespace gt4::settings_table {

void sceneOptions(std::vector<Option>& o) {
    // render.scene: original by default; parity (B7) and hybrid (B8) are checks in their first form, accepted but never in a
    // preset and never saved; modern (B15) is named and refused, so a file or --set naming it says why.
    Option scene = enumOption("render.scene", &Settings::scene, kSceneRenderers,
        "What draws the game's 3D scenes: original = the GS path, the PS2's picture; parity = a check, not a mode: the GS "
        "path with the 3D drawn from the mesh cache with GT4's lighting and depth, which should look the same; hybrid = the "
        "same, by the modern renderer's stages and with its side channels for modern lighting (in its first form it looks "
        "like parity); modern = the modern renderer with its own lighting and post effects. Parity and hybrid are for "
        "debugging, never in a preset, never saved.");
    const Setter set = scene.set;
    scene.allowed = "original | parity | hybrid";
    scene.set = [set](Settings& s, std::string_view text) -> std::string {
        const std::string value = lower(trim(text));
        if (value == "original" || value == "parity" || value == "hybrid") return set(s, value);
        if (value == "modern") return "'modern' comes with the modern renderer (docs/modern/PLAN.md B15); expected original, parity or hybrid";
        return "expected original, parity or hybrid";
    };
    o.push_back(scene);
}

}  // namespace gt4::settings_table

namespace gt4 {

// The game sends scene records (kOptionSceneRecords) when a capture wants them (GT4_HOST_RECORD_SCENE=1, CONTRACTS.md
// a.1 and docs/modern/CAPTURES.md), a modern renderer draws the scene, or the night relight needs the lamps
// (lighting.night = enhanced, PLAN.md F2: the game sends its lights only as scene records). Read here only, once.
bool sceneRecordsWanted(const Settings& settings) {
    static const bool record = [] {
        const char* e = std::getenv("GT4_HOST_RECORD_SCENE");
        return e && e[0] == '1' && e[1] == '\0';
    }();
    return record || settings.scene != SceneRenderer::Original || settings.night != LightingMode::Original;
}

// The scene split beside the records it needs, so the two switches can't drift apart (settings.h).
bool sceneSplitWanted(const Settings& settings) {
    const bool modern = settings.scene != SceneRenderer::Original;   // (parity, B7, hybrid, modern: views drawn from B6's ids)
    return sceneRecordsWanted(settings) && (modern || settings.night == LightingMode::Enhanced);
}

}  // namespace gt4
