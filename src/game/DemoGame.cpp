#include "game/DemoGame.h"

#include "core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>

using namespace render3d;

namespace {

// A 256x256 checkerboard with thin grid lines, generated in code.
gfx::Texture makeCheckerTexture(gfx::VulkanContext& gpu)
{
    constexpr int N = 256, cell = 128;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool odd = ((x / cell) + (y / cell)) & 1;
            const bool line = (x % cell) < 2 || (y % cell) < 2;
            const uint8_t v = line ? 120 : (odd ? 200 : 170);
            uint8_t* p = &px[size_t((y * N + x) * 4)];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    gfx::TextureDesc desc; // defaults: sRGB, linear filtering, mipmaps, repeat
    desc.debugName = "checker";
    return gfx::Texture::fromPixels(gpu, N, N, px.data(), desc);
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// True if the straight line from `a` to `b` passes through a sphere: a cheap
// stand-in for "is the big crate in the way?" when deciding to muffle a sound.
bool lineHitsSphere(glm::vec3 a, glm::vec3 b, glm::vec3 centre, float radius)
{
    const glm::vec3 ab = b - a;
    const float t = std::clamp(glm::dot(centre - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    return glm::distance(a + ab * t, centre) < radius;
}

// The drone's flight path: a 12-unit circle around the scene, bobbing up and down.
// It passes just behind the camera's starting position.
constexpr float kDroneRadius = 12.0f;

glm::vec3 droneAt(float angle)
{
    return {std::cos(angle) * kDroneRadius, 4.0f + std::sin(angle * 2.0f) * 0.8f, std::sin(angle) * kDroneRadius};
}

// Where things stand. Drawing, the player's collisions and the camera's collisions all
// use these, so they always agree.
constexpr int kRing = 12;   // crates in the ring
constexpr int kSpheres = 6;

glm::mat4 bigCrateMatrix(float spin)
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    m = glm::rotate(m, spin, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    return glm::scale(m, glm::vec3(2.0f));
}

glm::mat4 ringCrateMatrix(int i, float spin)
{
    const float a = float(i) / kRing * glm::two_pi<float>();
    glm::mat4 c = glm::translate(glm::mat4(1.0f), {std::cos(a) * 7.0f, 0.5f, std::sin(a) * 7.0f});
    return glm::rotate(c, -a + spin * 0.25f, glm::vec3(0, 1, 0));
}

glm::vec3 sphereCentre(int i, float t)
{
    const float a = float(i) / kSpheres * glm::two_pi<float>() + 0.26f;
    return {std::cos(a) * 4.0f, 0.8f + std::sin(t * 2.0f + float(i)) * 0.3f, std::sin(a) * 4.0f};
}
constexpr float kSphereRadius = 0.6f; // the 0.5 sphere mesh, drawn 1.2x

// The player's speeds (metres per second), and the speeds the Walk and Run animations
// were made for: playing them at speed / that, the feet keep pace with the ground.
constexpr float kWalkSpeed = 1.5f, kRunSpeed = 4.0f;
constexpr float kWalkAnimSpeed = 1.35f, kRunAnimSpeed = 3.4f;
constexpr float kPlayerRadius = 0.35f;

// The forest: trees scattered between these distances from the centre.
constexpr int kTrees = 800;
constexpr float kForestInner = 22.0f, kForestOuter = 90.0f;

// A tiny random number generator (xorshift): the same forest every run, on every compiler
// (std::uniform_real_distribution may give different numbers on MSVC and GCC).
float random01(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state >> 8) / float(1u << 24); // 0 .. 1
}

// Boxes around a tree's trunk and crown, for the camera's ray tests, before the tree's own
// matrix places them (tree.glb: the trunk is 0.28 m wide and 1.4 m tall; the crown is
// up to 2.7 m across, from 0.9 m up to 4.2 m: the box is narrower, around its thick part).
const glm::mat4 kTrunkBox = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 0.7f, 0.0f}), {0.28f, 1.4f, 0.28f});
const glm::mat4 kCrownBox = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 2.4f, 0.0f}), {1.6f, 3.0f, 1.6f});

} // namespace

void DemoGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    meshRenderer_ = std::make_unique<MeshRenderer>(gpu, app.renderer());

    cube_ = Mesh(gpu, primitives::cube(1.0f), "cube");
    sphere_ = Mesh(gpu, primitives::sphere(0.5f, 48, 24), "sphere");
    ground_ = Mesh(gpu, primitives::plane(200.0f, 100.0f), "ground"); // 200 x 200, texture repeated 100x

    crateTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/crate.png"));
    gfx::TextureDesc normalMap;
    normalMap.srgb = false; // directions, not colours: no sRGB decoding
    normalMap.debugName = "crate normals";
    crateNormals_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/crate_normal.png"), normalMap);
    checkerTexture_ = makeCheckerTexture(gpu);

    // Clear to the fog colour, so distant geometry fades into the "sky".
    const glm::vec3 fog = lighting_.fogColor;
    app.renderer().setClearColor({srgbToLinear(fog.r), srgbToLinear(fog.g), srgbToLinear(fog.b), 1.0f});

    // 2D overlay: a SpriteBatch works inside the 3D pass too (it ignores depth).
    hud_ = std::make_unique<render2d::SpriteBatch>(gpu, app.renderer());
    font_ = render2d::Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf"));

    // Sounds: a hum that loops forever, placed on the big crate (spatial = 3D), and
    // a chime to play from the spheres. preload() decodes it now, avoiding a delay
    // the first time it plays.
    audio::PlayParams hum;
    hum.volume = 0.6f;
    hum.loop = true;
    hum.spatial = true;
    hum.position = {0.0f, 1.5f, 0.0f};
    hum.minDistance = 2.0f;  // full volume within 2 units
    hum.maxDistance = 60.0f;
    crateHum_ = app.audio().play(core::assetPath("sounds/hum_loop.wav"), hum);

    pingSound_ = core::assetPath("sounds/ping.wav");
    app.audio().preload(pingSound_);

    // The drone's motor: a 3D sound that follows it (see onUpdate). Its Doppler is
    // doubled: realistic Doppler at these speeds is subtle, and games usually
    // exaggerate it a little.
    audio::PlayParams drone;
    drone.volume = 0.7f;
    drone.loop = true;
    drone.spatial = true;
    drone.position = droneAt(droneAngle_);
    drone.minDistance = 3.0f;
    drone.maxDistance = 80.0f;
    drone.doppler = 2.0f;
    droneSound_ = app.audio().play(core::assetPath("sounds/drone_loop.wav"), drone);

    // The drone's model: a glTF file, the format Blender exports. Its parts are nodes;
    // look up the rotors by name once, so they can be spun every frame.
    droneModel_ = render3d::Model::load(app.gpu(), core::assetPath("models/drone.glb"));
    dronePose_ = droneModel_.defaultPose();
    const char* rotorNames[4] = {"RotorFL", "RotorFR", "RotorBL", "RotorBR"};
    for (int i = 0; i < 4; ++i)
        rotors_[i] = droneModel_.findNode(rotorNames[i]);

    // The gunner and his rifle: two models. The rifle is drawn at the gunner's
    // "Socket_Hand_R" node, so it follows his animations.
    gunner_ = render3d::Model::load(app.gpu(), core::assetPath("models/gunner.glb"));
    rifle_ = render3d::Model::load(app.gpu(), core::assetPath("models/rifle.glb"));
    gunnerSocket_ = gunner_.findNode("Socket_Hand_R");
    gunnerTorso_ = gunner_.findNode("Torso");
    rifleMuzzle_ = rifle_.findNode("Muzzle");
    gunnerAnim_.play(gunner_, "Idle"); // loops until something else is played
    shotSound_ = core::assetPath("sounds/rifle_shot.wav");
    clickSound_ = core::assetPath("sounds/mag_click.wav");
    app.audio().preload(shotSound_);
    app.audio().preload(clickSound_);

    // The player: the soldier again, skinned (soldier.glb: the same bones and animations,
    // one mesh that bends), with his own animator (and so his own pose).
    soldier_ = render3d::Model::load(app.gpu(), core::assetPath("models/soldier.glb"));
    soldierSocket_ = soldier_.findNode("Socket_Hand_R");
    playerAnim_.play(soldier_, "Idle");

    // The forest: one model, and a matrix for each copy, each turned and sized a little
    // differently so they don't look stamped out.
    tree_ = render3d::Model::load(app.gpu(), core::assetPath("models/tree.glb"));
    uint32_t seed = 12345;
    for (int i = 0; i < kTrees; ++i) {
        const float angle = random01(seed) * glm::two_pi<float>();
        // Picking the squared distance spreads them evenly over the ring's area, rather than
        // crowding them at its inner edge.
        const float r = std::sqrt(glm::mix(kForestInner * kForestInner, kForestOuter * kForestOuter, random01(seed)));
        const glm::vec3 position(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        const float size = glm::mix(0.8f, 1.3f, random01(seed));
        const float height = glm::mix(0.9f, 1.25f, random01(seed));
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, random01(seed) * glm::two_pi<float>(), glm::vec3(0, 1, 0));
        trees_.push_back(glm::scale(m, glm::vec3(size, size * height, size)));
    }
    treesShown_ = kTrees;

    // Start behind him, looking where he looks (along -Z).
    follow_.yaw = -glm::half_pi<float>();
    follow_.snapTo(playerPos_);
    follow_.apply(camera_);
}

void DemoGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
    meshRenderer_.reset();
    hud_.reset();
}

void DemoGame::onFixedUpdate(core::Application& app, float dt)
{
    // Animation advances at a fixed rate, independent of the frame rate.
    prevSpin_ = spin_;
    prevSimTime_ = simTime_;
    spin_ += dt * 0.6f; // radians per second
    simTime_ += dt;
    prevDroneAngle_ = droneAngle_;
    droneAngle_ += droneSpeed_ / kDroneRadius * dt; // angle = distance / radius

    // Every 2 seconds the next sphere chimes from where it stands, each on its own
    // note of a pentatonic scale (pitch = playback rate).
    pingTimer_ -= dt;
    if (pingTimer_ <= 0.0f) {
        pingTimer_ += 2.0f;
        const float notes[6] = {1.0f, 1.125f, 1.25f, 1.5f, 1.6875f, 2.0f};
        const int i = nextPing_;
        nextPing_ = (nextPing_ + 1) % kSpheres;
        audio::PlayParams ping;
        ping.volume = 0.8f;
        ping.pitch = notes[i];
        ping.spatial = true;
        ping.position = sphereCentre(i, simTime_);
        // Behind the big crate (seen from the camera)? Then muffle it, as a real
        // obstacle would: walls let the low frequencies through best.
        lastChimeBlocked_ = lineHitsSphere(camera_.position, ping.position, {0.0f, 1.5f, 0.0f}, 1.3f);
        if (lastChimeBlocked_)
            ping.lowPassHz = 1200.0f;
        app.audio().play(pingSound_, ping);
    }
}

