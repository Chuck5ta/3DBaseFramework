# Game3D — Tutorial: Making the Demo

This tutorial rebuilds the demo scene from an empty window, one feature at a time. It uses a
new class, `MyGame`, so the original `DemoGame` stays available for comparison. After
every step you can build and run the game and see the result.

**What you'll learn:** the `Game` hooks, meshes and the `MeshRenderer`, materials and
textures, lighting and fog, a fly camera driven by mouse, keyboard and gamepad, and
animation on the fixed timestep with interpolation.

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

> Every code listing in this tutorial was compiled (with warnings as errors) and run with
> Vulkan validation enabled, step by step. If something doesn't work, compare with
> `src/game/DemoGame.cpp`: the finished `MyGame` does exactly what `DemoGame` does; the
> code differs only in small details such as line order.

---

## Step 1 — An empty game

A game is a class derived from `core::Game`. `core::Application` owns the window, input
and GPU, runs the main loop and calls your class's hooks (`onInit`, `onUpdate`,
`onRender`, …). Every hook is optional; this first version only uses `onUpdate` to quit.

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
