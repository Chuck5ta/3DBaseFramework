#pragma once

// Example game showing how the pieces fit together. Replace it with your own:
// keep main.cpp, swap DemoGame for your Game subclass.

#include "core/Application.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"
#include "render3d/MeshRenderer.h"

#include <memory>

class DemoGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;

private:
    void updateCamera(core::Application& app, float dt);

    std::unique_ptr<render3d::MeshRenderer> meshRenderer_;
    render3d::Camera3D camera_;
    render3d::SceneLighting lighting_;

    render3d::Mesh cube_;
    render3d::Mesh sphere_;
    render3d::Mesh ground_;
    gfx::Texture crateTexture_;
    gfx::Texture checkerTexture_;

    // Simulation state (fixed step); prev* for render interpolation.
    float spin_ = 0.0f;
    float prevSpin_ = 0.0f;
    float simTime_ = 0.0f;
    float prevSimTime_ = 0.0f;
    float cameraSpeedBoost_ = 1.0f;
};