void DemoGame::onUpdate(core::Application& app, float dt)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE)) {
        if (in.relativeMouseMode())
            in.setRelativeMouseMode(app.window().handle(), false); // first Esc releases the mouse
        else
            app.quit();
    }
    if (in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
    if (in.keyPressed(SDL_SCANCODE_V))
        app.renderer().setVsync(!app.renderer().vsync());
    if (in.keyPressed(SDL_SCANCODE_F1) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_NORTH))
        meshRenderer_->setWireframe(!meshRenderer_->wireframe());
    if (in.keyPressed(SDL_SCANCODE_G))
        lighting_.shadows.enabled = !lighting_.shadows.enabled;
    if (in.keyPressed(SDL_SCANCODE_H))
        showHelp_ = !showHelp_;
    if (in.keyPressed(SDL_SCANCODE_M))
        app.audio().setMuted(!app.audio().muted());
    if (in.keyPressed(SDL_SCANCODE_R)) { // Off -> Room -> Hall -> Cave -> Off
        reverbPreset_ = (reverbPreset_ + 1) % 4;
        app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset(reverbPreset_)));
    }

    // C (or Start) switches between flying and following the player.
    if (in.keyPressed(SDL_SCANCODE_C) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_START)) {
        cameraMode_ = cameraMode_ == CameraMode::Fly ? CameraMode::ThirdPerson : CameraMode::Fly;
        if (cameraMode_ == CameraMode::ThirdPerson)
            follow_.snapTo(playerPos_); // straight back behind him, wherever we flew to
    }

    const float alpha = app.interpolationAlpha();
    spinNow_ = glm::mix(prevSpin_, spin_, alpha);
    simTimeNow_ = glm::mix(prevSimTime_, simTime_, alpha);

    updateMouseCapture(app);
    updatePlayer(app, dt);
    if (cameraMode_ == CameraMode::Fly)
        updateFlyCamera(app, dt);
    else
        updateFollowCamera(app, dt); // after the player moved, so it follows him this frame
    // The player hears the world from the camera.
    app.audio().setListener(camera_.position, camera_.forward(), camera_.up());
    // Move the drone's sound with the drone. The audio engine works out its
    // velocity from these positions, and from that the Doppler pitch.
    const float droneAngle = glm::mix(prevDroneAngle_, droneAngle_, app.interpolationAlpha());
    app.audio().setPosition(droneSound_, droneAt(droneAngle));

    // Spin the rotors: purely visual, so once per frame is enough. They turn faster
    // when the drone flies faster.
    rotorAngle_ = std::fmod(rotorAngle_ + (30.0f + droneSpeed_) * dt, glm::two_pi<float>());

    droneNow_ = droneAt(droneAngle);
    updateGunner(app, dt);

    // Slowly move the sun around the scene (unless switched off in the debug UI).
    if (animateSun_) {
        const float a = float(app.time()) * 0.1f;
        lighting_.sunDirection = glm::normalize(glm::vec3(std::cos(a), -0.8f, std::sin(a)));
    }
}

// Where the gunner stands, turned to face the drone (his model faces +Z).
glm::mat4 DemoGame::gunnerTransform() const
{
    const glm::vec3 position(-2.5f, 0.0f, 5.5f);
    const glm::vec3 to = droneNow_ - position;
    const float yaw = std::atan2(to.x, to.z);
    return glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0));
}

void DemoGame::updateGunner(core::Application& app, float dt)
{
    constexpr int kMagazine = 5;
    core::Input& in = app.input();
    const bool loading = gunnerAnim_.isPlaying("Load") && !gunnerAnim_.finished();

    // F (or the gamepad's B) fires; an empty rifle reloads instead. L (or X) reloads.
    if (!loading && (in.keyPressed(SDL_SCANCODE_F) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_EAST))) {
        if (ammo_ > 0) {
            --ammo_;
            gunnerAnim_.play(gunner_, "Fire", render3d::AnimationLoop::Once, true); // restart: rapid fire
        } else {
            gunnerAnim_.play(gunner_, "Load", render3d::AnimationLoop::Once);
        }
    }
    if (!loading && ammo_ < kMagazine && (in.keyPressed(SDL_SCANCODE_L) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_WEST)))
        gunnerAnim_.play(gunner_, "Load", render3d::AnimationLoop::Once);

    gunnerAnim_.update(dt);
    muzzleFlash_ = std::max(0.0f, muzzleFlash_ - dt); // before a new shot, so a flash shows for at least a frame

    // Moments in the animations: the shot at the start of Fire, and the magazine coming
    // out (0.3 s) and going in (1.0 s) during Load.
    const glm::mat4 rifleAt = gunner_.nodeMatrix(gunnerSocket_, gunnerTransform(), &gunnerAnim_.pose());
    const glm::vec3 riflePos(rifleAt[3]);
    audio::PlayParams sound;
    sound.spatial = true;
    sound.position = riflePos;
    sound.minDistance = 3.0f;
    if (gunnerAnim_.isPlaying("Fire") && gunnerAnim_.reached(0.0f)) {
        muzzleFlash_ = 0.06f;
        app.audio().play(shotSound_, sound);
    }
    if (gunnerAnim_.isPlaying("Load") && (gunnerAnim_.reached(0.3f) || gunnerAnim_.reached(1.0f))) {
        sound.volume = 0.6f;
        app.audio().play(clickSound_, sound);
    }

    // When a Fire or Load finishes, go back to Idle (or reload an empty rifle).
    if (gunnerAnim_.finished()) {
        if (gunnerAnim_.isPlaying("Load"))
            ammo_ = kMagazine;
        if (gunnerAnim_.isPlaying("Fire") && ammo_ == 0)
            gunnerAnim_.play(gunner_, "Load", render3d::AnimationLoop::Once);
        else
            gunnerAnim_.play(gunner_, "Idle");
    }

    // Aim up at the drone: tilt the torso back on top of whatever the animation does.
    // (The pose is rebuilt by every update(), so this doesn't build up.)
    const glm::vec3 chest = glm::vec3(gunnerTransform()[3]) + glm::vec3(0.0f, 1.4f, 0.0f);
    const glm::vec3 to = droneNow_ - chest;
    const float pitch = std::clamp(std::atan2(to.y, glm::length(glm::vec2(to.x, to.z))), 0.0f, glm::radians(35.0f));
    render3d::NodeTransform& torso = gunnerAnim_.pose()[gunnerTorso_];
    torso.rotation = glm::angleAxis(-pitch, glm::vec3(1, 0, 0)) * torso.rotation;
}

