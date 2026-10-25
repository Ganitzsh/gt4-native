#pragma once
// A procedural 3D test scene that exercises the display pipeline until the R1 renderer exists: a ring
// track with fine geometry (fences, gantries), textured surfaces seen at grazing angles, moving cars with
// clear-coat paint and lights, bright highlights, a large depth range and an analytic sky. It contains no
// game data. Its animation advances only through tick() (the 60 Hz simulation clock); rendering
// interpolates between ticks and never feeds back into it.
#include "scene.h"

#include <memory>

namespace gt4 {

class TestScene final : public SceneSource {
public:
    TestScene(const vk::Device& device, VkCommandPool pool);
    ~TestScene() override;
    TestScene(const TestScene&) = delete;
    TestScene& operator=(const TestScene&) = delete;

    // Simulation (game-owned clock): one fixed 1/60 s step.
    void tick();
    // Fraction of the way from the previous tick to the current one, for rendering between ticks.
    void setInterpolation(float alpha);
    void setControls(float stick, bool action);
    void apply(const Settings& settings);
    float speedKmh() const;
    int lap() const;

    void prepare(const SceneFormats& formats) override;
    void recordPrepass(VkCommandBuffer command, const SceneView& view) override;
    void recordMain(VkCommandBuffer command, const SceneView& view) override;
    SceneCamera camera() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
