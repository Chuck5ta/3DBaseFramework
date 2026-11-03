# Game3D — Tutorial: Making the Demo

This tutorial rebuilds the demo scene from an empty window, one feature at a time. It uses a
new class, `MyGame`, so the original `DemoGame` stays available for comparison. After
every step you can build and run the game and see the result.

**What you'll learn:** the `Game` hooks, meshes and the `MeshRenderer`, materials and
textures, lighting and fog, a fly camera driven by mouse, keyboard and gamepad,
animation on the fixed timestep with interpolation, a 2D text HUD on top of the 3D scene,
3D sound, audio effects and a debug UI.

**Before you start:** the project builds and runs (see [BUILDING.md](BUILDING.md)). For
background on any class used here, see [ARCHITECTURE.md](ARCHITECTURE.md).

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
            uint8_t* p = &px[(y * N + x) * 4];
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

**That's the complete demo.** Your `MyGame` now behaves exactly like `DemoGame`. You can
delete `DemoGame.h/.cpp` from your own projects once you no longer need them for reference.

---

## Where to go next

Ideas, roughly in order of difficulty, each building on what you've used here:

1. **Your own meshes.** Fill a `render3d::MeshData` (positions, normals, UVs, and
   counter-clockwise triangles) and construct a `Mesh` — try a pyramid or a cylinder.
2. **A third-person character.** Replace the fly camera: move a "player" cube in
   `onFixedUpdate` and place the camera behind it, smoothing with `1 - exp(-k·dt)`.
3. **Model loading.** Load glTF 2.0 files (cgltf or fastgltf) into `MeshData` and
   `Texture`s, so you can use models from Blender or asset stores.
4. **A skybox.** A cube drawn around the camera with a sky texture, rendered first with
   depth writes off.
5. **Shadows.** Render the scene's depth from the sun in `onPreRender`, then sample that
   *shadow map* in `mesh.frag`.
6. **Instancing.** Draw many copies of a mesh in one call by putting the model matrices in a
   buffer; essential for forests, crowds and particles.
7. **Physics.** Jolt Physics, stepped in `onFixedUpdate`.
8. **Saving settings.** Write `lighting_` to a small text file from a "Save" button in
   the debug UI and load it in `onInit`, so tuned values survive a restart.
9. **Moving sound sources.** Give a moving object a looping spatial sound and call
   `app.audio().setPosition(handle, position)` each frame, such as an engine on a
   circling drone.
10. **World-space labels.** Project a 3D position to the screen with
   `camera.viewProjection()` and draw a name tag there with the HUD batch.