// Hold the right mouse button (or press Tab) to capture the mouse for looking around.
void DemoGame::updateMouseCapture(core::Application& app)
{
    core::Input& in = app.input();
    if (in.keyPressed(SDL_SCANCODE_TAB))
        in.setRelativeMouseMode(app.window().handle(), !in.relativeMouseMode());
    if (in.mousePressed(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), true);
    if (in.mouseReleased(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), false);
}

// The player soldier, its feet at playerPos_, turned to face playerYaw_ (his model faces +Z).
glm::mat4 DemoGame::playerTransform() const
{
    return glm::rotate(glm::translate(glm::mat4(1.0f), playerPos_), playerYaw_, glm::vec3(0, 1, 0));
}

void DemoGame::updatePlayer(core::Application& app, float dt)
{
    core::Input& in = app.input();

    // Which way to go, relative to the camera: "forward" walks away from it. Only in
    // third person; in fly mode the same keys move the camera and he stands still.
    glm::vec2 input(0.0f); // x = right, y = forward
    bool run = false;
    if (cameraMode_ == CameraMode::ThirdPerson) {
        if (in.keyDown(SDL_SCANCODE_W)) input.y += 1.0f;
        if (in.keyDown(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (in.keyDown(SDL_SCANCODE_D)) input.x += 1.0f;
        if (in.keyDown(SDL_SCANCODE_A)) input.x -= 1.0f;
        input += glm::vec2(in.leftStick().x, -in.leftStick().y); // stick up is negative
        if (glm::length(input) > 1.0f)
            input = glm::normalize(input);
        run = in.keyDown(SDL_SCANCODE_LSHIFT) || in.gamepadDown(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    }
    const float amount = glm::length(input); // 0..1: a stick pushed half way walks slowly
    const glm::vec3 wanted = follow_.flatForward() * input.y + follow_.flatRight() * input.x;

    // Turn smoothly towards that direction, the short way round.
    if (amount > 0.05f) {
        const float target = std::atan2(wanted.x, wanted.z);
        const float turn = std::remainder(target - playerYaw_, glm::two_pi<float>());
        playerYaw_ += turn * (1.0f - std::exp(-12.0f * dt));
    }

    // Speed up and slow down smoothly, and move the way he faces (so he walks in an arc
    // while turning, rather than sliding sideways).
    const float targetSpeed = amount * (run ? kRunSpeed : kWalkSpeed);
    playerSpeed_ += (targetSpeed - playerSpeed_) * (1.0f - std::exp(-8.0f * dt));
    playerPos_ += glm::vec3(std::sin(playerYaw_), 0.0f, std::cos(playerYaw_)) * playerSpeed_ * dt;

    // Keep him out of the crates, the spheres and the gunner: push him back out of a
    // circle around each (enough for a flat world; a physics engine does this in general).
    auto pushOut = [this](glm::vec2 centre, float radius) {
        const glm::vec2 p(playerPos_.x, playerPos_.z);
        const glm::vec2 away = p - centre;
        const float d = glm::length(away);
        const float r = radius + kPlayerRadius;
        if (d < r && d > 1e-4f) {
            const glm::vec2 out = centre + away / d * r;
            playerPos_.x = out.x;
            playerPos_.z = out.y;
        }
    };
    pushOut({0.0f, 0.0f}, 1.6f); // the big crate, at any angle
    for (int i = 0; i < kRing; ++i) {
        const glm::vec3 c(ringCrateMatrix(i, spinNow_)[3]);
        pushOut({c.x, c.z}, 0.65f);
    }
    for (int i = 0; i < kSpheres; ++i) {
        const glm::vec3 c = sphereCentre(i, simTimeNow_);
        pushOut({c.x, c.z}, kSphereRadius);
    }
    pushOut({-2.5f, 5.5f}, 0.4f); // the gunner
    for (const glm::mat4& tree : trees_) { // the trunks (their x scale is the tree's size)
        const glm::vec3 c(tree[3]);
        pushOut({c.x, c.z}, 0.14f * glm::length(glm::vec3(tree[0])));
    }

    // The animation follows the speed: Idle, Walk or Run, played faster or slower so
    // his feet keep pace with the ground.
    if (playerSpeed_ < 0.2f) {
        playerAnim_.play(soldier_, "Idle");
        playerAnim_.speed = 1.0f;
    } else if (playerSpeed_ < (kWalkSpeed + kRunSpeed) * 0.5f) {
        playerAnim_.play(soldier_, "Walk");
        playerAnim_.speed = playerSpeed_ / kWalkAnimSpeed;
    } else {
        playerAnim_.play(soldier_, "Run");
        playerAnim_.speed = playerSpeed_ / kRunAnimSpeed;
    }
    playerAnim_.update(dt);
}

// What the third-person camera can't see through: the ground, the crates, the spheres and
// the gunner.
// Returns the distance to the nearest one along the ray, if any.
std::optional<float> DemoGame::raycastScene(const render3d::Ray& ray) const
{
    std::optional<float> nearest;
    auto consider = [&nearest](std::optional<float> hit) {
        if (hit && (!nearest || *hit < *nearest))
            nearest = hit;
    };
    consider(raycastPlane(ray, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    consider(raycastBox(ray, bigCrateMatrix(spinNow_))); // the same matrices they're drawn with
    for (int i = 0; i < kRing; ++i)
        consider(raycastBox(ray, ringCrateMatrix(i, spinNow_)));
    for (int i = 0; i < kSpheres; ++i)
        consider(raycastSphere(ray, sphereCentre(i, simTimeNow_), kSphereRadius));
    // The gunner, as a box around him (simple shapes are usually enough for a camera).
    consider(raycastBox(ray, glm::scale(glm::translate(gunnerTransform(), {0.0f, 0.9f, 0.0f}), {0.6f, 1.8f, 0.6f})));
    // Trees: a box for the trunk and one for the crown, but only for those near the ray's
    // start (the camera never looks far past the player).
    for (const glm::mat4& tree : trees_) {
        if (glm::distance(glm::vec3(tree[3]), ray.origin) < 15.0f) {
            consider(raycastBox(ray, tree * kTrunkBox));
            consider(raycastBox(ray, tree * kCrownBox));
        }
    }
    return nearest;
}

void DemoGame::updateFollowCamera(core::Application& app, float dt)
{
    core::Input& in = app.input();

    // Orbit with the captured mouse or the right stick; zoom with the wheel or the d-pad.
    if (in.relativeMouseMode())
        follow_.orbit(in.mouseDelta().x * 0.0025f, -in.mouseDelta().y * 0.0025f);
    const glm::vec2 look = in.rightStick();
    follow_.orbit(look.x * 2.5f * dt, -look.y * 2.0f * dt);
    follow_.zoom(in.mouseWheel());
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_UP)) follow_.zoom(4.0f * dt);
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) follow_.zoom(-4.0f * dt);

    follow_.update(playerPos_, dt, [this](const render3d::Ray& ray) { return raycastScene(ray); });
    follow_.apply(camera_);
}

void DemoGame::updateFlyCamera(core::Application& app, float dt)
{
    core::Input& in = app.input();

    // --- Look: with the captured mouse (see updateMouseCapture) or the right stick ---
    if (in.relativeMouseMode()) {
        const float sensitivity = 0.0025f; // radians per pixel
        camera_.yaw += in.mouseDelta().x * sensitivity;
        camera_.pitch -= in.mouseDelta().y * sensitivity;
    }
    const glm::vec2 look = in.rightStick();
    camera_.yaw += look.x * 2.5f * dt;
    camera_.pitch -= look.y * 2.0f * dt;
    camera_.pitch = std::clamp(camera_.pitch, -1.55f, 1.55f); // just short of straight up/down

    // --- Move: WASD + left stick; up/down on Space/E, Ctrl/Q or the triggers ---
    glm::vec3 move(0.0f); // x = right, y = up, z = forward
    if (in.keyDown(SDL_SCANCODE_W)) move.z += 1.0f;
    if (in.keyDown(SDL_SCANCODE_S)) move.z -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_D)) move.x += 1.0f;
    if (in.keyDown(SDL_SCANCODE_A)) move.x -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_SPACE) || in.keyDown(SDL_SCANCODE_E)) move.y += 1.0f;
    if (in.keyDown(SDL_SCANCODE_LCTRL) || in.keyDown(SDL_SCANCODE_Q)) move.y -= 1.0f;
    const glm::vec2 stick = in.leftStick();
    move.x += stick.x;
    move.z -= stick.y; // stick up is negative
    move.y += in.rightTrigger() - in.leftTrigger();
    if (glm::length(move) > 1.0f)
        move = glm::normalize(move);

    // Shift / clicking the left stick: ease into 4x speed.
    const bool fast = in.keyDown(SDL_SCANCODE_LSHIFT) || in.gamepadDown(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    const float targetBoost = fast ? 4.0f : 1.0f;
    cameraSpeedBoost_ += (targetBoost - cameraSpeedBoost_) * (1.0f - std::exp(-8.0f * dt));
    const float speed = 6.0f * cameraSpeedBoost_; // units per second

    // Forward/back moves on the horizontal plane, so looking down doesn't dive.
    glm::vec3 flatForward = camera_.forward();
    flatForward.y = 0.0f;
    if (glm::length(flatForward) > 1e-4f)
        flatForward = glm::normalize(flatForward);
    camera_.position += (flatForward * move.z + camera_.right() * move.x + glm::vec3(0, 1, 0) * move.y) *
                        speed * dt;
    camera_.position.y = std::max(camera_.position.y, 0.2f); // stay above the ground
}

