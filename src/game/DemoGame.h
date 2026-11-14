#pragma once

// Example game showing how the pieces fit together. docs/TUTORIAL.md builds this
// same game step by step (as MyGame). Replace it with your own: keep main.cpp,
// swap DemoGame for your Game subclass.

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"
#include "render3d/MeshRenderer.h"
#include "render3d/Model.h"
#include "render3d/ModelAnimator.h"

#include <memory>
#include <string>

class DemoGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;
    void onDebugUI(core::Application& app) override;

private:
    void updateCamera(core::Application& app, float dt);
    void updateGunner(core::Application& app, float dt);
    glm::mat4 gunnerTransform() const;

    std::unique_ptr<render3d::MeshRenderer> meshRenderer_;
    render3d::Camera3D camera_;
    render3d::SceneLighting lighting_;
    render3d::Mesh cube_;
    render3d::Mesh sphere_;
    render3d::Mesh ground_;
    gfx::Texture crateTexture_;
    gfx::Texture checkerTexture_;

    float cameraSpeedBoost_ = 1.0f; // eases between normal and fast movement

    // 2D overlay for the HUD, drawn on top of the 3D scene.
    std::unique_ptr<render2d::SpriteBatch> hud_;
    render2d::Camera2D hudCamera_;
    render2d::Font font_;
    bool showHelp_ = true;

    // Animation state, advanced in onFixedUpdate; prev* for interpolation.
    float spin_ = 0.0f;
    float prevSpin_ = 0.0f;
    float simTime_ = 0.0f;
    float prevSimTime_ = 0.0f;

    // 3D sound: a looping hum on the big crate, and chimes from the spheres.
    std::string pingSound_;
    audio::SoundHandle crateHum_;
    float pingTimer_ = 1.0f; // seconds until the next chime
    int nextPing_ = 0;       // which sphere chimes next

    // Settings changed in the debug UI.
    bool animateSun_ = true;
    float humVolume_ = 0.6f;

    int reverbPreset_ = 0;         // index into audio::ReverbPreset (0 = off), cycled with R
    bool lastChimeBlocked_ = false; // was the last chime behind the big crate?

    // A drone circling the scene with a buzzing motor: listen to its pitch as it passes.
    audio::SoundHandle droneSound_;
    float droneAngle_ = 0.0f;     // position on its circle (radians), advanced in onFixedUpdate
    float prevDroneAngle_ = 0.0f;
    float droneSpeed_ = 18.0f;    // units per second

    // The drone's 3D model (a glTF file), and its rotors' pose.
    render3d::Model droneModel_;
    render3d::ModelPose dronePose_; // this drone's own node transforms
    int rotors_[4] = {-1, -1, -1, -1}; // node indices: front left, front right, back left, back right
    float rotorAngle_ = 0.0f;

    // A gunner: a soldier model with animations made in Blender (Idle, Fire, Load), and a
    // rifle model attached to a socket in his right hand. He turns to follow the drone.
    render3d::Model gunner_;
    render3d::Model rifle_;
    render3d::ModelAnimator gunnerAnim_;
    int gunnerSocket_ = -1; // node in gunner_: where the rifle goes
    int gunnerTorso_ = -1;  // node in gunner_: tilted to aim up at the drone
    int rifleMuzzle_ = -1;  // node in rifle_: where the muzzle flash appears
    int ammo_ = 5;          // rounds left in the magazine
    float muzzleFlash_ = 0.0f; // seconds left to show the flash
    glm::vec3 droneNow_{0.0f}; // the drone's position this frame (the gunner aims at it)
    std::string shotSound_;
    std::string clickSound_;
};
