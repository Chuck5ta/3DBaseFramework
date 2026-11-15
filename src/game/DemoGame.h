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
#include "render3d/Raycast.h"
#include "render3d/ThirdPersonCamera.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class DemoGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onPreRender(core::Application& app, gfx::FrameContext& frame) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;
    void onDebugUI(core::Application& app) override;

private:
    void drawScene(float alpha); // the 3D scene: drawn twice, for the shadows and for the screen
    void updateMouseCapture(core::Application& app);
    void updateFlyCamera(core::Application& app, float dt);
    void updateFollowCamera(core::Application& app, float dt);
    void updatePlayer(core::Application& app, float dt);
    std::optional<float> raycastScene(const render3d::Ray& ray) const;
    glm::mat4 playerTransform() const;
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

    float cameraSpeedBoost_ = 1.0f; // eases between normal and fast movement (fly camera)

    // Two ways to view the scene, switched with C: flying freely, or following the
    // player from behind (third person).
    enum class CameraMode { Fly, ThirdPerson };
    CameraMode cameraMode_ = CameraMode::ThirdPerson;
    render3d::ThirdPersonCamera follow_;

    // The player: a second soldier, with his own animator, who walks and runs where you
    // steer him in third person. His model is skinned: one mesh that bends at the joints.
    glm::vec3 playerPos_{2.0f, 0.0f, 10.0f}; // his feet
    float playerYaw_ = glm::pi<float>();      // which way he faces (pi = along -Z)
    float playerSpeed_ = 0.0f;                // metres per second, eased
    render3d::ModelAnimator playerAnim_;
    render3d::Model soldier_;
    int soldierSocket_ = -1; // node in soldier_: where his rifle goes

    // A forest around the scene: one tree model, drawn many times with instancing (all the
    // copies in one draw call per part). One matrix per tree, made once in onInit.
    render3d::Model tree_;
    std::vector<glm::mat4> trees_;
    int treesShown_ = 0;         // how many to draw (the debug UI's slider)
    bool instanceTrees_ = true;  // false: one draw() per tree, to compare

    // 2D overlay for the HUD, drawn on top of the 3D scene.
    std::unique_ptr<render2d::SpriteBatch> hud_;
    render2d::Camera2D hudCamera_;
    render2d::Font font_;
    bool showHelp_ = true;

    // Animation state, advanced in onFixedUpdate; prev* for interpolation.
    float spin_ = 0.0f;
    float spinNow_ = 0.0f;    // spin_ interpolated for this frame (the camera's collisions use it)
    float prevSpin_ = 0.0f;
    float simTime_ = 0.0f;
    float simTimeNow_ = 0.0f; // simTime_ interpolated for this frame
    float prevSimTime_ = 0.0f;

    // 3D sound: a looping hum on the big crate, and chimes from the spheres.
    std::string pingSound_;
    audio::SoundHandle crateHum_;
    float pingTimer_ = 1.0f; // seconds until the next chime
    int nextPing_ = 0;       // which sphere chimes next

    // Settings changed in the debug UI.
    bool animateSun_ = false; // off: a turning sun makes shadow edges crawl
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