void DemoGame::onDebugUI(core::Application& app)
{
    // Our own window, at the top right the first time it appears, sized to fit
    // its contents (but no taller than the screen). The framework's "Engine"
    // window is drawn separately.
    const float scale = ImGui::GetStyle().FontScaleDpi; // matches the screen's DPI setting
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(screen.x - 12.0f * scale, 60.0f * scale), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(screen.x, screen.y - 100.0f * scale));
    if (!ImGui::Begin("Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { // collapsed: skip the contents
        ImGui::End();
        return;
    }

    ImGui::Text("Draw calls: %u (+%u for shadows)", meshRenderer_->drawCalls(), meshRenderer_->shadowDrawCalls());
    ImGui::Text("Triangles: %u (+%u for shadows)", meshRenderer_->triangles(), meshRenderer_->shadowTriangles());
    ImGui::Text("Joint matrices: %u of %u", meshRenderer_->jointsUsed(), meshRenderer_->maxJoints());
    ImGui::Text("Instance matrices: %u of %u", meshRenderer_->instancesUsed(), meshRenderer_->maxInstances());
    bool wireframe = meshRenderer_->wireframe();
    if (ImGui::Checkbox("Wireframe", &wireframe))
        meshRenderer_->setWireframe(wireframe);
    ImGui::SameLine();
    bool normalMaps = meshRenderer_->normalMaps();
    if (ImGui::Checkbox("Normal maps", &normalMaps)) // off: the crates go flat
        meshRenderer_->setNormalMaps(normalMaps);

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        int mode = int(cameraMode_);
        if (ImGui::Combo("Mode", &mode, "Fly\0Third person\0")) {
            cameraMode_ = CameraMode(mode);
            if (cameraMode_ == CameraMode::ThirdPerson)
                follow_.snapTo(playerPos_);
        }
    }
    if (cameraMode_ == CameraMode::ThirdPerson && ImGui::CollapsingHeader("Third person", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Distance", &follow_.distance, follow_.minDistance, follow_.maxDistance, "%.1f m");
        ImGui::SliderFloat("Shoulder", &follow_.shoulder, -1.0f, 1.0f, "%.2f m");
        ImGui::SliderFloat("Look-at height", &follow_.targetOffset.y, 0.0f, 2.5f, "%.2f m");
        ImGui::SliderAngle("Pitch", &follow_.pitch, -72.0f, 34.0f);
        ImGui::SliderFloat("Follow sharpness", &follow_.followSharpness, 0.0f, 30.0f, "%.0f");
        ImGui::SliderFloat("Return sharpness", &follow_.returnSharpness, 0.5f, 20.0f, "%.1f");
        ImGui::SliderFloat("Collision radius", &follow_.collisionRadius, 0.0f, 0.6f, "%.2f m");
        ImGui::SliderAngle("Field of view", &camera_.fovY, 20.0f, 120.0f);
        ImGui::Text("Distance now: %.2f m%s", double(follow_.currentDistance()), follow_.blocked() ? " (blocked)" : "");
        ImGui::Text("Player: %s at %.1f m/s", playerAnim_.animationName().c_str(), double(playerSpeed_));
    }
    if (cameraMode_ == CameraMode::Fly && ImGui::CollapsingHeader("Fly camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &camera_.position.x, 0.1f, -100.0f, 100.0f, "%.1f");
        ImGui::SliderAngle("Yaw", &camera_.yaw, -360.0f, 360.0f);
        ImGui::SliderAngle("Pitch", &camera_.pitch, -89.0f, 89.0f);
        ImGui::SliderAngle("Field of view", &camera_.fovY, 20.0f, 120.0f);
        if (ImGui::Button("Look at the crate"))
            camera_.lookAt({0.0f, 1.5f, 0.0f});
    }

    if (ImGui::CollapsingHeader("Forest")) {
        ImGui::SliderInt("Trees", &treesShown_, 0, int(trees_.size()));
        ImGui::Checkbox("Instancing", &instanceTrees_); // off: watch the draw calls go up
    }

    if (ImGui::CollapsingHeader("Culling")) {
        bool culling = meshRenderer_->frustumCulling();
        if (ImGui::Checkbox("Frustum culling", &culling)) // off: watch the triangles go up
            meshRenderer_->setFrustumCulling(culling);
        bool frozen = meshRenderer_->cullingFrozen();
        if (ImGui::Checkbox("Freeze", &frozen)) // then fly out and look back
            meshRenderer_->setCullingFrozen(frozen);
        ImGui::Text("Culled: %u (+%u for shadows)", meshRenderer_->culled(), meshRenderer_->shadowCulled());
    }
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::Checkbox("Animate sun", &animateSun_);
        if (ImGui::SliderFloat3("Sun direction", &lighting_.sunDirection.x, -1.0f, 1.0f)) {
            animateSun_ = false; // dragging takes over from the animation
            if (glm::length(lighting_.sunDirection) < 1e-3f)
                lighting_.sunDirection = {0.0f, -1.0f, 0.0f};
            lighting_.sunDirection = glm::normalize(lighting_.sunDirection);
        }
        ImGui::ColorEdit3("Sun colour", &lighting_.sunColor.x);
        ImGui::SliderFloat("Sun intensity", &lighting_.sunIntensity, 0.0f, 6.0f);
        ImGui::ColorEdit3("Sky colour", &lighting_.skyColor.x);
        ImGui::ColorEdit3("Ground colour", &lighting_.groundColor.x);
        ImGui::SliderFloat("Ambient", &lighting_.ambientIntensity, 0.0f, 2.0f);
        // The clear colour must follow the fog colour, as set up in onInit.
        if (ImGui::ColorEdit3("Fog colour", &lighting_.fogColor.x)) {
            const glm::vec3 fog = lighting_.fogColor;
            app.renderer().setClearColor({srgbToLinear(fog.r), srgbToLinear(fog.g), srgbToLinear(fog.b), 1.0f});
        }
        ImGui::SliderFloat("Fog density", &lighting_.fogDensity, 0.0f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic);
    }

    if (ImGui::CollapsingHeader("Shadows")) {
        render3d::ShadowSettings& sh = lighting_.shadows;
        ImGui::Checkbox("Enabled", &sh.enabled);
        ImGui::SliderFloat("Distance", &sh.distance, 5.0f, 150.0f, "%.0f m");
        ImGui::SliderFloat("Strength", &sh.strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Softness", &sh.softness, 0.0f, 4.0f, "%.1f texels");
        ImGui::SliderFloat("Normal bias", &sh.normalBias, 0.0f, 5.0f, "%.1f texels");
        ImGui::SliderFloat("Depth bias", &sh.depthBias, 0.0f, 5.0f, "%.1f texels");
        ImGui::Text("Map %u x %u, one texel = %.1f cm", meshRenderer_->shadowMapSize(), meshRenderer_->shadowMapSize(),
                    double(meshRenderer_->shadowTexelSize() * 100.0f));
    }

    if (ImGui::CollapsingHeader("Sound")) {
        if (ImGui::SliderFloat("Crate hum", &humVolume_, 0.0f, 1.0f))
            app.audio().setVolume(crateHum_, humVolume_);
        if (ImGui::Button("Chime now"))
            pingTimer_ = 0.0f; // onFixedUpdate plays it on the next step
        ImGui::Text("Last chime: %s", lastChimeBlocked_ ? "muffled (behind the crate)" : "clear");
        const char* presets[] = {"Off", "Room", "Hall", "Cave"};
        if (ImGui::Combo("Reverb", &reverbPreset_, presets, 4))
            app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset(reverbPreset_)));
        ImGui::SeparatorText("Doppler");
        ImGui::SliderFloat("Drone speed", &droneSpeed_, 0.0f, 40.0f, "%.0f units/s");
        float factor = app.audio().dopplerFactor();
        if (ImGui::SliderFloat("Doppler factor", &factor, 0.0f, 4.0f, "%.1f"))
            app.audio().setDopplerFactor(factor);
        const float pitch = app.audio().dopplerPitch(droneSound_);
        ImGui::Text("Drone pitch: %+.1f%%", double((pitch - 1.0f) * 100.0f));
    }

    // Our own textures, shown inside the UI.
    if (ImGui::CollapsingHeader("Textures")) {
        debugui::DebugUI& ui = *app.debugUI(); // never null inside onDebugUI
        const ImVec2 size(96.0f * scale, 96.0f * scale);
        ImGui::Image(ui.texture(crateTexture_), size);
        ImGui::SameLine();
        ImGui::Image(ui.texture(crateNormals_), size);
        ImGui::SameLine();
        ImGui::Image(ui.texture(checkerTexture_), size);
    }
    ImGui::End();
}

