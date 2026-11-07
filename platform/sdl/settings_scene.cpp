// The modern renderer's scene options (settings_table.h; docs/modern/ARCHITECTURE.md section 7).
#include "settings_table.h"

namespace gt4::settings_table {

void sceneOptions(std::vector<Option>& o) {
    // render.scene: only original until the modern renderer can draw a view (PLAN.md B7 parity, B8 hybrid, B15
    // modern); the other values are named and refused, so a file or --set naming one says why.
    Option scene = enumOption("render.scene", &Settings::scene, kSceneRenderers,
        "What draws the game's 3D scenes: original = the GS path, the PS2's picture (the only one until the modern "
        "renderer lands); hybrid = the modern renderer, graded by GT4's own post effects; modern = the modern renderer "
        "with its own lighting and post effects; parity = the modern renderer with GT4's lighting, for comparison "
        "(debugging, never in a preset).");
    const Setter set = scene.set;
    scene.allowed = "original";
    scene.set = [set](Settings& s, std::string_view text) -> std::string {
        const std::string value = lower(trim(text));
        if (value == "original") return set(s, value);
        if (value == "parity" || value == "hybrid" || value == "modern")
            return "'" + value + "' comes with the modern renderer (docs/modern/PLAN.md B7, B8, B15); expected original";
        return "expected original";
    };
    o.push_back(scene);
}

}  // namespace gt4::settings_table
