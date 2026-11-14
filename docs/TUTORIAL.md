# Game3D — Tutorial: Making the Demo

This tutorial rebuilds the demo scene from an empty window, one feature at a time. It uses a
new class, `MyGame`, so the original `DemoGame` stays available for comparison. After
every step you can build and run the game and see the result.

**What you'll learn:** the `Game` hooks, meshes and the `MeshRenderer`, materials and
textures, lighting and fog, a fly camera driven by mouse, keyboard and gamepad,
animation on the fixed timestep with interpolation, a 2D text HUD on top of the 3D scene,
3D sound with Doppler, audio effects, a debug UI, 3D models from Blender (glTF), animated,
and a third-person camera that follows a walking character and keeps out of walls.

**Before you start:** the project builds and runs (see [BUILDING.md](BUILDING.md)). For
background on any class used here, see [GAME3D_ARCHITECTURE.md](GAME3D_ARCHITECTURE.md).

| Step | You add | You see |
|---|---|---|
| [1](#step-1--an-empty-game) | An empty game with a depth buffer | A cleared window; Esc quits |
| [2](#step-2--a-lit-cube) | `MeshRenderer`, a cube mesh, a camera | A lit orange cube |
| [3](#step-3--textures-ground-and-fog) | Textures, a ground plane, fog colour | A crate standing on a checkered plain |
| [4](#step-4--a-fly-camera) | Mouse/keyboard/gamepad camera | Fly around the scene |
| [5](#step-5--many-objects-and-animation) | Spheres, a crate ring, fixed-step animation | The full demo scene, moving |
| [6](#step-6--a-moving-sun-wireframe-and-vsync) | Moving sun, wireframe, vsync toggles | Shifting light; debug views |
| [7](#step-7--a-text-hud) | A 2D overlay with a font | Stats, wireframe tag and a help panel |
| [8](#step-8--3d-sound) | Positional audio with `app.audio()` | A humming crate and chiming spheres; **M** mutes |
| [9](#step-9--a-debug-ui) | A Dear ImGui window with `onDebugUI` | Live camera, lighting and fog controls |
| [10](#step-10--reverb-and-muffled-sounds) | Audio effects: reverb presets, a per-sound low-pass | **R** changes the room; chimes behind the crate are muffled |
| [11](#step-11--a-passing-drone-doppler) | A moving sound and the Doppler effect | A buzzing drone that rises in pitch as it approaches and drops as it passes |
| [12](#step-12--a-drone-model-gltf) | A glTF model with moving parts | The drone becomes a proper model, rotors spinning, leaning into its turn |
| [13](#step-13--an-animated-gunner) | Animations from Blender, sockets | A soldier who follows the drone, fires (**F**) and reloads (**L**) |
| [14](#step-14--a-third-person-camera) | `ThirdPersonCamera`, ray tests, a player character | Walk and run a second soldier; the camera follows him and won't go through crates; **C** switches cameras |

> Every code listing in this tutorial was compiled (with warnings as errors) and run with
> Vulkan validation enabled, step by step. If something doesn't work, compare with
> `src/game/DemoGame.cpp`: the finished `MyGame` does exactly what `DemoGame` does; the
> code differs only in small details such as line order.

---

## Step 1 — An empty game

A game is a class derived from `core::Game`. `core::Application` owns the window, input
and GPU, runs the main loop and calls your class's hooks (`onInit`, `onUpdate`,
`onRender`, …). Every hook is optional; this first version only uses `onUpdate` to quit.

> **Adding files in Visual Studio.** You never need to edit CMake for new source files:
> anything under `src/` is picked up automatically on the next build. Create the files from
> **File Explorer**, or in Solution Explorer switch to **Folder View** (the *Switch between
> solutions and available views* button) and use *Add → New File* on `src/game`.
> **Avoid *Add → New Item* in the *CMake Targets View*:** Visual Studio then tries to add
> the file to a CMake target itself, and can edit the wrong CMake script (for example
> `cmake/Dependencies.cmake`), which breaks the configure step.

Create **`src/game/MyGame.h`**:

```cpp
#pragma once

#include "core/Application.h"

class MyGame : public core::Game {
public:
    void onUpdate(core::Application& app, float dt) override;
};
```

Create **`src/game/MyGame.cpp`**:

```cpp
#include "game/MyGame.h"

void MyGame::onUpdate(core::Application& app, float /*dt*/)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
}
```

Now point **`src/main.cpp`** at the new class. Replace its contents with:

```cpp
#include "core/Application.h"
#include "game/MyGame.h"

#include <SDL3/SDL_main.h>

int main(int, char**)
{
    core::AppConfig config;
    config.window.title = "MyGame";
    config.renderer.depthBuffer = true; // 3D: the depth buffer decides what's in front

    core::Application app(config);
    MyGame game;
    return app.run(game);
}
```

`depthBuffer = true` makes the renderer create a depth buffer, clear it every frame and
attach it to the main pass. Without it, whatever is drawn last would cover everything else.

**Build and run.** CMake picks up new files in `src/` automatically. You get a dark window,
with the frame rate in the title bar. **Esc** quits; **F11** toggles fullscreen (handled
by `Application`). On a gamepad (and the Steam Deck), the *View/Back* button quits.

---

## Step 2 — A lit cube

Drawing in 3D takes four pieces:

| Piece | Role |
|---|---|
| `render3d::Mesh` | Geometry on the GPU (vertex + index buffers). `primitives::cube/plane/sphere` generate common shapes. |
| `render3d::MeshRenderer` | The shaders and pipeline; draws meshes with lighting and fog. |
| `render3d::Camera3D` | Where we look from: position, yaw/pitch, field of view. |
| `render3d::SceneLighting` | The sun, ambient light and fog. The defaults give a pleasant daylight look. |

GPU resources are created in `onInit` (the GPU is ready by then) and released in
`onShutdown` (the GPU is idle by then).

Update **`MyGame.h`**:

```cpp
#pragma once

#include "core/Application.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"
#include "render3d/MeshRenderer.h"

#include <memory>

class MyGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;

private:
    std::unique_ptr<render3d::MeshRenderer> meshRenderer_;
    render3d::Camera3D camera_;
    render3d::SceneLighting lighting_;
    render3d::Mesh cube_;
};
```

Update **`MyGame.cpp`** with the includes and the new hooks:

```cpp
#include "game/MyGame.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace render3d;

void MyGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    meshRenderer_ = std::make_unique<MeshRenderer>(gpu, app.renderer());

    cube_ = Mesh(gpu, primitives::cube(1.0f), "cube");

    camera_.position = {0.0f, 2.0f, 5.0f};
    camera_.lookAt({0.0f, 0.5f, 0.0f});
}

void MyGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
    meshRenderer_.reset();
}

void MyGame::onUpdate(core::Application& app, float /*dt*/)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
}

void MyGame::onRender(core::Application&, gfx::FrameContext& frame)
{
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height));

    meshRenderer_->begin(frame, camera_, lighting_);

    // An orange cube, slightly rotated so three faces are visible.
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 0.0f});
    model = glm::rotate(model, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    meshRenderer_->draw(cube_, model, {nullptr, {1.0f, 0.55f, 0.2f, 1.0f}});

    meshRenderer_->end();
}
```

Things to notice:

- **The model matrix** places an object in the world. Build it as
  translate → rotate → scale (read right to left: the mesh is scaled, then rotated, then
  moved). GLM's `translate`/`rotate`/`scale` each multiply onto the matrix you pass in.
- **The material** `{nullptr, colour}` means "no texture, just this colour". The colour is
  sRGB (as in an image editor); the renderer converts it to linear for lighting.
- **The world** is right-handed with **+Y up**; the camera looks down **−Z** by default.
  `lookAt` points it at a target.
- **`setAspect`** must match the window shape, so it's set every frame from
  `frame.extent`. Resize the window and the cube keeps its proportions.

**Run it:** an orange cube, lit from above by the sun, its sides shaded by the sky and
ground ambient light.

---

## Step 3 — Textures, ground and fog

Now give the scene a ground and the cube a texture:

- **`gfx::Texture::fromFile`** loads `assets/textures/crate.png` (PNG/JPG/TGA/BMP).
  Textures default to sRGB colour, linear filtering, mipmaps and anisotropic filtering:
  right for most 3D surfaces.
- **`gfx::Texture::fromPixels`** builds a texture from bytes in memory; here, a
  procedural checkerboard.
- **The ground** is a 200×200 plane whose UVs repeat the texture 100 times (texture
  wrapping is on by default).
- **Fog.** `MeshRenderer` fades distant geometry towards `lighting_.fogColor`. Clearing
  the screen to the same colour makes the horizon blend seamlessly into the "sky".

In **`MyGame.h`**, add the new members:

```cpp
    render3d::Mesh ground_;
    gfx::Texture crateTexture_;
    gfx::Texture checkerTexture_;
```

In **`MyGame.cpp`**, update the includes:

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

using namespace render3d;
```

Add the helpers after the includes:

```cpp
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

} // namespace
```

Replace `onInit`:

```cpp
void MyGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    meshRenderer_ = std::make_unique<MeshRenderer>(gpu, app.renderer());

    cube_ = Mesh(gpu, primitives::cube(1.0f), "cube");
    ground_ = Mesh(gpu, primitives::plane(200.0f, 100.0f), "ground"); // 200 x 200, texture repeated 100x

    crateTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/crate.png"));
    checkerTexture_ = makeCheckerTexture(gpu);

    // Clear to the fog colour, so distant geometry fades into the "sky".
    const glm::vec3 fog = lighting_.fogColor;
    app.renderer().setClearColor({srgbToLinear(fog.r), srgbToLinear(fog.g), srgbToLinear(fog.b), 1.0f});

    camera_.position = {0.0f, 3.0f, 10.0f};
    camera_.lookAt({0.0f, 1.0f, 0.0f});
}
```

And in `onRender`, replace the orange cube with the ground and a big crate:

```cpp
    // Ground: checker texture with a green tint.
    meshRenderer_->draw(ground_, glm::mat4(1.0f), {&checkerTexture_, {0.55f, 0.62f, 0.5f, 1.0f}});

    // Centrepiece: a big crate.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    m = glm::rotate(m, 0.6f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    m = glm::scale(m, glm::vec3(2.0f));
    meshRenderer_->draw(cube_, m, {&crateTexture_});
```

Things to notice:

- `{&checkerTexture_, colour}` multiplies the texture by a tint, so one grey texture can be
  any colour.
- The clear colour must be **linear**, but `SceneLighting` colours are sRGB, hence
  `srgbToLinear` when copying the fog colour to the renderer.
- The checkerboard stays crisp at a distance and at grazing angles thanks to the mipmaps and
  anisotropic filtering, and the fog hides the far edge of the plane.

**Run it:** a big tilted crate standing on a green checkered plain that fades into a hazy
blue sky.

---

## Step 4 — A fly camera

Let's move around. The camera is updated in `onUpdate`, once per rendered frame, so it
responds as quickly as the display allows.

- **Looking** changes `yaw` (left/right) and `pitch` (up/down). The mouse gives a
  *delta* in pixels per frame while **relative mouse mode** is on (cursor hidden and
  captured): hold the right button, or press **Tab** to toggle. The right stick gives a rate,
  so it is multiplied by `dt`.
- **Moving** combines `forward()`/`right()` with keyboard, stick and trigger input. Forward
  is flattened onto the ground plane, so looking down and pressing W doesn't dive into the
  floor.
- **Pitch is clamped** just short of ±90°: looking exactly straight up or down makes the
  view direction parallel to "up", and the camera would flip.

In **`MyGame.h`**, add a private helper and the speed boost state:

```cpp
    void updateCamera(core::Application& app, float dt);

    float cameraSpeedBoost_ = 1.0f; // eases between normal and fast movement
```

In **`MyGame.cpp`**, update the includes:

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <vector>
#include <algorithm>

using namespace render3d;
```

Replace `onUpdate` (it now uses `dt`, handles Esc with a captured mouse, and calls the
camera code):

```cpp
void MyGame::onUpdate(core::Application& app, float dt)
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

    updateCamera(app, dt);
}
```

Add the camera function:

```cpp
void MyGame::updateCamera(core::Application& app, float dt)
{
    core::Input& in = app.input();

    // --- Look: hold the right mouse button (or press Tab) to capture the mouse ---
    if (in.keyPressed(SDL_SCANCODE_TAB))
        in.setRelativeMouseMode(app.window().handle(), !in.relativeMouseMode());
    if (in.mousePressed(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), true);
    if (in.mouseReleased(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), false);

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
```

Things to notice:

- **Esc first releases the mouse**, then quits on the second press. Players expect to get
  their cursor back before the game closes.
- **Mouse look isn't multiplied by `dt`**: the mouse delta is already "movement this
  frame". Stick look is a rate, so it is.
- **Speed ramps** smoothly into the 4× boost using the same `1 - exp(-k·dt)` easing as
  the 2D demo's camera follow.

**Run it:** hold the right mouse button and move the mouse to look; WASD to move,
Space/E and Ctrl/Q to go up and down, Shift to go fast. With a gamepad: sticks to move and
look, triggers for up/down, click the left stick to go fast.

---

## Step 5 — Many objects and animation

Now fill the scene and bring it to life. Animation goes in **`onFixedUpdate`**, which runs
at exactly 60 Hz, so it behaves the same at any frame rate. To keep motion smooth on faster
displays, store the previous value too, and draw
`mix(previous, current, app.interpolationAlpha())`.

Every object is one `draw()` call with its own model matrix and material, and the same
`cube_` mesh is reused for all 13 crates.

In **`MyGame.h`**, declare `onFixedUpdate` next to the other hooks:

```cpp
    void onFixedUpdate(core::Application& app, float dt) override;
```

and add the sphere mesh and animation state:

```cpp
    render3d::Mesh sphere_;

    // Animation state, advanced in onFixedUpdate; prev* for interpolation.
    float spin_ = 0.0f;
    float prevSpin_ = 0.0f;
    float simTime_ = 0.0f;
    float prevSimTime_ = 0.0f;
```

In **`MyGame.cpp`**, add `#include <glm/gtc/constants.hpp>` (for `two_pi`), create the
sphere in `onInit` next to the cube,

```cpp
    sphere_ = Mesh(gpu, primitives::sphere(0.5f, 48, 24), "sphere");
```

and add the fixed update:

```cpp
void MyGame::onFixedUpdate(core::Application&, float dt)
{
    // Animation advances at a fixed rate, independent of the frame rate.
    prevSpin_ = spin_;
    prevSimTime_ = simTime_;
    spin_ += dt * 0.6f; // radians per second
    simTime_ += dt;
}
```

`onRender` now interpolates the animation state, spins the big crate and draws the ring
and the spheres. The full function:

```cpp
void MyGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height));

    // Interpolate the fixed-step animation state for smooth motion.
    const float alpha = app.interpolationAlpha();
    const float spin = glm::mix(prevSpin_, spin_, alpha);
    const float t = glm::mix(prevSimTime_, simTime_, alpha);

    meshRenderer_->begin(frame, camera_, lighting_);

    // Ground: checker texture with a green tint.
    meshRenderer_->draw(ground_, glm::mat4(1.0f), {&checkerTexture_, {0.55f, 0.62f, 0.5f, 1.0f}});

    // Centrepiece: a big crate.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, 0.0f});
    m = glm::rotate(m, spin, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    m = glm::scale(m, glm::vec3(2.0f));
    meshRenderer_->draw(cube_, m, {&crateTexture_});

    // Ring of crates
    constexpr int kRing = 12;
    for (int i = 0; i < kRing; ++i) {
        const float a = float(i) / kRing * glm::two_pi<float>();
        glm::mat4 c = glm::translate(glm::mat4(1.0f), {std::cos(a) * 7.0f, 0.5f, std::sin(a) * 7.0f});
        c = glm::rotate(c, -a + spin * 0.25f, glm::vec3(0, 1, 0));
        meshRenderer_->draw(cube_, c, {&crateTexture_});
    }

    // Bobbing coloured spheres
    const glm::vec4 palette[] = {
        {0.95f, 0.30f, 0.30f, 1.0f}, {0.30f, 0.80f, 0.40f, 1.0f}, {0.30f, 0.50f, 0.95f, 1.0f},
        {0.95f, 0.80f, 0.25f, 1.0f}, {0.80f, 0.40f, 0.90f, 1.0f}, {0.95f, 0.95f, 0.95f, 1.0f},
    };
    for (int i = 0; i < 6; ++i) {
        const float a = float(i) / 6.0f * glm::two_pi<float>() + 0.26f;
        const glm::vec3 p(std::cos(a) * 4.0f, 0.8f + std::sin(t * 2.0f + float(i)) * 0.3f, std::sin(a) * 4.0f);
        glm::mat4 s = glm::translate(glm::mat4(1.0f), p);
        s = glm::scale(s, glm::vec3(1.2f));
        meshRenderer_->draw(sphere_, s, {nullptr, palette[i]});
    }

    meshRenderer_->end();
}
```

Things to notice:

- `onRender` now uses `app` (for `interpolationAlpha()`), so the parameter is named again.
- The spheres are **untextured**: `nullptr` selects a built-in white texture, so the
  material colour is used as-is.
- The renderer skips re-binding the texture when consecutive draws use the same one, so
  drawing all crates together is slightly cheaper than interleaving them with spheres.
- 20 objects is nothing for the GPU. For thousands of objects you would add *instancing*
  (one draw for many copies); see "Where to go next".

**Run it:** the full demo scene: a spinning crate, a slowly turning ring of crates, and
six bobbing spheres with specular highlights.

---

## Step 6 — A moving sun, wireframe and vsync

Finally, three small additions that make tuning and debugging easier.

In `onUpdate`, after the Esc handling, add the toggles:

```cpp
    if (in.keyPressed(SDL_SCANCODE_V))
        app.renderer().setVsync(!app.renderer().vsync());
    if (in.keyPressed(SDL_SCANCODE_F1) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_NORTH))
        meshRenderer_->setWireframe(!meshRenderer_->wireframe());
```

and at the end of `onUpdate`, move the sun:

```cpp
    // Slowly move the sun around the scene.
    const float a = float(app.time()) * 0.1f;
    lighting_.sunDirection = glm::normalize(glm::vec3(std::cos(a), -0.8f, std::sin(a)));
```

Things to notice:

- **`SceneLighting`** is plain data that you can change any time: sun direction, colour
  and intensity, sky/ground ambient, fog colour and density. Try a low orange sun
  (`sunDirection = {1, -0.15f, 0}`, `sunColor = {1, 0.6f, 0.3f}`) for a sunset, and
  remember to update the clear colour if you change the fog colour.
- **Wireframe** (F1, or Y on a gamepad) shows the triangles. It's only available if the GPU
  supports `fillModeNonSolid`, which every desktop GPU and the Deck do.
- **Vsync off** (V) shows how fast the scene really renders in the title bar. Animation
  speed doesn't change, thanks to the fixed timestep.

**Run it:** watch the light and shading on the crates shift as the sun circles; F1 for
wireframe, V for vsync.

---

## Step 7 — A text HUD

Game3D includes the same `render2d` module as Game2D: `SpriteBatch`, `Camera2D` and
**`Font`**. A `SpriteBatch` can draw inside the 3D main pass (it ignores the depth buffer),
so a HUD is just a second set of draw calls after the meshes, with a 2D camera where one
unit is one screen pixel.

A **`render2d::Font`** loads a TrueType/OpenType file and bakes its characters into a
texture atlas with stb_truetype; **`SpriteBatch::drawText`** draws the characters as small
quads. The project ships **Roboto Medium** in `assets/fonts/` (Apache 2.0 licence; see the
README there).

In **`MyGame.h`**, add the includes

```cpp
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"
```

and the overlay members:

```cpp
    // 2D overlay for the HUD, drawn on top of the 3D scene.
    std::unique_ptr<render2d::SpriteBatch> hud_;
    render2d::Camera2D hudCamera_;
    render2d::Font font_;
    bool showHelp_ = true;
```

In **`MyGame.cpp`**, add `#include <cstdio>`. Create the overlay in `onInit` (before the
camera setup):

```cpp
    // 2D overlay: a SpriteBatch works inside the 3D pass too (it ignores depth).
    hud_ = std::make_unique<render2d::SpriteBatch>(gpu, app.renderer());
    font_ = render2d::Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf"));
```

destroy it in `onShutdown` next to the mesh renderer,

```cpp
    hud_.reset();
```

and toggle the help panel with **H** in `onUpdate`, after the other toggles:

```cpp
    if (in.keyPressed(SDL_SCANCODE_H))
        showHelp_ = !showHelp_;
```

Then, in `onRender`, draw the HUD after `meshRenderer_->end();`:

```cpp
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
    char info[160];
    std::snprintf(info, sizeof(info), "%.0f fps   %u draw calls\ncamera  %.1f, %.1f, %.1f",
                  double(app.fps()), meshRenderer_->drawCalls(), double(camera_.position.x),
                  double(camera_.position.y), double(camera_.position.z));
    render2d::TextStyle shadow = text;
    shadow.color = {0.0f, 0.0f, 0.0f, 0.5f};
    hud_->drawText(font_, info, glm::vec2(17.0f, 13.0f) * ui, shadow);
    hud_->drawText(font_, info, glm::vec2(16.0f, 12.0f) * ui, text);

    // Top right: wireframe indicator.
    if (meshRenderer_->wireframe()) {
        render2d::TextStyle tag = text;
        tag.align = render2d::TextAlign::Right;
        tag.color = render2d::colors::Yellow;
        hud_->drawText(font_, "WIREFRAME", {viewport.x - 16.0f * ui, 12.0f * ui}, tag);
    }

    // Bottom: a help panel, sized to fit its text.
    if (showHelp_) {
        const char* help = "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down\n"
                           "Shift: fast      F1 / (Y): wireframe      V: vsync      H: hide help";
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
```

Things to notice:

- **Order matters, depth doesn't.** The HUD is drawn after the scene, and the sprite
  pipeline doesn't test depth, so it always appears on top.
- **`TextStyle`** controls size (pixels here), colour, alignment (`Left`, `Center`,
  `Right` relative to the position) and line spacing; `'\n'` starts a new line.
  `snapToPixel` keeps glyphs on whole pixels for crisp HUD text.
- **A shadow** is the same text drawn one pixel down-right in translucent black first: a
  cheap way to keep text readable over bright scenery.
- **`font.measure`** gives the size of a block of text before drawing it, used here to fit
  the help panel's background.
- **Baked size vs drawn size.** The font is rasterised once at 48 px and scaled when drawn;
  text is sharpest at or a little below that size. Change it with `FontDesc::pixelHeight`.

**Run it:** frame rate, draw calls and the camera position at the top left, a yellow
"WIREFRAME" tag when F1 is on, and a help panel at the bottom (**H** hides it).

---

## Step 8 — 3D sound

The framework plays audio with **miniaudio**. `Application` owns an
**`audio::AudioEngine`**, reached with `app.audio()`. It opens the default output device
at startup and releases finished sounds every frame for you. If the PC has no audio
device, the game still runs: every audio call quietly does nothing.

A sound played with `spatial = true` has a **position** in the world. The engine makes it
quieter with distance and pans it between the speakers by its direction from a
**listener**, which you place at the camera each frame. Walk around the crate and the
hum moves from ear to ear.

The project ships two sounds in `assets/sounds/`: `hum_loop.wav` (a seamless 2-second
loop) and `ping.wav` (CC0; see the README there). WAV, FLAC, MP3 and OGG Vorbis all work.
Use **mono** files for 3D sounds: the engine does the panning itself.

| `PlayParams` field | Meaning |
|---|---|
| `volume`, `pitch` | Linear gain; playback rate (2 = an octave higher) |
| `loop` | Plays until you call `app.audio().stop(handle)` |
| `spatial` | Attenuate and pan relative to the listener |
| `position` | World position of the sound (move it later with `setPosition`) |
| `minDistance` | Full volume up to this distance |
| `maxDistance` | Beyond this distance it gets no quieter |
| `rolloff` | How quickly volume falls with distance (higher = faster) |

In **`MyGame.h`**, add `#include <string>` to the includes and the sound state at the end
of the members:

```cpp
    // 3D sound: a looping hum on the big crate, and chimes from the spheres.
    std::string pingSound_;
    audio::SoundHandle crateHum_;
    float pingTimer_ = 1.0f; // seconds until the next chime
    int nextPing_ = 0;       // which sphere chimes next
```

In **`MyGame.cpp`**, start the hum and load the chime in `onInit`, after the overlay from
step 7:

```cpp
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
```

`play()` returns a **`SoundHandle`**. We keep the hum's handle so it could be moved,
changed or stopped later (`setPosition`, `setVolume`, `stop(crateHum_, fadeSeconds)`). A
looping sound plays until it is stopped or the game exits, so nothing is needed in
`onShutdown`.

The chimes are timed in `onFixedUpdate`, which now uses its `Application` parameter.
Give it a name:

```cpp
void MyGame::onFixedUpdate(core::Application& app, float dt)
```

and add this at the end of the function:

```cpp
    // Every 2 seconds the next sphere chimes from where it stands, each on its own
    // note of a pentatonic scale (pitch = playback rate).
    pingTimer_ -= dt;
    if (pingTimer_ <= 0.0f) {
        pingTimer_ += 2.0f;
        const float notes[6] = {1.0f, 1.125f, 1.25f, 1.5f, 1.6875f, 2.0f};
        const int i = nextPing_;
        nextPing_ = (nextPing_ + 1) % 6;
        const float a = float(i) / 6.0f * glm::two_pi<float>() + 0.26f; // same layout as onRender
        audio::PlayParams ping;
        ping.volume = 0.8f;
        ping.pitch = notes[i];
        ping.spatial = true;
        ping.position = {std::cos(a) * 4.0f, 0.8f, std::sin(a) * 4.0f};
        app.audio().play(pingSound_, ping);
    }
```

In `onUpdate`, toggle mute with **M** after the help toggle,

```cpp
    if (in.keyPressed(SDL_SCANCODE_M))
        app.audio().setMuted(!app.audio().muted());
```

and move the listener with the camera, right after `updateCamera(app, dt);`:

```cpp
    // The player hears the world from the camera.
    app.audio().setListener(camera_.position, camera_.forward(), camera_.up());
```

In `onRender`, replace the wireframe indicator with one that also shows when the game
is muted:

```cpp
    // Top right: wireframe and mute indicators.
    render2d::TextStyle tag = text;
    tag.align = render2d::TextAlign::Right;
    tag.color = render2d::colors::Yellow;
    if (meshRenderer_->wireframe())
        hud_->drawText(font_, "WIREFRAME", {viewport.x - 16.0f * ui, 12.0f * ui}, tag);
    if (app.audio().muted())
        hud_->drawText(font_, "MUTED", {viewport.x - 16.0f * ui, 34.0f * ui}, tag);
```

and add `M: mute` to the help text:

```cpp
        const char* help = "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down\n"
                           "Shift: fast      F1 / (Y): wireframe      M: mute      V: vsync      H: hide help";
```

Things to notice:

- **The listener is the player's ears.** `setListener(position, forward, up)` uses the
  same right-handed axes as `Camera3D`. Update it once per frame, after the camera moves.
  For a third-person game, putting it at the character often sounds more natural.
- **Distance.** Between `minDistance` and `maxDistance` the volume falls off as
  `minDistance / distance` (with `rolloff = 1`). Raise `rolloff` for sounds that should fade
  quickly, such as footsteps.
- **Preloading.** A sound is decoded the first time it plays and then cached in memory.
  `preload` does that work in `onInit`, so the first chime doesn't cause a small hitch.
- **Not everything is 3D.** UI clicks and menu sounds should leave `spatial` off, so they
  play at the same level wherever the camera is (with `pan` for left/right if you like).
- **Volumes and mute.** `setMasterVolume`, `setSfxVolume` and `setMusicVolume` are ready
  for an options menu. `setMuted` silences everything without changing those values.

**Music.** The demo has no music track, but it's one line once you add a file (OGG
Vorbis is a good choice: small and loops cleanly). Put it in `assets/music/` and call:

```cpp
app.audio().playMusic(core::assetPath("music/theme.ogg"), {.fadeInSeconds = 2.0f});
```

Music is streamed from disk rather than loaded whole, loops by default, is never
positioned in 3D and has its own volume (`setMusicVolume`, 0.8 by default). Calling
`playMusic` again crossfades to the new track. `stopMusic(seconds)`, `pauseMusic()` and
`resumeMusic()` do what you'd expect.

**Run it:** the crate hums, louder as you fly towards it and from the left or right as
you turn. Every two seconds one of the spheres chimes from where it stands, walking up a
scale. **M** mutes and "MUTED" appears at the top right.

---

## Step 9 — A debug UI

While you work on a game you constantly want to see a value, nudge a number or press a
"do it now" button without restarting. The framework includes
[Dear ImGui](https://github.com/ocornut/imgui) for exactly that. Lighting is the perfect
example: tuning fog and colours by recompiling is slow, while dragging a slider takes
seconds.

Press **`** (the key below Esc) in any step of this tutorial and two things appear: the
framework's **Engine** window (frame time graph, GPU, vsync, audio volumes, input) and,
from the checkbox at its bottom, ImGui's own demo window, which shows every widget along
with the code that makes it. Press **`** again to hide them.

The debug UI exists in **Debug builds only** (`AppConfig::debugUI.enabled`), so players
never see it. When you ship a Release build it isn't even created.

ImGui is an *immediate-mode* UI: there are no widget objects to create or keep. Every
frame you call functions that both draw a widget and report what happened to it:

```cpp
if (ImGui::Button("Respawn"))           // true on the frame it's clicked
    respawn();
ImGui::SliderFloat("Speed", &speed_, 0.0f, 50.0f); // edits speed_ directly
```

You write that code in a new hook, **`Game::onDebugUI`**. It runs once per frame after
`onUpdate`, and only while the debug UI is visible. That's the only place to call ImGui.

In **`MyGame.h`**, declare the hook after `onRender`,

```cpp
    void onDebugUI(core::Application& app) override;
```

and add two settings at the end of the members:

```cpp
    // Settings changed in the debug UI.
    bool animateSun_ = true;
    float humVolume_ = 0.6f;
```

In **`MyGame.cpp`**, let the debug UI stop the sun's movement. In `onUpdate`, replace the
sun code from step 6 with:

```cpp
    // Slowly move the sun around the scene (unless switched off in the debug UI).
    if (animateSun_) {
        const float a = float(app.time()) * 0.1f;
        lighting_.sunDirection = glm::normalize(glm::vec3(std::cos(a), -0.8f, std::sin(a)));
    }
```

Add `onDebugUI` after `updateCamera`:

```cpp
void MyGame::onDebugUI(core::Application& app)
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

    ImGui::Text("Draw calls: %u", meshRenderer_->drawCalls());
    bool wireframe = meshRenderer_->wireframe();
    if (ImGui::Checkbox("Wireframe", &wireframe))
        meshRenderer_->setWireframe(wireframe);

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &camera_.position.x, 0.1f, -100.0f, 100.0f, "%.1f");
        ImGui::SliderAngle("Yaw", &camera_.yaw, -360.0f, 360.0f);
        ImGui::SliderAngle("Pitch", &camera_.pitch, -89.0f, 89.0f);
        ImGui::SliderAngle("Field of view", &camera_.fovY, 20.0f, 120.0f);
        if (ImGui::Button("Look at the crate"))
            camera_.lookAt({0.0f, 1.5f, 0.0f});
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

    if (ImGui::CollapsingHeader("Sound")) {
        if (ImGui::SliderFloat("Crate hum", &humVolume_, 0.0f, 1.0f))
            app.audio().setVolume(crateHum_, humVolume_);
        if (ImGui::Button("Chime now"))
            pingTimer_ = 0.0f; // onFixedUpdate plays it on the next step
    }

    // Our own textures, shown inside the UI.
    if (ImGui::CollapsingHeader("Textures")) {
        debugui::DebugUI& ui = *app.debugUI(); // never null inside onDebugUI
        const ImVec2 size(96.0f * scale, 96.0f * scale);
        ImGui::Image(ui.texture(crateTexture_), size);
        ImGui::SameLine();
        ImGui::Image(ui.texture(checkerTexture_), size);
    }
    ImGui::End();
}
```

and add the key to the help text in `onRender` (it becomes three lines):

```cpp
        const char* help = "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down\n"
                           "Shift: fast      F1 / (Y): wireframe      M: mute      V: vsync      H: hide help\n"
                           "` (below Esc): debug UI";
```

Things to notice:

- **Widgets edit your variables directly.** `ColorEdit3("Sky colour", &lighting_.skyColor.x)`
  reads and writes the three floats of the `glm::vec3`; there's no copying back and forth.
  Functions return `true` when the value changed, for when you need to react: the fog
  colour also updates the clear colour, and dragging the sun switches its animation off.
- **`SliderAngle`** shows degrees while the variable stays in radians, which suits
  `Camera3D`'s yaw, pitch and field of view.
- **`Begin`/`End` make a window.** Always call `End`, even when `Begin` returns `false` (the
  window is collapsed). `CollapsingHeader` sections keep a long window manageable.
- **Labels are IDs.** Two widgets with the same label in one window clash. Add a hidden
  suffix to tell them apart: `"Colour##sky"` shows as "Colour".
- **Clicks don't leak into the game.** While the mouse is over a debug window, or you are
  typing into a text field, those events don't reach `app.input()`. Right-clicking over a
  window doesn't start the mouse look, and typing a number doesn't move the camera.
- **Textures in the UI.** `app.debugUI()->texture(t)` turns any `gfx::Texture` into
  something `ImGui::Image` can show.
- **ImGui's colours are sRGB.** The framework's ImGui shader converts them for our sRGB
  swapchain, so colours you pick in ImGui look the same as in any image editor.

**Run it:** press **`**. A "Game" window at the top right lets you move and turn the
camera, change the field of view, toggle wireframe, change the sun, sky, ground and fog,
set the crate's hum volume and trigger a chime. Try a fog density of 0.05 with a dark fog
colour for a night scene.

---

## Step 10 — Reverb and muffled sounds

Effects make a place sound like a place. The audio engine has four:

| Effect | What it does | Typical use |
|---|---|---|
| **Low-pass** | Lets only the low frequencies through ("muffles") | Behind a wall, under water, a pause menu |
| **High-pass** | Lets only the high frequencies through ("thin") | A radio, a phone, a tiny speaker |
| **Echo** | Distinct repeats after a delay | A canyon, a long corridor |
| **Reverb** | A room's reflections blending into a tail | A cave, a hall, a bathroom |

Low-pass, high-pass and echo work on a **bus**: `audio::Bus::Sfx` (all sound effects),
`Bus::Music` or `Bus::Master` (everything). The **reverb** is shared: every sound effect
sends some of itself to it, and music never does. A sound can also have its **own**
low-pass, set when it's played (`PlayParams::lowPassHz`) or changed later with
`setLowPass(handle, hz)`.

Every change **glides** to the new value (0.3 seconds by default for buses), because a
filter that snaps on sounds like a glitch.

This step does two things: **R** steps through the reverb presets (off, room, hall, cave),
and a chime from a sphere *behind the big crate* (as seen from the camera) is muffled, the
way a real obstacle blocks the high frequencies first.

In **`MyGame.h`**, add at the end of the members:

```cpp
    int reverbPreset_ = 0;         // index into audio::ReverbPreset (0 = off), cycled with R
    bool lastChimeBlocked_ = false; // was the last chime behind the big crate?
```

In **`MyGame.cpp`**, add a small geometry helper in the unnamed namespace, after
`srgbToLinear`:

```cpp
// True if the straight line from `a` to `b` passes through a sphere: a cheap
// stand-in for "is the big crate in the way?" when deciding to muffle a sound.
bool lineHitsSphere(glm::vec3 a, glm::vec3 b, glm::vec3 centre, float radius)
{
    const glm::vec3 ab = b - a;
    const float t = std::clamp(glm::dot(centre - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    return glm::distance(a + ab * t, centre) < radius;
}
```

In `onFixedUpdate`, decide whether each chime is blocked. Add this just before
`app.audio().play(pingSound_, ping);`:

```cpp
        // Behind the big crate (seen from the camera)? Then muffle it, as a real
        // obstacle would: walls let the low frequencies through best.
        lastChimeBlocked_ = lineHitsSphere(camera_.position, ping.position, {0.0f, 1.5f, 0.0f}, 1.3f);
        if (lastChimeBlocked_)
            ping.lowPassHz = 1200.0f;
```

In `onUpdate`, cycle the reverb with **R**, after the mute toggle:

```cpp
    if (in.keyPressed(SDL_SCANCODE_R)) { // Off -> Room -> Hall -> Cave -> Off
        reverbPreset_ = (reverbPreset_ + 1) % 4;
        app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset(reverbPreset_)));
    }
```

In `onRender`, show the reverb at the top right, after the "MUTED" tag,

```cpp
    const char* reverbNames[] = {"", "REVERB: ROOM", "REVERB: HALL", "REVERB: CAVE"};
    if (reverbPreset_ > 0)
        hud_->drawText(font_, reverbNames[reverbPreset_], {viewport.x - 16.0f * ui, 56.0f * ui}, tag);
```

and replace the help text:

```cpp
        const char* help = "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down\n"
                           "Shift: fast      F1 / (Y): wireframe      M: mute      R: reverb      V: vsync\n"
                           "H: hide help      ` (below Esc): debug UI";
```

Finally, in `onDebugUI`, add to the **Sound** section, after the "Chime now" button:

```cpp
        ImGui::Text("Last chime: %s", lastChimeBlocked_ ? "muffled (behind the crate)" : "clear");
        const char* presets[] = {"Off", "Room", "Hall", "Cave"};
        if (ImGui::Combo("Reverb", &reverbPreset_, presets, 4))
            app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset(reverbPreset_)));
```

Things to notice:

- **Occlusion is the game's decision.** The audio engine doesn't know about walls; it
  muffles what you tell it to. Here a sphere stands in for the crate. In a real game you'd
  cast a ray with your physics or collision code, and use `setLowPass(handle, ...)` to
  update long-playing sounds while things move.
- **A muffled sound is mostly quieter.** The chime is close to a pure tone, so cutting
  above 1200 Hz takes away about two thirds of its level and leaves a softer sound. On
  noisy sounds (footsteps, engines) a low-pass sounds more obviously "dull".
- **Presets are plain numbers.** `ReverbParams::preset(ReverbPreset::Hall)` returns a
  `ReverbParams` (`level`, `roomSize`, `damping`, `width`) that you can tweak before
  passing it to `setReverb`.
- **Only sound effects reverberate.** The hum and the chimes are effects, so both pick up
  the reverb. Music never does, and a sound played with `{.reverbSend = 0.0f}` stays dry,
  which suits interface sounds.
- **Try every effect live.** In the debug UI, open **Engine → Audio → Audio effects** for
  sliders on each bus, reverb presets and a reset button.

**Run it:** press **R** a few times and listen to the hum and the chimes move from a dry
field to a room, a hall and a cave. Then fly around so the big crate is between you and
the spheres: chimes from behind it are softer and duller. The debug UI's Sound section
says whether the last chime was blocked.

---

## Step 11 — A passing drone (Doppler)

When a car or a plane passes you, its sound jumps from high to low. That's the **Doppler
effect**: a sound moving towards you is squeezed to a higher pitch, and one moving away
is stretched lower. It's one of the strongest cues that something is *moving*, rather
than just being somewhere.

The audio engine does it for every 3D (spatial) sound. You don't give it any speeds: each
frame it works out how fast each sound and the listener moved from your `setPosition` and
`setListener` calls. So a moving sound only needs its position updated every frame, which
is what a moving sound needs anyway.

| Setting | Where | Meaning |
|---|---|---|
| `speedOfSound` | `AudioDesc`, or `setSpeedOfSound` | In *your* world units per second. 343 if one unit is a metre (the default) |
| `dopplerFactor` | `AudioDesc`, or `setDopplerFactor` | For every sound: 0 = off, 1 = realistic, 2-3 = exaggerated |
| `PlayParams::doppler` | Per sound | Multiplies the engine's factor for this sound; 0 = never shifts |

This step adds a drone that circles the scene with a buzzing motor. The project ships its
sound, `assets/sounds/drone_loop.wav` (a seamless 1-second loop, CC0 like the others).

In **`MyGame.h`**, add at the end of the members:

```cpp
    // A drone circling the scene with a buzzing motor: listen to its pitch as it passes.
    audio::SoundHandle droneSound_;
    float droneAngle_ = 0.0f;     // position on its circle (radians), advanced in onFixedUpdate
    float prevDroneAngle_ = 0.0f;
    float droneSpeed_ = 18.0f;    // units per second
```

In **`MyGame.cpp`**, add the drone's flight path in the unnamed namespace, after
`lineHitsSphere`:

```cpp
// The drone's flight path: a 12-unit circle around the scene, bobbing up and down.
// It passes just behind the camera's starting position.
constexpr float kDroneRadius = 12.0f;

glm::vec3 droneAt(float angle)
{
    return {std::cos(angle) * kDroneRadius, 4.0f + std::sin(angle * 2.0f) * 0.8f, std::sin(angle) * kDroneRadius};
}
```

Start its sound in `onInit`, after the chime is preloaded:

```cpp
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
```

Fly it in `onFixedUpdate`, after `simTime_ += dt;`:

```cpp
    prevDroneAngle_ = droneAngle_;
    droneAngle_ += droneSpeed_ / kDroneRadius * dt; // angle = distance / radius
```

Move its sound with it in `onUpdate`, after `setListener`:

```cpp
    // Move the drone's sound with the drone. The audio engine works out its
    // velocity from these positions, and from that the Doppler pitch.
    const float droneAngle = glm::mix(prevDroneAngle_, droneAngle_, app.interpolationAlpha());
    app.audio().setPosition(droneSound_, droneAt(droneAngle));
```

Draw it in `onRender`, after the spheres and before `meshRenderer_->end();`:

```cpp
    // The drone: a dark body facing along its path, four rotor discs and a red light.
    const float droneAngle = glm::mix(prevDroneAngle_, droneAngle_, alpha);
    glm::mat4 d = glm::translate(glm::mat4(1.0f), droneAt(droneAngle));
    d = glm::rotate(d, -droneAngle, glm::vec3(0, 1, 0));
    const glm::vec4 dark(0.18f, 0.2f, 0.24f, 1.0f);
    meshRenderer_->draw(cube_, glm::scale(d, glm::vec3(0.7f, 0.18f, 0.45f)), {nullptr, dark});
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 corner((i & 1) ? 0.55f : -0.55f, 0.12f, (i & 2) ? 0.4f : -0.4f);
        meshRenderer_->draw(sphere_, glm::scale(glm::translate(d, corner), glm::vec3(0.6f, 0.05f, 0.6f)),
                            {nullptr, {0.35f, 0.38f, 0.42f, 1.0f}});
    }
    meshRenderer_->draw(sphere_, glm::scale(glm::translate(d, {0.0f, 0.12f, 0.0f}), glm::vec3(0.18f)),
                        {nullptr, {1.0f, 0.15f, 0.1f, 1.0f}});
```

And in `onDebugUI`, at the end of the **Sound** section:

```cpp
        ImGui::SeparatorText("Doppler");
        ImGui::SliderFloat("Drone speed", &droneSpeed_, 0.0f, 40.0f, "%.0f units/s");
        float factor = app.audio().dopplerFactor();
        if (ImGui::SliderFloat("Doppler factor", &factor, 0.0f, 4.0f, "%.1f"))
            app.audio().setDopplerFactor(factor);
        const float pitch = app.audio().dopplerPitch(droneSound_);
        ImGui::Text("Drone pitch: %+.1f%%", double((pitch - 1.0f) * 100.0f));
```

Things to notice:

- **Positions every frame are all it takes.** The engine compares each sound's position
  with the previous frame's, smooths the result over about a tenth of a second (so
  uneven frame times don't wobble the pitch), and hands the velocities to miniaudio.
- **The listener moves too.** Fly towards the humming crate quickly (hold **Shift**) and
  its hum rises slightly; fly away and it drops. That's the same effect, from the
  listener's motion.
- **Teleports are ignored.** A jump faster than sound (respawning, a cut to a new camera)
  isn't treated as a real speed, so it doesn't produce a squeal.
- **Realistic Doppler is subtle.** At 18 units per second (65 km/h, if units are metres) the
  real shift is about 5%. The drone doubles it with `doppler = 2.0f`; most games
  exaggerate it like this. Very high speeds are capped so the pitch can't run away.
- **Choose your speed of sound to match your units.** If your world uses centimetres,
  set `speedOfSound = 34300`; with metres the default is right.
- **Known velocities.** If you already know an object's velocity (from physics, say),
  `app.audio().setVelocity(handle, v)` uses it instead of the estimate.
  `setListenerVelocity` does the same for the listener.
- **Music and plain (non-spatial) sounds are never shifted**, and neither is anything
  played with `{.doppler = 0.0f}`: good for a 3D sound that shouldn't wobble, such as a
  radio held by the player.

**Run it:** stand still near the start and listen as the drone comes round. Its buzz rises
as it approaches, drops as it passes, and stays low as it flies away. In the debug
UI's **Sound** section, the "Drone pitch" readout follows it. Raise the drone speed or the
Doppler factor to exaggerate the effect, or set the factor to 0 to hear the difference.

---

## Step 12 — A drone model (glTF)

The drone is still a box and some squashed spheres. Real games draw **models**: shapes
made in a modelling program such as Blender, with their own materials and textures. The
framework loads **glTF 2.0**, the open standard for this ("the JPEG of 3D"): Blender
exports it (**File > Export > glTF 2.0**), and most free and paid asset packs include it.

A glTF file comes in two forms, and both load the same way:

| Form | Files | Best for |
|---|---|---|
| `.glb` | One file with everything inside | Games: one file to ship, nothing to lose |
| `.gltf` | A text file plus a `.bin` (the geometry) and the images | Reading and debugging; keep them all together |

In Blender, choose **glTF Binary (.glb)** as the format. The project ships a drone made
for this step, `assets/models/drone.glb` (CC0, like the sounds): about 1.3 m across, 430
triangles and one small texture. Its parts are separate objects, which glTF calls
**nodes**: `Body`, `Frame`, `Lights`, `Eye` and four rotors named `RotorFL`, `RotorFR`,
`RotorBL` and `RotorBR` (front/back, left/right).

In **`MyGame.h`**, include the model header after `MeshRenderer.h`:

```cpp
#include "render3d/Model.h"
```

and add at the end of the members:

```cpp
    // The drone's 3D model (a glTF file), and its rotors' pose.
    render3d::Model droneModel_;
    render3d::ModelPose dronePose_; // this drone's own node transforms
    int rotors_[4] = {-1, -1, -1, -1}; // node indices: front left, front right, back left, back right
    float rotorAngle_ = 0.0f;
```

In **`MyGame.cpp`**, load it in `onInit`, after the drone's sound starts:

```cpp
    // The drone's model: a glTF file, the format Blender exports. Its parts are nodes;
    // look up the rotors by name once, so they can be spun every frame.
    droneModel_ = render3d::Model::load(app.gpu(), core::assetPath("models/drone.glb"));
    dronePose_ = droneModel_.defaultPose();
    const char* rotorNames[4] = {"RotorFL", "RotorFR", "RotorBL", "RotorBR"};
    for (int i = 0; i < 4; ++i)
        rotors_[i] = droneModel_.findNode(rotorNames[i]);
```

Spin the rotors in `onUpdate`, after the drone's sound is moved:

```cpp
    // Spin the rotors: purely visual, so once per frame is enough. They turn faster
    // when the drone flies faster.
    rotorAngle_ = std::fmod(rotorAngle_ + (30.0f + droneSpeed_) * dt, glm::two_pi<float>());
```

And in `onRender`, **replace** the step 11 drone (from `// The drone: a dark body...` to
the red light) with:

```cpp
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
```

Things to notice:

- **One `draw` call per model, as for a mesh.** `MeshRenderer` draws each part with its
  own material: the textured hull, the dark metal frame, the rotors. The draw calls in
  the HUD go up by one per part.
- **Glowing parts.** The eye and the navigation lights (red on the left, green on the
  right, as on aircraft) are **emissive**: they glow the same whether or not the sun
  reaches them (fog still fades them with distance, like everything else). In Blender,
  that's the Principled BSDF's *Emission* colour and strength.
- **Nodes and poses.** A model's nodes are the objects in Blender's Outliner, each placed
  relative to its parent. `findNode` looks one up by name (once: it's a search). A
  `ModelPose` holds the position, rotation and scale of every node for *one copy* of the
  model, so ten drones on screen can each have their own pose from one loaded `Model`.
  Changing `dronePose_[node].rotation` turns that part about its own **origin**, so in
  Blender, set each rotor's origin to its centre (*Object > Set Origin > Origin to
  Geometry*).
- **A misspelt name doesn't crash.** `findNode` returns -1 and logs a warning once, and
  `pose[-1]` is quietly ignored, so a renamed part just stops moving.
- **Transforms stack.** `d` is translated, then turned to face along the path, then
  rolled about its own front axis; the model's matrix is multiplied with each node's.
  Try `glm::scale(d, glm::vec3(2.0f))` for a drone twice the size.
- **Which way is front?** Models face **+Z** in the framework, which is Blender's **-Y**
  after export (the glTF exporter turns Blender's Z-up into Y-up). If a model faces the
  wrong way, rotate it in code or in Blender (and apply the rotation: *Ctrl+A*).
- **Exporting from Blender.** Apply scale and rotation (*Ctrl+A > All Transforms*), use
  metres, keep materials simple (Principled BSDF with a Base Color image, roughness,
  metallic, emission). Untick *Compression* in the export dialog: Draco-compressed files
  aren't supported and are refused with a message saying so.
- **Sizes for gameplay.** `droneModel_.size()` and `boundsMin()/boundsMax()` give the
  model's extent in metres: handy for standing a model on the ground or for collision.

**Run it:** the drone now has a hull, glowing eye and lights, and four spinning rotors
(fly up close and press **F1** for wireframe to see how few triangles it takes). Raise
the drone speed in the debug UI: it leans further into the turn and its rotors spin faster.

---

## Step 13 — An animated gunner

The drone's rotors were moved by code. Most movement in games is **animation** made in
a modelling program instead: a soldier firing, a door opening, a turret recoiling.
Blender stores each animation as an **Action** ("Idle", "Fire", "Load"), and the glTF
exporter writes them into the file. The framework plays them with a
`render3d::ModelAnimator`, which works like the 2D `Animator`: you tell it which
animation to play, call `update(dt)` every frame, and draw with the **pose** it works out.

The project ships a soldier made for this step, `assets/models/gunner.glb`, built from
separate rigid parts (head, torso, upper arms, forearms, hands, legs), each turning about
its joint. This step uses three of its animations: **Idle** (4 s, loops: breathing and
looking around), **Fire** (0.45 s: the recoil) and **Load** (1.5 s: the left hand swaps
the magazine). Step 14 uses the other two, **Walk** and **Run**.
His rifle is a separate model, `assets/models/rifle.glb`, attached in code to an empty
in his right hand called `Socket_Hand_R`: a **socket**. Swap the rifle for any other
model and it's carried the same way.

In **`MyGame.h`**, include the animator after `Model.h`:

```cpp
#include "render3d/ModelAnimator.h"
```

declare two helpers after `updateCamera`:

```cpp
    void updateGunner(core::Application& app, float dt);
    glm::mat4 gunnerTransform() const;
```

and add at the end of the members:

```cpp
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
```

In **`MyGame.cpp`**, load the models and sounds in `onInit`, before the camera is placed:

```cpp
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
```

Add the two helpers before `updateCamera`:

```cpp
// Where the gunner stands, turned to face the drone (his model faces +Z).
glm::mat4 MyGame::gunnerTransform() const
{
    const glm::vec3 position(-2.5f, 0.0f, 5.5f);
    const glm::vec3 to = droneNow_ - position;
    const float yaw = std::atan2(to.x, to.z);
    return glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0));
}

void MyGame::updateGunner(core::Application& app, float dt)
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

```

Call it in `onUpdate`, after the rotors are spun:

```cpp
    droneNow_ = droneAt(droneAngle);
    updateGunner(app, dt);
```

And in `onRender`, draw the gunner after the drone (before `meshRenderer_->end()`):

```cpp
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
        meshRenderer_->draw(sphere_, glm::scale(glm::translate(muzzle, {0.0f, 0.0f, 0.06f}), glm::vec3(0.12f, 0.12f, 0.22f)),
                            flash);
    }
```

Things to notice:

- **`play` is safe to call every frame.** Asking for the animation that's already
  playing does nothing, unless you pass `restart = true` (rapid fire restarts **Fire**
  on every shot). `AnimationLoop::Once` plays to the end and holds the last pose, and
  `finished()` then tells you it's over: that's when the gunner goes back to **Idle**.
- **Changing animation blends.** For `blendTime` (0.15 s by default) the animator mixes
  the old pose into the new one, so going from **Fire** to **Idle** doesn't snap.
- **Moments in an animation.** `reached(seconds)` is true on the update that passed that
  time: the shot at 0 s into **Fire**, the magazine clicks at 0.3 s and 1.0 s into
  **Load**. It's how you'd spawn a bullet or play a footstep exactly in time with the
  animation. To find the times, take the frame number in Blender's timeline, subtract
  the first frame and divide by the frame rate (frame 25 of an animation starting on
  frame 1, at 24 fps, is 1 s), and tick *Set all glTF Animation starting at 0* in the
  exporter's Animation settings so the file's times start at 0 too.
- **Sockets.** `nodeMatrix(node, transform, &pose)` is the world matrix of one node in
  this pose. Drawing another model with it attaches that model there; the rifle's own
  `Muzzle` node then gives the place for the flash. Chains of attachments work the same way.
- **Animation plus code.** `pose()` can be changed after `update()`: here the torso is
  tilted back to aim up at the drone, on top of whatever animation is playing. The
  pose is rebuilt on every update, so the tilt doesn't build up.
- **Unknown names don't crash.** A misspelt animation logs a warning once, listing the
  model's animations, and the current one carries on.
- **One model, many soldiers.** Each `ModelAnimator` has its own time and pose, so a
  squad can share one loaded `gunner_`, each with its own animator.

**Making your own in Blender.** Build the character from separate objects with the same
names and hierarchy as the gunner (or your own scheme, kept the same for every character),
each with its **origin at its joint** (the shoulder for an upper arm: *Object > Set
Origin > Origin to 3D Cursor*, with the cursor on the joint). Parent them (*Ctrl+P >
Object*), apply scale (*Ctrl+A*), and add an empty called `Socket_Hand_R` in the hand.
Then either:

- **use an armature** (recommended for characters): add bones at the joints and parent
  each part to its bone (*Ctrl+P > Bone*, not *With Automatic Weights*), then make one
  Action per animation on the armature (Dope Sheet > Action Editor, *New*). The parts
  stay rigid, the bones become nodes with the parts as their children, and the framework
  animates them the same way. One object holds all the animation, so this is the easy
  route; or
- **animate the objects themselves:** one animation is **one Action shared by every part**
  (Blender 4.4 and later give each object its own *slot* in it). Keying several objects
  gives each one its own Action (`HipsAction`, `TorsoAction`, …), which would export as
  separate animations that each move one part, so: key one part, rename its Action to the
  animation's name ("Fire"), then pick that Action for each other part (Object
  Properties > Animation, or the Action Editor) before keying it. Importing
  `gunner.glb` into Blender shows this set-up.

**Export** (*File > Export > glTF 2.0*, format *glTF Binary (.glb)*): tick *Animation*
and leave *Animation > Mode* on **Actions** (the default) so each Action becomes one
animation with its name. Don't use *NLA Tracks*: tested with Blender 5.2, it mixed the
other tracks into each animation and moved the rest pose. Blender exports whole frames,
so keys between frames are rounded (frame 1 is 1/24 s at 24 fps, unless you tick *Set
All glTF Animation Starting at 0*). Use Linear keyframes for loops. Skinned meshes
(vertices bent by several bones with *Automatic Weights*) aren't supported yet: they
load in their rest pose with a warning.

**Run it:** the gunner stands on the left, turning to follow the drone, breathing and
glancing around. Press **F** to fire (watch the recoil and the muzzle flash), five
times to empty the rifle and see him reload, or **L** to reload at any time. The HUD
shows his animation and ammunition.

---

## Step 14 — A third-person camera

The fly camera is good for looking around, but most action games follow a character: the
camera floats behind and above the player, turns around him with the mouse or the right
stick, and moves in when a wall gets in the way. `render3d::ThirdPersonCamera` does that.
You give it the player's position every frame and it works out where the camera goes:

- it **follows** with a little lag, which smooths out small jerks in the movement;
- it **orbits** (`orbit`) and **zooms** (`zoom`) around a point at head height, shifted
  sideways for an over-the-shoulder view (`shoulder`);
- it **avoids obstacles**: you give it a function that casts a ray into your world, and
  when something comes between the player and the camera, the camera jumps in front of it
  at once, then eases back out when the way is clear.

The rays are tested with `render3d/Raycast.h`: `raycastPlane`, `raycastSphere` and
`raycastBox`, each returning the distance to the hit (or nothing). `raycastBox` takes the
same model matrix you draw a crate with, so the collision shapes can't drift away from
what's drawn. The same tests work for shooting, line of sight and picking things with
the mouse.

The player is a second soldier, using `gunner.glb` again: it has two more animations
for this step, **Walk** (1 s) and **Run** (0.6 s), both loops. One loaded model, two
animators, two soldiers doing different things. **C** switches between the two cameras.

In **`MyGame.h`**, include the two new headers after `ModelAnimator.h`:

```cpp
#include "render3d/Raycast.h"
#include "render3d/ThirdPersonCamera.h"
```

and `#include <optional>` after `#include <memory>`. Replace the `updateCamera`
declaration with these:

```cpp
    void updateMouseCapture(core::Application& app);
    void updateFlyCamera(core::Application& app, float dt);
    void updateFollowCamera(core::Application& app, float dt);
    void updatePlayer(core::Application& app, float dt);
    std::optional<float> raycastScene(const render3d::Ray& ray) const;
    glm::mat4 playerTransform() const;
```

Replace the `cameraSpeedBoost_` member with:

```cpp
    float cameraSpeedBoost_ = 1.0f; // eases between normal and fast movement (fly camera)

    // Two ways to view the scene, switched with C: flying freely, or following the
    // player from behind (third person).
    enum class CameraMode { Fly, ThirdPerson };
    CameraMode cameraMode_ = CameraMode::ThirdPerson;
    render3d::ThirdPersonCamera follow_;

    // The player: a second soldier (the gunner's model, with his own animator) who walks
    // and runs where you steer him in third person.
    glm::vec3 playerPos_{2.0f, 0.0f, 10.0f}; // his feet
    float playerYaw_ = glm::pi<float>();      // which way he faces (pi = along -Z)
    float playerSpeed_ = 0.0f;                // metres per second, eased
    render3d::ModelAnimator playerAnim_;
```

and replace the animation-state members (`spin_` to `prevSimTime_`) with these, which add
the values for this frame (the camera's collisions need to know where the crates are now):

```cpp
    float spin_ = 0.0f;
    float spinNow_ = 0.0f;    // spin_ interpolated for this frame (the camera's collisions use it)
    float prevSpin_ = 0.0f;
    float simTime_ = 0.0f;
    float simTimeNow_ = 0.0f; // simTime_ interpolated for this frame
    float prevSimTime_ = 0.0f;
```

In **`MyGame.cpp`**, the crates, spheres and player now need their positions in three
places: to draw them, to stop the player walking through them, and to stop the camera
seeing through them. Add these helpers to the anonymous namespace, after `droneAt`:

```cpp
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
```

In `onInit`, replace the two lines that place the camera with:

```cpp
    // The player: the same soldier model with his own animator (and so his own pose).
    playerAnim_.play(gunner_, "Idle");

    // Start behind him, looking where he looks (along -Z).
    follow_.yaw = -glm::half_pi<float>();
    follow_.snapTo(playerPos_);
    follow_.apply(camera_);
```

The chimes now come from where the spheres really are (they bob). In `onFixedUpdate`,
replace the lines from `nextPing_ = ...` to `ping.position = ...` with:

```cpp
        nextPing_ = (nextPing_ + 1) % kSpheres;
        audio::PlayParams ping;
        ping.volume = 0.8f;
        ping.pitch = notes[i];
        ping.spatial = true;
        ping.position = sphereCentre(i, simTime_);
```

In `onUpdate`, replace `updateCamera(app, dt);` with:

```cpp
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
```

Now split `updateCamera` up. Its first lines, which capture the mouse, become a function
of their own that both cameras use; the rest becomes `updateFlyCamera`. Replace the start
of `updateCamera`, up to and including `if (in.relativeMouseMode()) {`, with the
following pieces. First the mouse capture:

```cpp
// Hold the right mouse button (or press Tab) to capture the mouse for looking around.
void MyGame::updateMouseCapture(core::Application& app)
{
    core::Input& in = app.input();
    if (in.keyPressed(SDL_SCANCODE_TAB))
        in.setRelativeMouseMode(app.window().handle(), !in.relativeMouseMode());
    if (in.mousePressed(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), true);
    if (in.mouseReleased(SDL_BUTTON_RIGHT))
        in.setRelativeMouseMode(app.window().handle(), false);
}
```

The player: he moves relative to the camera (forward = away from it), turns smoothly
towards where he's going, and plays Idle, Walk or Run to match his speed:

```cpp
// The player soldier, its feet at playerPos_, turned to face playerYaw_ (his model faces +Z).
glm::mat4 MyGame::playerTransform() const
{
    return glm::rotate(glm::translate(glm::mat4(1.0f), playerPos_), playerYaw_, glm::vec3(0, 1, 0));
}

void MyGame::updatePlayer(core::Application& app, float dt)
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

    // The animation follows the speed: Idle, Walk or Run, played faster or slower so
    // his feet keep pace with the ground.
    if (playerSpeed_ < 0.2f) {
        playerAnim_.play(gunner_, "Idle");
        playerAnim_.speed = 1.0f;
    } else if (playerSpeed_ < (kWalkSpeed + kRunSpeed) * 0.5f) {
        playerAnim_.play(gunner_, "Walk");
        playerAnim_.speed = playerSpeed_ / kWalkAnimSpeed;
    } else {
        playerAnim_.play(gunner_, "Run");
        playerAnim_.speed = playerSpeed_ / kRunAnimSpeed;
    }
    playerAnim_.update(dt);
}
```

What the camera can't see through, as a function that casts a ray into the scene:

```cpp
// What the third-person camera can't see through: the ground, the crates, the spheres and
// the gunner.
// Returns the distance to the nearest one along the ray, if any.
std::optional<float> MyGame::raycastScene(const render3d::Ray& ray) const
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
    return nearest;
}
```

The camera: orbit, zoom, then `update` with the player's position and the ray function,
and `apply` to copy the result into `camera_`. Then the start of the old function, renamed
to `updateFlyCamera` (the rest of it stays as it was):

```cpp
void MyGame::updateFollowCamera(core::Application& app, float dt)
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

void MyGame::updateFlyCamera(core::Application& app, float dt)
{
    core::Input& in = app.input();

    // --- Look: with the captured mouse (see updateMouseCapture) or the right stick ---
    if (in.relativeMouseMode()) {
```

In `onRender`, the animation state for this frame is now worked out in `onUpdate`.
Replace the three lines after `camera_.setAspect(...)` with:

```cpp
    // The fixed-step animation state, interpolated for smooth motion (in onUpdate).
    const float alpha = app.interpolationAlpha();
    const float spin = spinNow_;
    const float t = simTimeNow_;
```

draw the crates with the helpers (replacing the big crate and the ring loop):

```cpp
    // Centrepiece: a big crate, and a ring of crates around it.
    meshRenderer_->draw(cube_, bigCrateMatrix(spin), {&crateTexture_});
    for (int i = 0; i < kRing; ++i)
        meshRenderer_->draw(cube_, ringCrateMatrix(i, spin), {&crateTexture_});
```

and the spheres (replacing their loop; the `palette` stays):

```cpp
    for (int i = 0; i < kSpheres; ++i) {
        glm::mat4 s = glm::translate(glm::mat4(1.0f), sphereCentre(i, t));
        s = glm::scale(s, glm::vec3(kSphereRadius * 2.0f));
        meshRenderer_->draw(sphere_, s, {nullptr, palette[i]});
    }
```

Draw the player after the gunner, before `meshRenderer_->end()`:

```cpp
    // The player: the same two models again, in his own pose.
    const glm::mat4 p = playerTransform();
    meshRenderer_->draw(gunner_, p, &playerAnim_.pose());
    meshRenderer_->draw(rifle_, gunner_.nodeMatrix(gunnerSocket_, p, &playerAnim_.pose()));
```

The HUD shows the camera mode. Replace the `info` lines with:

```cpp
    char info[256];
    std::snprintf(info, sizeof(info), "%.0f fps   %u draw calls\ncamera  %s   %.1f, %.1f, %.1f\ngunner  %s   ammo %d",
                  double(app.fps()), meshRenderer_->drawCalls(),
                  cameraMode_ == CameraMode::Fly ? "fly" : (follow_.blocked() ? "third person (blocked)" : "third person"),
                  double(camera_.position.x), double(camera_.position.y), double(camera_.position.z),
                  gunnerAnim_.animationName().c_str(), ammo_);
```

and the help text with one for each camera:

```cpp
        const char* help = cameraMode_ == CameraMode::Fly
            ? "FLY CAMERA      C / (Start): follow the soldier\n"
              "Right mouse / Tab: look      WASD / left stick: move      Space / Ctrl: up / down      Shift: fast\n"
              "F / (B): gunner fires      L / (X): gunner reloads      F1 / (Y): wireframe\n"
              "M: mute      R: reverb      V: vsync      H: hide help      ` (below Esc): debug UI"
            : "THIRD PERSON      C / (Start): fly camera\n"
              "WASD / left stick: walk      Shift / (L3): run      Right mouse / Tab / right stick: look around\n"
              "Mouse wheel / d-pad: zoom      F / (B): gunner fires      L / (X): gunner reloads      F1 / (Y): wireframe\n"
              "M: mute      R: reverb      V: vsync      H: hide help      ` (below Esc): debug UI";
```

Finally, the debug UI gets a mode switch and the third-person settings, so you can tune
them while you play. In `onDebugUI`, replace the start of the Camera section, up to and
including the `DragFloat3("Position", ...)` line, with:

```cpp
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
```

The rest of the Camera section (yaw, pitch, field of view and the "Look at the crate"
button) now belongs to "Fly camera".

Things to notice:

- **Moving relative to the camera.** `flatForward()` and `flatRight()` are the camera's
  directions along the ground. Mixing them by the input makes W always walk away from the
  camera and D always walk to its right, wherever it has been turned.
- **Smoothing that doesn't depend on the frame rate.** The player's turning and speed, and
  the camera's following, all move a fraction `1 - exp(-sharpness * dt)` of the way to
  their target each frame. Unlike a fixed fraction per frame, that gives the same motion
  at 30 and 300 fps.
- **In fast, out slowly.** When the camera is blocked it moves in at once, so you never see
  the inside of a crate; when the way is clear again it eases out (`returnSharpness`), so
  it doesn't jump back and forth as you walk past things. It casts five rays, from the
  look-at point and from points around it as wide as the camera (`collisionRadius`), so
  it doesn't slip past edges that a single thin ray would miss.
- **Animation speed matches the ground.** Walk was made for about 1.35 m/s, so playing it at
  `playerSpeed_ / 1.35` keeps his feet from sliding when he walks slower or faster (a
  half-pushed stick walks slowly). Above half way between walking and running speed, Run
  takes over; the animator blends between them.
- **Collisions without physics.** On a flat world, pushing the player out of a circle
  around each obstacle is enough. A physics engine (Jolt, say) does this for any shape.
- **The listener stays at the camera**, so sounds pan as you see them.

**Try it:** in the debug UI's Third person section, set *Shoulder* to 0 for a centred view,
or change *Follow sharpness* (higher is tighter, 0 is rigid) and *Return sharpness*.

**Run it:** you start behind the second soldier. Walk with **WASD** (or the left stick)
and run with **Shift** (or click the stick); hold the right mouse button, press **Tab**,
or use the right stick to look around him; zoom with the mouse wheel or the d-pad. Walk
behind the big crate and turn the camera so the crate is between you and it: the camera
moves in front of it ("blocked" in the HUD). **C** switches to the fly camera and back.

**That's the complete demo.** Your `MyGame` now behaves exactly like `DemoGame`. You can
delete `DemoGame.h/.cpp` from your own projects once you no longer need them for reference.

---

## Where to go next

Ideas, roughly in order of difficulty, each building on what you've used here:

1. **Your own meshes.** Fill a `render3d::MeshData` (positions, normals, UVs, and
   counter-clockwise triangles) and construct a `Mesh` — try a pyramid or a cylinder.
2. **Aiming over the shoulder.** While the right mouse button is held, zoom in (a smaller
   `distance`, a bigger `shoulder`), turn the player to face where the camera looks, and
   cast a ray from the camera through the middle of the screen with `raycastScene` to
   find what he'd hit.
3. **A squadron.** Draw several drones from the one `droneModel_`, each with its own
   `ModelPose`, flight path and rotor speed: one `Model` in memory, many on screen. Or a
   squad of gunners, each with its own `ModelAnimator`, firing in turn.
4. **A skybox.** A cube drawn around the camera with a sky texture, rendered first with
   depth writes off.
5. **Shadows.** Render the scene's depth from the sun in `onPreRender`, then sample that
   *shadow map* in `mesh.frag`.
6. **Instancing.** Draw many copies of a mesh in one call by putting the model matrices in a
   buffer; essential for forests, crowds and particles.
7. **Physics.** Jolt Physics, stepped in `onFixedUpdate`.
8. **Saving tuned values.** Store `lighting_` with `core::SaveData` from a "Save" button in
   the debug UI and load it in `onInit`, so tuned values survive a restart.
9. **World-space labels.** Project a 3D position to the screen with
   `camera.viewProjection()` and draw a name tag there with the HUD batch.