// Everything in the 3D scene. Drawn twice a frame: first from the sun, into the shadow
// map (onPreRender), then from the camera, onto the screen (onRender).
void DemoGame::drawScene(float alpha)
{
    // The fixed-step animation state, interpolated for smooth motion (in onUpdate).
    const float spin = spinNow_;
    const float t = simTimeNow_;

    // Ground: checker texture with a green tint. Flat on the ground, it has nothing to
    // cast a shadow on, so it skips the shadow pass.
    render3d::Material ground{&checkerTexture_, {0.55f, 0.62f, 0.5f, 1.0f}};
    ground.castShadows = false;
    meshRenderer_->draw(ground_, glm::mat4(1.0f), ground);

    // Centrepiece: a big crate, and a ring of crates around it, all twelve in one draw call
    // (instancing: the same mesh and material, a matrix for each copy). The normal map
    // gives the frame, brace and planks their bevels and grooves.
    render3d::Material crate{&crateTexture_};
    crate.normalTexture = &crateNormals_;
    meshRenderer_->draw(cube_, bigCrateMatrix(spin), crate);
    glm::mat4 ring[kRing];
    for (int i = 0; i < kRing; ++i)
        ring[i] = ringCrateMatrix(i, spin);
    meshRenderer_->drawInstanced(cube_, ring, crate);

    // The forest: hundreds of trees, one draw call for the trunks and one for the crowns.
    const std::span<const glm::mat4> trees(trees_.data(), size_t(treesShown_));
    if (instanceTrees_) {
        meshRenderer_->drawInstanced(tree_, trees);
    } else {
        for (const glm::mat4& tree : trees)
            meshRenderer_->draw(tree_, tree);
    }

    // Bobbing coloured spheres
    const glm::vec4 palette[] = {
        {0.95f, 0.30f, 0.30f, 1.0f}, {0.30f, 0.80f, 0.40f, 1.0f}, {0.30f, 0.50f, 0.95f, 1.0f},
        {0.95f, 0.80f, 0.25f, 1.0f}, {0.80f, 0.40f, 0.90f, 1.0f}, {0.95f, 0.95f, 0.95f, 1.0f},
    };
    for (int i = 0; i < kSpheres; ++i) {
        glm::mat4 s = glm::translate(glm::mat4(1.0f), sphereCentre(i, t));
        s = glm::scale(s, glm::vec3(kSphereRadius * 2.0f));
        meshRenderer_->draw(sphere_, s, {nullptr, palette[i]});
    }

    // The drone: its glTF model, facing along its path and banking into the turn.
    const float droneAngle = glm::mix(prevDroneAngle_, droneAngle_, alpha);
    glm::mat4 d = glm::translate(glm::mat4(1.0f), droneAt(droneAngle));
    d = glm::rotate(d, -droneAngle, glm::vec3(0, 1, 0));            // the model's front (+Z) along the path
    d = glm::rotate(d, droneSpeed_ * 0.012f, glm::vec3(0, 0, 1));  // lean into the turn
    for (int i = 0; i < 4; ++i) {
        // Diagonal pairs spin the same way, so the drone doesn't twist round.
        const float direction = (i == 0 || i == 3) ? 1.0f : -1.0f;
        dronePose_[rotors_[i]].rotation = glm::angleAxis(rotorAngle_ * direction, glm::vec3(0, 1, 0));
    }
    meshRenderer_->draw(droneModel_, d, &dronePose_);

    // The gunner, in the pose his animator worked out, and the rifle at his hand's socket.
    const glm::mat4 g = gunnerTransform();
    meshRenderer_->draw(gunner_, g, &gunnerAnim_.pose());
    const glm::mat4 rifleAt = gunner_.nodeMatrix(gunnerSocket_, g, &gunnerAnim_.pose());
    meshRenderer_->draw(rifle_, rifleAt);
    if (muzzleFlash_ > 0.0f) { // a glowing blob at the rifle's "Muzzle" node
        const glm::mat4 muzzle = rifle_.nodeMatrix(rifleMuzzle_, rifleAt);
        render3d::Material flash{nullptr, {1.0f, 0.75f, 0.3f, 1.0f}};
        flash.emissive = {1.0f, 0.7f, 0.25f};
        flash.emissiveStrength = 3.0f;
        flash.castShadows = false; // a flash of light casts no shadow
        meshRenderer_->draw(sphere_, glm::scale(glm::translate(muzzle, {0.0f, 0.0f, 0.06f}), glm::vec3(0.12f, 0.12f, 0.22f)),
                            flash);
    }

    // The player: the skinned soldier and the rifle, in his own pose.
    const glm::mat4 p = playerTransform();
    meshRenderer_->draw(soldier_, p, &playerAnim_.pose());
    meshRenderer_->draw(rifle_, soldier_.nodeMatrix(soldierSocket_, p, &playerAnim_.pose()));
}

// Before the main pass: the shadow pass. The same scene, drawn from the sun.
void DemoGame::onPreRender(core::Application& app, gfx::FrameContext& frame)
{
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height)); // (the shadows' fit uses it)
    meshRenderer_->beginShadows(frame, camera_, lighting_);
    drawScene(app.interpolationAlpha());
    meshRenderer_->endShadows();
}

void DemoGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    meshRenderer_->begin(frame, camera_, lighting_);
    drawScene(app.interpolationAlpha());
    meshRenderer_->end();

    // ===== HUD: 2D text and shapes on top, with a camera where 1 unit = 1 pixel =====
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f;
    hud_->begin(frame, hudCamera_.viewProjection());

    const float ui = std::max(1.0f, viewport.y / 720.0f);
    render2d::TextStyle text;
    text.size = 16.0f * ui;
    text.snapToPixel = true;

    // Top left: performance and camera info, with a soft shadow for readability.
    char info[256];
    std::snprintf(info, sizeof(info), "%.0f fps   %u draw calls (+%u shadow)\ncamera  %s   %.1f, %.1f, %.1f\ngunner  %s   ammo %d",
                  double(app.fps()), meshRenderer_->drawCalls(), meshRenderer_->shadowDrawCalls(),
                  cameraMode_ == CameraMode::Fly ? "fly" : (follow_.blocked() ? "third person (blocked)" : "third person"),
                  double(camera_.position.x), double(camera_.position.y), double(camera_.position.z),
                  gunnerAnim_.animationName().c_str(), ammo_);
    render2d::TextStyle shadow = text;
    shadow.color = {0.0f, 0.0f, 0.0f, 0.5f};
    hud_->drawText(font_, info, glm::vec2(17.0f, 13.0f) * ui, shadow);
    hud_->drawText(font_, info, glm::vec2(16.0f, 12.0f) * ui, text);

    // Top right: wireframe, mute, reverb and shadow indicators.
    render2d::TextStyle tag = text;
    tag.align = render2d::TextAlign::Right;
    tag.color = render2d::colors::Yellow;
    if (meshRenderer_->wireframe())
        hud_->drawText(font_, "WIREFRAME", {viewport.x - 16.0f * ui, 12.0f * ui}, tag);
    if (app.audio().muted())
        hud_->drawText(font_, "MUTED", {viewport.x - 16.0f * ui, 34.0f * ui}, tag);
    const char* reverbNames[] = {"", "REVERB: ROOM", "REVERB: HALL", "REVERB: CAVE"};
    if (reverbPreset_ > 0)
        hud_->drawText(font_, reverbNames[reverbPreset_], {viewport.x - 16.0f * ui, 56.0f * ui}, tag);
    if (!lighting_.shadows.enabled)
        hud_->drawText(font_, "NO SHADOWS", {viewport.x - 16.0f * ui, 78.0f * ui}, tag);

    // Bottom: a help panel, sized to fit its text.
    if (showHelp_) {
        const char* help = cameraMode_ == CameraMode::Fly
            ? "FLY CAMERA      C / (Start): follow the soldier\n"
              "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down      Shift: fast\n"
              "F / (B): gunner fires      L / (X): gunner reloads      F1 / (Y): wireframe\n"
              "M: mute      R: reverb      V: vsync      G: shadows      H: hide help      ` (below Esc): debug UI"
            : "THIRD PERSON      C / (Start): fly camera\n"
              "WASD / left stick: walk      Shift / (L3): run      Right mouse / Tab / right stick: look around\n"
              "Mouse wheel / d-pad: zoom      F / (B): gunner fires      L / (X): gunner reloads      F1 / (Y): wireframe\n"
              "M: mute      R: reverb      V: vsync      G: shadows      H: hide help      ` (below Esc): debug UI";
        render2d::TextStyle style = text;
        style.align = render2d::TextAlign::Center;
        const glm::vec2 size = font_.measure(help, style.size);
        const glm::vec2 top(viewport.x * 0.5f, viewport.y - size.y - 24.0f * ui);
        const glm::vec2 margin(14.0f * ui, 8.0f * ui);
        hud_->drawRect({top.x - size.x * 0.5f - margin.x, top.y - margin.y}, size + margin * 2.0f,
                       {0.0f, 0.0f, 0.0f, 0.5f});
        hud_->drawText(font_, help, top, style);
    }

    hud_->end();
}
