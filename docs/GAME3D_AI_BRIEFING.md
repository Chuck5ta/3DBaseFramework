# Game3D — AI briefing

> **All projects:** how this project fits with the owner's other projects, and which
> briefings go in which Claude Project, is in `PROJECTS_AI_BRIEFING.md`
> (`F:\Pojects\Claude\`, uploaded to every Claude Project).

> **For the AI assistant.** This file is attached at the start of every chat about a game
> built on the Game3D framework. It is the working summary of the framework: what
> exists, how to use it correctly, the conventions, and how this user likes to work.
> Read it fully before writing code. For more depth, ask for `docs/GAME3D_ARCHITECTURE.md`
> (design and internals), `docs/TUTORIAL.md` (the demo built step by step) or
> `docs/BUILDING.md` (Windows, Linux, Steam Deck builds).
>
> Framework state: **2026-11-19**. If the user's copy of the framework is older or newer,
> trust the source code over this file.

## 1. What it is

An engine-free C++20 starter for **3D games** on Windows, Linux and Steam Deck. The game
owns all the code: a thin platform layer (SDL3), a Vulkan 1.3 renderer with a depth buffer,
a lit and textured forward renderer for meshes and **glTF models** (from Blender) with
their **animations** and **skinning**, **normal maps**, **instancing**, **frustum culling**, sun shadows, fly and third-person cameras, a 2D overlay for HUD text, 3D audio with effects and
Doppler, and a debug UI. A demo scene (`DemoGame`: crates, spheres, a sun that can be animated, a humming
crate, chiming spheres, a drone model circling with spinning rotors, an animated gunner
built from rigid parts who tracks it, fires and reloads, a skinned player soldier you
walk and run, and an instanced forest of 800 trees) shows how it fits together.

- **Language and build:** C++20, CMake ≥ 3.22 with presets (`debug`, `relwithdebinfo`,
  `release`, `vs2022`, `vs2026`). Every dependency is downloaded by CMake at a pinned
  version (`cmake/Dependencies.cmake`).
- **Platforms:** Windows (Visual Studio 2022/2026), Linux, Steam Deck (SteamOS, Mesa RADV).
- **Libraries:** SDL3 3.4.16 (window, input, gamepads), Vulkan 1.3 through volk, VMA 3.3.0
  (GPU memory), GLM 1.0.1 (maths), stb_image and stb_truetype (images, fonts), miniaudio
  0.11.23 (audio), Dear ImGui 1.92.9 (debug UI), nlohmann/json 3.12.0 (JSON), cgltf 1.15
  (glTF models; used by Game3D, fetched by both so `Dependencies.cmake` stays identical).
- **Shared code:** `core/`, `gfx/`, `audio/`, `debugui/` and `render2d/` are identical in
  Game2D and Game3D (this is Game3D; its sibling is Game2D). Game3D adds `render3d` and uses `render2d` for its HUD.

## 2. Project layout

```
src/
  main.cpp              AppConfig + run the game (swap DemoGame for your game here)
  core/                 Application (loop, hooks), Window, Input, Actions, GameState, SaveData, Paths, Log
  gfx/                  Vulkan: VulkanContext, Swapchain, Renderer, Buffer, Image,
                        Texture, Pipeline(Builder), Descriptors
  render3d/             MeshRenderer (Material, SceneLighting, ShadowSettings; sun shadows, GPU skinning, instancing, culling), Model (glTF, skins), ModelAnimator, Mesh (+ primitives),
                        Camera3D, Frustum, ThirdPersonCamera, Raycast
  render2d/             SpriteBatch, Camera2D, Font, Animation (used for the HUD)
  audio/                AudioEngine (miniaudio): sounds, music, 3D, effects, Doppler
  debugui/              DebugUI (Dear ImGui)
  game/                 DemoGame (the example); your game goes here
shaders/                mesh.vert/.frag, shadow.vert/.frag, mesh_skinned.vert, shadow_skinned.vert, skin.glsl,
                        instance.glsl, tangents.glsl, morph.glsl, common.glsl, sprite.vert/.frag, imgui.frag
assets/                 fonts/ (Roboto), models/ (drone, gunner, soldier, rifle, tree .glb), sounds/, textures/ (crate + its normal map) (copied next to the exe)
cmake/                  Dependencies.cmake, Shaders.cmake
tools/                  make_drone.py, make_gunner.py, make_tree.py (+ gltf_builder.py), make_gunner_sounds.py,
                        make_crate_normal.py: the demo's models, gunner sounds and crate normal map, in plain Python
docs/                   GAME3D_ARCHITECTURE, BUILDING, TUTORIAL, GAME3D_AI_BRIEFING (this file)
```

New `.cpp`/`.h` files anywhere under `src/` are picked up automatically
(`GLOB_RECURSE ... CONFIGURE_DEPENDS`); no CMake edits are needed. New shaders in
`shaders/` are compiled to `<exe>/shaders/<name>.spv` automatically. Everything in `assets/`
is copied next to the executable on every build.

## 3. A complete minimal game

This compiles and runs as is. It shows the shape every game follows: a `core::Game`
subclass with hooks, created in `main.cpp`.

**`src/game/MyGame.h`**

```cpp
#pragma once

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"
#include "render3d/MeshRenderer.h"

#include <memory>

class MyGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;
    void onDebugUI(core::Application& app) override;

private:
    std::unique_ptr<render3d::MeshRenderer> meshes_;
    render3d::Camera3D camera_;
    render3d::SceneLighting lighting_;
    render3d::Mesh cube_, ground_;
    gfx::Texture crate_;

    std::unique_ptr<render2d::SpriteBatch> hud_; // 2D overlay on top of the 3D scene
    render2d::Camera2D hudCamera_;
    render2d::Font font_;

    audio::SoundHandle hum_;
    float angle_ = 0.0f, prevAngle_ = 0.0f; // simulation state (fixed step)
};
```

**`src/game/MyGame.cpp`**

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>

void MyGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    meshes_ = std::make_unique<render3d::MeshRenderer>(gpu, app.renderer());
    cube_ = render3d::Mesh(gpu, render3d::primitives::cube(1.0f), "cube");
    ground_ = render3d::Mesh(gpu, render3d::primitives::plane(100.0f, 50.0f), "ground");
    crate_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/crate.png"));
    hud_ = std::make_unique<render2d::SpriteBatch>(gpu, app.renderer());
    font_ = render2d::Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf"));

    camera_.position = {0.0f, 2.0f, 6.0f};
    camera_.lookAt({0.0f, 0.5f, 0.0f});

    audio::PlayParams hum;
    hum.loop = true;
    hum.spatial = true;               // 3D: attenuated and panned from the listener
    hum.position = {0.0f, 0.5f, 0.0f};
    hum.minDistance = 2.0f;
    hum_ = app.audio().play(core::assetPath("sounds/hum_loop.wav"), hum);
}

void MyGame::onShutdown(core::Application&)
{
    meshes_.reset(); // GPU is idle here: safe to destroy GPU objects
    hud_.reset();
}

void MyGame::onFixedUpdate(core::Application&, float dt)
{
    prevAngle_ = angle_;
    angle_ += 0.8f * dt;
}

void MyGame::onUpdate(core::Application& app, float)
{
    if (app.input().keyPressed(SDL_SCANCODE_ESCAPE))
        app.quit();
    app.audio().setListener(camera_.position, camera_.forward(), camera_.up()); // every frame
}

void MyGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height));
    const float angle = glm::mix(prevAngle_, angle_, app.interpolationAlpha());

    meshes_->begin(frame, camera_, lighting_);
    meshes_->draw(ground_, glm::mat4(1.0f), {nullptr, {0.5f, 0.6f, 0.45f, 1.0f}});
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 0.0f});
    m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    meshes_->draw(cube_, m, {&crate_});
    meshes_->end();

    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f; // 1 unit = 1 pixel, (0,0) top-left
    hud_->begin(frame, hudCamera_.viewProjection());
    render2d::TextStyle text;
    text.size = 20.0f;
    text.snapToPixel = true;
    hud_->drawText(font_, "Hello 3D", {16.0f, 12.0f}, text);
    hud_->end();
}

void MyGame::onDebugUI(core::Application&)
{
    ImGui::Begin("Game");
    ImGui::DragFloat3("Camera", &camera_.position.x, 0.1f);
    ImGui::ColorEdit3("Fog", &lighting_.fogColor.x);
    ImGui::End();
}
```

**`src/main.cpp`**

```cpp
#include "core/Application.h"
#include "game/MyGame.h"

#include <SDL3/SDL_main.h> // provides the platform entry point; include in one file only

int main(int, char**)
{
    core::AppConfig config;
    config.window.title = "My Game";
    config.renderer.depthBuffer = true; // 3D needs depth
    core::Application app(config);
    MyGame game;
    return app.run(game);
}
```

## 4. The game loop and hooks (`core/Application.h`)

`core::Application` owns the window, input, GPU, renderer, audio engine and debug UI,
and runs the loop. A game is a subclass of `core::Game`; every hook is optional:

| Hook | When | Use it for |
|---|---|---|
| `onInit(app)` | once, GPU ready | load textures, fonts, sounds; create renderers |
| `onFixedUpdate(app, dt)` | 0..n times per frame at a fixed rate (60 Hz) | gameplay simulation, physics, timers |
| `onUpdate(app, dt)` | once per frame, real frame time | input, camera, UI state, audio listener |
| `onDebugUI(app)` | after `onUpdate`, only while the debug UI is visible | ImGui windows (the only place to call ImGui) |
| `onPreRender(app, frame)` | before the main render pass | the shadow pass (`beginShadows` … `endShadows`), uploads, off-screen passes, compute |
| `onRender(app, frame)` | inside the main pass (cleared, viewport set) | all drawing |
| `onResize(app, w, h)` | swapchain size changed (pixels) | resize render targets |
| `onShutdown(app)` | after `vkDeviceWaitIdle` | destroy GPU objects (reset `unique_ptr`s) |
| `onPause(app)` / `onResume(app)` | a game state that pauses the game was pushed / is gone | snap interpolation (`prev = current`), pause loops |

Frame order: events → actions update → game-state changes → `onFixedUpdate` × n → `onUpdate` → audio update → `onDebugUI` →
`onPreRender` → `onRender` → debug UI drawn on top → present.

**Fixed timestep with interpolation.** Simulate in `onFixedUpdate` and keep the previous
state. Render `glm::mix(prev, current, app.interpolationAlpha())` so motion is smooth at any
refresh rate. Read held input (movement) in `onUpdate` into a member and apply it in
`onFixedUpdate`; for one-off presses read in `onFixedUpdate` (a punch, a jump) use
`app.actions().consume("punch")`, so a press is never lost between steps or used twice.

**`core::Application`:** `window()`, `input()`, `actions()`, `states()`, `settings()`, `saveSettings()`, `resetSettings()`, `gpu()` (`gfx::VulkanContext&`),
`renderer()` (`gfx::Renderer&`), `audio()` (`audio::AudioEngine&`), `debugUI()`
(`debugui::DebugUI*`, **null in Release builds**), `config()`, `time()` (seconds since
start), `fps()`, `interpolationAlpha()`, `quit()`.

**Game states** (`core/GameState.h`, `app.states()`): title screens, pause menus, game-over
screens, as classes derived from `core::GameState`, on a stack **above** the `Game` (which
is always the bottom layer). Use them instead of `if (paused)` checks in the game.

```cpp
class PauseState : public core::GameState {
public:
    explicit PauseState(MyGame& game) : game_(game) { name = "Pause"; } // pausesBelow = true by default
    void onUpdate(core::Application& app, float) override {
        if (app.actions().pressed("pause")) app.states().pop();
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override { /* dim + menu */ }
private:
    MyGame& game_; // states usually reach the game's resources through a reference
};
// in MyGame::onUpdate:  if (app.actions().pressed("pause")) app.states().push<PauseState>(*this);
```

- Hooks: the `Game`'s (`onFixedUpdate`, `onUpdate`, `onPreRender`, `onRender`, `onResize`,
  `onDebugUI`) plus `onEnter`/`onExit` (added/removed; destroyed after `onExit`) and
  `onPause`/`onResume`. Settings (public members, set in the constructor): `pausesBelow`
  (default **true**: layers below get no `onFixedUpdate`/`onUpdate`), `hidesBelow` (default
  false: layers below aren't drawn), `name` (shown in the Engine panel's "Game states").
- Update and draw go bottom to top, from the lowest layer not paused/hidden by a state
  above. `onDebugUI` reaches every state. `timeInState()` = seconds active (not paused).
- `push(std::unique_ptr)` / `push<T>(args...)`, `pop()`, `replace<T>(args...)` (pop then
  push, no resume in between), `clear()`: **queued, applied at the start of the next frame**,
  so a state may pop itself from its own hooks. `onEnter`/`onExit` may queue more changes
  (applied in the same frame). Reading: `empty()`, `size()`, `top()`, `at(i)` (0 = bottom),
  `find<T>()`, `isActive(state)`, `gamePaused()`, `changesPending()`.
- When the stack changes, `Actions::clearPresses()` runs: a press that closed a menu can't
  also act below (incl. buffered `consume`).
- Rendering still interpolates with `interpolationAlpha()` while paused, which keeps
  changing: in `Game::onPause` (or a state's) set previous = current state, or the frozen
  picture shimmers.
- At shutdown (GPU idle): states `onExit` top first, then `Game::onShutdown`.
- To push a state from `onInit` (start on a title screen) is fine: it applies before the
  first update.
- Game2D tutorial step 14 / `DemoGame`: `Menu` (keys, pad, mouse), `MenuState` base, title,
  pause (muffles audio with a master-bus low-pass in `onEnter`/`onExit`) and result screens.

**`core::AppConfig`** (set in `main.cpp`): `window` (`title`, `width`, `height`,
`resizable`, `fullscreen`), `renderer` (`vsync`, `depthBuffer`, `clearColor` as **linear**
RGBA, `framesInFlight`), `audio` (see §9), `debugUI` (see §10), `fixedTimestep` (1/60),
`validation`, `showFpsInTitle`, `f11TogglesFullscreen` (also Alt+Enter), `organization` +
`application` (name the save folder; set in `main.cpp`, never change after release),
`rememberSettings` (default true).

**Settings and save files** (`core/SaveData.h`, `core/Paths.h`):

- **Save folder:** `core::saveFolder()` / `core::savePath("x.json")` = the player's writable
  folder from `SDL_GetPrefPath(organization, application)`: `%APPDATA%\<org>\<app>\` on
  Windows, `~/.local/share/<org>/<app>/` on Linux/Steam Deck. Env `GAME_SAVE_DIR` overrides.
  Never write next to the exe (Steam install folders may be read-only).
- **`core::SaveData`**: a JSON object in a file. `SaveData save("save.json")` (relative =
  in the save folder; absolute paths allowed), `load()` (false if no file / unreadable:
  data empty), `save()` (false on failure, logged), `exists()`, `erase()` (file + backup +
  data), `path()`. Values: `getBool/getInt/getFloat/getString(key, fallback)`,
  `set(key, bool|int|float|double|const char*|string_view)`, `has`, `remove`, `clear`;
  **dotted keys** nest (`"stats.rounds"` → `{"stats": {"rounds": ...}}`); missing keys and
  wrong types return the fallback. `json()` gives the `nlohmann::json` (include
  `<nlohmann/json.hpp>`) for lists/arrays. `toString()`/`parse(text)`.
- Writes are crash-safe: `<file>.tmp` → current file renamed to `<file>.bak` → temp renamed
  into place; `load()` falls back to `.bak` if the file is damaged. Floats are written
  short (`0.7`). Save progress **when it happens** (end of a level/round), not only at exit.
  Save slots = separate files (`"slot1.json"`...).
- **`app.settings()`** is the framework's `SaveData` for `settings.json`. With
  `rememberSettings`, at start-up `video.fullscreen`, `video.vsync`, `audio.master/sfx/music`
  are applied before the window/renderer/audio are created, `audio.muted` after, and
  `controls` (rebound actions only) after `Game::onInit`. At exit (after `onShutdown`) the
  current state is captured and saved. The game adds its own keys under `"game."`
  (`app.settings().set("game.showHelp", false)`). `app.saveSettings()` captures + writes now
  (call when leaving an options screen); `app.resetSettings()` restores config defaults and
  default bindings. Options screens just change the live systems
  (`audio().setMasterVolume`, `window().toggleFullscreen()`, `renderer().setVsync`).
- Debug UI → Engine → "Settings & saves": folder, Open folder, Save settings now, Reset settings.
- Steam Cloud: Auto-Cloud root `WinAppDataRoaming` + `<org>/<app>`, Linux root override
  `LinuxXdgDataHome`; no code needed.

**Renderer:** `app.renderer().setVsync(bool)`, `vsync()`, `setClearColor(linear vec4)`,
`extent()`, `colorFormat()`, `depthFormat()`, `framesInFlight()`, `waitIdle()`.
`gfx::FrameContext` (passed to the render hooks): `cmd`, `frameIndex` (index per-frame
resources with it), `imageIndex`, `frameNumber`, `extent`.

## 5. Input

### Input actions (`core::Actions`, `app.actions()`) — use these for gameplay controls

The game names what the player does and asks about the name, never about a key, so every
control is rebindable and on-screen prompts always match. Define in `onInit`:

```cpp
using core::Binding;
core::Actions& actions = app.actions();
actions.define("left", {Binding::key(SDL_SCANCODE_A), Binding::key(SDL_SCANCODE_LEFT),
                        Binding::axis(SDL_GAMEPAD_AXIS_LEFTX, -1), Binding::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});
// ...right, up, down the same way...
actions.define("punch", {Binding::key(SDL_SCANCODE_J), Binding::button(SDL_GAMEPAD_BUTTON_WEST)});
```

- `Binding::key(scancode)`, `Binding::mouse(SDL_BUTTON_LEFT)`, `Binding::button(SDL_GamepadButton)`,
  `Binding::axis(SDL_GamepadAxis, ±1)` (one direction of a stick, or a trigger with +1).
  `define` again replaces the defaults; names are case-sensitive strings.
- Reading: `down(name)`, `pressed(name)` (this frame; taps shorter than a frame count),
  `released(name)`, `value(name)` (0..1, analog for sticks/triggers),
  `vector("left", "right", "up", "down")` (length ≤ 1, **+Y down**; negate Y for 3D forward),
  `consume(name, bufferSeconds = 0.15f)` (buffered press for `onFixedUpdate`: true once if
  pressed within the buffer and not used yet). Unknown names warn once and read as idle.
- Updated once per frame after events and before `onFixedUpdate`, so `pressed` is true for
  one *frame*: in `onFixedUpdate` (which may run 0 or 2+ times per frame) use `consume`.
- Rebinding: `bindings(name)`, `defaultBindings(name)`, `setBindings`, `addBinding`,
  `removeBinding`, `resetToDefaults(name)`, `resetAllToDefaults()`, `actionsUsing(binding)`
  (clashes; sharing is allowed), `names()` (definition order). `capture()` returns the first
  input pressed this frame (`std::optional<Binding>`) for "press a key" screens; while
  waiting, the game should ignore its own controls, and Esc conventionally cancels.
- Saving: `saveBindings()` → JSON text `{"left": ["key:A", "key:Left", "axis:leftx-", "pad:dpleft"], ...}`;
  `loadBindings(text)` → false if unreadable; unknown actions/inputs are skipped.
  `Binding::toString()/fromString()` for single bindings. (No settings file yet: store the text.)
- Prompts: `lastDevice()` (`core::InputDevice::KeyboardMouse` or `Gamepad`, whichever was
  touched last), `prompt(name)` → that device's first binding as text ("Space", "Left
  Arrow", "A", "Cross", "Left Stick Up", "RB", "LT"), `displayName(binding)`. Pad names
  follow the connected pad: Xbox and Steam Deck A/B/X/Y, PlayStation Cross/Circle/
  Square/Triangle (L1, R2...), Nintendo labels. List the stick before the d-pad in
  defaults if the stick should be the one shown. `pressThreshold` (0.5) = how far a stick
  must move to count as down.
- The Game2D demo (tutorial step 13) shows a complete rebinding window in the debug UI.

### Raw input (`core::Input`, `app.input()`) — for things that aren't actions

The mouse wheel and cursor, editor hot keys, and anything needing a specific device.

- Keys by **scancode** (layout-independent): `keyDown`, `keyPressed`, `keyReleased`
  (`SDL_SCANCODE_W`, ...). Presses are latched, so short taps are never missed.
- Mouse (framebuffer **pixels**): `mouseDown/Pressed/Released(SDL_BUTTON_LEFT)`,
  `mousePosition()`, `mouseDelta()`, `mouseWheel()`, `setRelativeMouseMode(app.window().handle(), on)`.
- Gamepad (first connected; Steam Deck controls arrive as a gamepad through Steam Input):
  `hasGamepad()`, `gamepadName()`, `gamepadDown/Pressed/Released(SDL_GAMEPAD_BUTTON_SOUTH ...)`,
  `leftStick()`/`rightStick()` in [-1, 1] with **+Y down** and a radial dead zone,
  `leftTrigger()`/`rightTrigger()` in [0, 1], `rumble(low, high, ms)`, `stickDeadZone`.
- SOUTH = A (Xbox) / cross, EAST = B, WEST = X, NORTH = Y; BACK = View/Select.
- While the debug UI has the mouse over a window or a text field focused, those presses
  don't reach `Input` (releases always do).

## 6. Textures, files and logging

- `gfx::Texture::fromFile(app.gpu(), core::assetPath("textures/x.png"), desc)` (PNG, JPG,
  TGA, BMP; aborts with a message if missing), `fromPixels(gpu, w, h, rgba, desc)`,
  `solid(gpu, r, g, b, a)`. Move-only; `width()`, `height()`, `size()`, `valid()`.
- `gfx::TextureDesc`: `srgb` (true for colour images; false for data such as normal maps),
  `mipmaps`, `filter` (`VK_FILTER_NEAREST` for pixel art), `wrap`
  (`VK_SAMPLER_ADDRESS_MODE_REPEAT` / `CLAMP_TO_EDGE`; both directions), `wrapV` (a
  different vertical wrap; default = same as `wrap`), `debugName`.
- Paths: `core::assetPath("sounds/x.wav")` → `<exe>/assets/sounds/x.wav`,
  `core::shaderPath("x.frag.spv")`, `core::basePath()`. Never rely on the working directory.
- Logging (`core/Log.h`): `LOG_INFO/LOG_WARN/LOG_ERROR/LOG_DEBUG(printf-style)`;
  `core::fatal(fmt, ...)` logs, shows a message box and aborts.

## 7. 3D rendering (`render3d`)

**World space:** right-handed, **+Y up**, +X right; the default camera looks along **−Z**.
Units are free; the demo and the audio defaults treat 1 unit as 1 metre. Triangles are
**counter-clockwise** from the front.

**`render3d::Camera3D`:** `position`, `yaw` (radians; −π/2 looks along −Z), `pitch` (clamp
to about ±1.55), `fovY`, `nearPlane`, `farPlane`. `setAspect(w / h)` every frame,
`lookAt(target)`, `forward()`, `right()`, `up()`, `view()`, `projection()` (Vulkan: depth 0..1,
Y flipped), `viewProjection()`.

**`render3d::Mesh`:** `Mesh(app.gpu(), MeshData, "name")`; `MeshData` = `vertices` (`position`,
`normal`, `uv`, `color` = linear vertex colour, white by default, `tangent` = vec4 for normal
maps, xyz along +u, w = ±1, all zero = none; 64 bytes) + `indices` (uint32). Primitives:
`primitives::cube(size)`, `plane(size, uvRepeat)` (XZ plane facing +Y), `sphere(radius,
segments, rings)`, all with tangents. For your own `MeshData` with a normal map, call
`render3d::generateTangents(data)` (`render3d/Tangents.h`) before making the `Mesh`.

**`render3d::MeshRenderer`** (`MeshRenderer(app.gpu(), app.renderer(), shadowMapSize = 2048, maxJoints = 8192, maxInstances = 16384, maxMorphWeights = 65536)`,
needs `config.renderer.depthBuffer = true`):
`begin(frame, camera, lighting)` → `draw(mesh, modelMatrix, Material)` and/or
`draw(model, transform, &pose)` × n → `end()` (see-through model parts are drawn in `end()`,
farthest first). It stops with a message if the renderer has no depth buffer.
`draw(model, transform, pose, std::span<const glm::vec4> materialColors)` replaces the
model's base colours: one **linear** RGBA per material, in `model.materials()` order (a
shorter or empty span keeps the file's colours for the rest; see-through parts keep their
override when sorted). One `Model`, many colour schemes: CharacterCreator's recoloured
parts, team colours. It came from CharacterCreator (2026-11-17).
`draw(model, transform, pose, materialColors, std::span<const float> morphWeights)` and
`drawInstanced(model, matrices, pose, morphWeights)` set the model's **morph target (shape
key) weights** for that draw (see Morph targets below).
`Material{texture, color (sRGB tint), emissive (sRGB glow), emissiveStrength,
emissiveTexture, normalTexture, normalScale = 1, roughness = 0.5, metallic = 0, doubleSided,
castShadows = true}` (load a normal map with `TextureDesc::srgb = false`).
`setNormalMaps(bool)` / `normalMaps()` (all normal maps on/off, to compare), `setWireframe(bool)`, `wireframe()`, `wireframeSupported()`, `drawCalls()`,
`shadowDrawCalls()`, `shadowMapSize()`, `shadowTexelSize()` (metres), `jointsUsed()`,
`maxJoints()`, `instancesUsed()`, `maxInstances()`, `morphWeightsUsed()`,
`maxMorphWeights()`, `triangles()`, `shadowTriangles()`.
Per-object data travels
as push constants (one draw call per object or model part); the block is 128 bytes, the
guaranteed maximum, and **completely full** (morph targets took `surface.yzw`, as integers
bit for bit): a new per-draw value must go in a buffer, found by an index already pushed. Build model matrices with
`glm::translate/rotate/scale` (`<glm/gtc/matrix_transform.hpp>`).

**Instancing** — many copies of one mesh or model in one draw call per part:
`drawInstanced(mesh, matrices, Material)` and `drawInstanced(model, matrices, &pose)`, where
`matrices` is a `std::span<const glm::mat4>` (a `std::vector<glm::mat4>` or a C array
converts). Use it for anything repeated (trees, rocks, crates, crowds); keep `draw()` for
one-offs.

- Matrices go into a per-frame instance buffer (storage buffer, set 0 binding 3,
  `maxInstances` × 64 B, 16384 = 1 MB per frame); the vertex shaders (`instance.glsl`,
  included by all four) compute `instances[gl_InstanceIndex] * object.model`. Entry 0 is
  the identity and single `draw()`s use it (`firstInstance = 0`): no extra pipelines.
- Instanced model parts: push constant `model` = the part's node matrix in model space
  (identity for meshes and skinned parts); the instance matrix places the copy.
- Copies share the material and the pose (skinned: joints uploaded once per call). Mirrored
  copies (negative determinant) are sorted last and drawn as a second draw with the
  opposite winding. Blend parts are queued per copy for `end()`'s sorted pass.
- Both passes append (each call costs its copy count twice with shadows); overflow skips
  the call with one warning. Only copies that pass frustum culling are uploaded.

**Normal maps** — `Material::normalTexture` (+ `normalScale`) or a glTF material's normal
texture. Tangent space, OpenGL/glTF/Blender convention (green = up the image). Set 1
binding 2 (a flat (0.5, 0.5, 1) texture when there's none); push constant
`surface.x` = strength (0 = off; also off when the vertex has no tangent).

- Tangents: `Vertex::tangent` (xyz, w = ±1), from MikkTSpace (`render3d/Tangents.h`,
  library fetched in Game3D's own `CMakeLists.txt`, not the shared `Dependencies.cmake`)
  with UV v fed as `1 - v`; or from the glTF file.
- Shaders (`tangents.glsl`): per vertex `N = normalize(normalMatrix · n)`,
  `T = normalize(mat3(model) · t)`, `B = t.w · sign(det(model)) · cross(N, T)`; per pixel
  `N' = normalize(m.x·T + m.y·B + m.z·N)` with the interpolated vectors not renormalised,
  `m = texel·2 − 1` with xy × strength (the MikkTSpace pixel transform, so maps baked by a
  MikkTSpace baker, such as CharacterCreator's planned one, line up). Skinned meshes bend
  the tangent with the same skin matrix. Shadow lookups use the surface's own normal.
- Demo: the crates (`assets/textures/crate_normal.png` from `tools/make_crate_normal.py`);
  debug UI *Normal maps* checkbox next to *Wireframe*.

**Frustum culling** — automatic, nothing to call. `begin()` builds a `render3d::Frustum`
from the camera, `beginShadows()` from the shadow map's box (which reaches 100 m towards the
sun, so off-screen casters still cast). Each `draw(mesh)` is tested by `Mesh::boundsMin/Max`
× its matrix; each rigid part of a model by its primitive's box × node matrix; a skinned
node by `Model::skinBounds(skin, joints, min, max)` (per-joint bind-pose boxes moved by the
joint matrices, computed in `Model::fromData`); each copy of a `drawInstanced` by one box
around the whole posed model (or the mesh) × the copy's matrix, before upload. Blend parts
are culled before they are queued.

- The test is an oriented box against the six planes (conservative only at corners): the
  picture is identical with culling off (checked pixel for pixel on six views, 2026-11-17).
- `setFrustumCulling(bool)` (on by default), `setCullingFrozen(bool)` (keeps both frustums
  of that moment: fly out and look), `culled()` / `shadowCulled()` (meshes, parts, skinned
  nodes and copies left out).
- `render3d::Frustum(viewProjection)` is public: `intersects(min, max, matrix)`,
  `intersectsSphere(centre, radius)`, `planes()`; default-constructed = contains all. Use
  it to skip game work off screen.
- No spatial structure: every object is tested every pass (~15 ns per box, optimised).
  A vertex shader that moves vertices beyond the mesh's box would need bigger bounds.

**Shadows (sun only)** — the scene is drawn twice through the same calls, so put all 3D
drawing in one function (the demo's `drawScene(alpha)`):

```cpp
void MyGame::onPreRender(core::Application& app, gfx::FrameContext& frame)
{
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height)); // before the fit
    meshes_->beginShadows(frame, camera_, lighting_); // draw() now writes depth from the sun
    drawScene(app.interpolationAlpha());
    meshes_->endShadows();
}
void MyGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    meshes_->begin(frame, camera_, lighting_);        // same camera and lighting
    drawScene(app.interpolationAlpha());
    meshes_->end();
    // ...HUD...
}
```

- One 2048² shadow map (D32) fitted to a sphere around the view out to
  `lighting.shadows.distance`, moved in whole texels: no shimmering while the sun is
  still. Changing `sunDirection` every frame makes edges crawl, so the demo's sun
  animation (`animateSun_`) defaults to off. ~3.5 cm per texel at
  30 m (16:9). Shadows fade out over the last 20 % of the distance. Casters up to 100 m
  towards the sun beyond that sphere still cast (a high drone).
- `ShadowSettings{enabled = true, distance = 30 m, strength = 1, softness = 1,
  normalBias = 1, depthBias = 1.5}` in `SceneLighting::shadows`; softness and biases are in
  **texels**. Acne (stripes) → raise the biases; shadows detached from casters → lower them.
- Skip the shadow pass (or `enabled = false`) and the frame renders without shadows.
  `castShadows = false` for the ground, glows and effects; Blend model parts never cast;
  alpha-masked parts cut holes in the shadow. Shadows dim the sun only (ambient stays).
- Shadow pipeline: depth only (a colour attachment of format `UNDEFINED`, null view), no
  culling, same layout and sets as the main pass; shaders `shadow.vert`/`shadow.frag`.
  `mesh.frag` reads the map at set 0, binding 1 (`sampler2DShadow`, 3×3 filtered lookups).

**`render3d::Model`** (`render3d/Model.h`): glTF 2.0, `.glb` (one file, preferred) or
`.gltf` (+ `.bin` + images beside it), e.g. Blender's *File > Export > glTF 2.0*.

```cpp
// onInit
drone_ = render3d::Model::load(app.gpu(), core::assetPath("models/drone.glb")); // fatal with a reason if unusable
pose_ = drone_.defaultPose();                 // one ModelPose per copy on screen
rotor_ = drone_.findNode("RotorFL");          // a node = an object in Blender's Outliner; -1 + warning if missing
// onRender, between begin() and end()
pose_[rotor_].rotation = glm::angleAxis(angle, glm::vec3(0, 1, 0)); // relative to its parent; pose[-1] is ignored
meshes.draw(drone_, transform, &pose_);       // pose optional (nullptr = as exported)
```

- Front is **+Z**, up +Y, metres (Blender's −Y front, converted by the exporter). A node
  turns about its own origin (in Blender: *Set Origin > Origin to Geometry* for rotors).
- `NodeTransform{translation, rotation (glm::quat), scale}`; `node(i)` gives `name`,
  `parent`, `mesh` and the default transform; `nodeCount()`.
- Bounds of the default pose: `boundsMin()`, `boundsMax()`, `size()`, `center()`.
- Materials used: base colour (factor × texture × vertex colour), emissive (×
  `KHR_materials_emissive_strength`, × texture), normal texture + scale
  (`ModelMaterial::normalTexture`, `normalScale`), metallic and roughness factors, alpha
  modes (opaque, mask + cutoff, blend), double-sided, `KHR_materials_unlit`; per-texture
  filter and per-axis wrap. Ignored with a logged warning: metallic-roughness and
  occlusion textures, morph target animations and tangent offsets, cameras, lights.
- Normal-mapped parts get tangents at load: the file's `TANGENT`, or MikkTSpace
  (`generateTangents`, with UV v flipped to count up as Blender does; verified identical
  to Blender's tangents on Khronos' NormalTangentMirrorTest, 2026-11-17). Vertices needing
  two tangents are copied (with their skin data). Textures used as normal maps are
  uploaded linear.
- **Morph targets (shape keys)**, added 2026-11-19 for CharacterCreator's body sliders
  (built here first, then copied there):
  - Loaded: each primitive's `POSITION` and `NORMAL` target offsets (sparse too) into
    `ModelData::morphDeltas` (`MorphDelta{vec3 position; vec3 normal}`, 24 B), each vertex's
    targets side by side; `ModelPrimitive::morphTargets` (count) and `morphFirst`. Vertices
    the loader makes (flat-shading corners, tangent-split copies) take their source
    vertex's offsets. `ModelMesh::targetNames` (Blender's `extras.targetNames`, else
    "target N"), `defaultWeights` (`mesh.weights`), `targetMin/Max` (each target's offset box).
  - **Weights are numbered model-wide**: each node with a morphed mesh owns a run from
    `ModelNode::firstMorphWeight` (in its mesh's target order), at the node's `weights`,
    else the mesh's, else 0. `model.defaultMorphWeights()`, `morphWeightCount()`,
    `findMorphTarget("Belly")` / `findMorphTarget(node, name)` (-1 + one warning listing the
    names), `morphTargetName(i)`. Bounds of the default pose include the default weights.
  - Drawing: keep a `std::vector<float>` per character (start from
    `defaultMorphWeights()`), set entries, pass it as `morphWeights` (empty or shorter span =
    file weights for the rest). Weights live in the draw, not the `ModelPose` (the animator
    rebuilds poses each update), so one `Model` shows many body shapes at once.
  - GPU: per morphed node with any weight ≠ 0, its weights go into the frame's weight buffer
    (set 0 binding 4, `maxMorphWeights` floats, reset with the joints; both passes append;
    overflow draws unmorphed with one warning); the model's offsets are a storage buffer in
    set 1 binding 3 (a 24-byte empty buffer for everything else); push constants
    `surface.y/z/w` = first weight, target count (0 = off), base (`morphFirst −
    vertexOffset·count`; the shader indexes `base + gl_VertexIndex·count + t`).
    `morph.glsl`'s `applyMorphs` adds `Σ w·offset` to position and normal **before
    skinning and instancing**, in all four vertex shaders (shadows too). Weight-0 targets
    are skipped (uniform branch). The vertex stage now uses exactly 4 storage buffers, the
    Vulkan minimum limit.
  - Culling: per draw each target's box × its weight (negative flips) is added to rigid
    parts' boxes and to every joint box (`skinBounds(..., morphLo, morphHi)`): identical
    picture with culling on or off.
  - Not done: tangent offsets (normal-mapped morphed meshes keep the unmorphed tangent
    frame), weight animation channels, per-copy weights in `drawInstanced`.
  - Writing them from Python: `tools/gltf_builder.py` (identical in CharacterCreator) takes
    `Mesh.targets` (one (position offsets, normal offsets) pair per key, per vertex) and
    `Builder.mesh(name, parts, target_names=[...], extras={...})`; every part of a mesh needs
    the same key count. CharacterCreator's MakeHuman bodies have 13 keys (2026-11-19).
  - Checked 2026-11-19 (cloud container, lavapipe, validation on): Khronos
    AnimatedMorphCube, MorphPrimitivesTest, MorphStressTest (negative weights), a split-
    tangent/flat-shaded test mesh, and CharacterCreator's MakeHuman body (14,517-vertex
    skin + eyes, 56 bones) with 10 test targets, posed, shadowed, single and instanced:
    every picture pixel-identical to the same model morphed on the CPU; all-zero weights
    identical to the body without targets. The owner built and ran it in Visual Studio 2022
    the same day: all fine.
- `render3d::loadModelData(path, data, &error)` loads into `ModelData` on the CPU only
  (tools and tests), returning false with a reason instead of stopping.
- One `Model` in memory can be drawn many times with different transforms and poses.
- `Game3D/assets/models/drone.glb` (CC0, ~1.3 m, nodes `Body`, `Frame`, `Lights`, `Eye`,
  `RotorFL/FR/BL/BR`) is the demo's; tutorial step 12 shows it. Its generator is
  `tools/make_drone.py`.
- **Sockets:** `glm::mat4 nodeMatrix(int node, const glm::mat4& transform, const ModelPose*
  pose = nullptr)` = one node's world matrix (node -1 → `transform`). Draw another model
  with it to attach it (weapons, hats); its own nodes (a `Muzzle`) chain on from there.
- `animations()` (`ModelAnimation{name, duration, channels}`; `apply(time, pose)` writes the
  animated nodes), `findAnimation(name)` (-1 + one warning listing the names).

**`render3d::ModelAnimator`** (`render3d/ModelAnimator.h`): plays a model's animations (one
per Blender Action); mirrors the 2D `Animator`. One per animated object.

```cpp
// onInit
gunner_ = render3d::Model::load(app.gpu(), core::assetPath("models/gunner.glb"));
anim_.play(gunner_, "Idle");                                     // AnimationLoop::Loop by default
// a key press
anim_.play(gunner_, "Fire", render3d::AnimationLoop::Once, true); // restart = true: rapid fire
// onUpdate
anim_.update(dt);
if (anim_.isPlaying("Fire") && anim_.reached(0.0f)) ...          // a moment: true on the update that passed it
if (anim_.finished()) anim_.play(gunner_, "Idle");               // a Once animation ended (holds its last pose)
anim_.pose()[torso].rotation = aim * anim_.pose()[torso].rotation; // optional tweaks after update (rebuilt each update)
// onRender
meshes.draw(gunner_, transform, &anim_.pose());
meshes.draw(rifle_, gunner_.nodeMatrix(socket, transform, &anim_.pose()));
```

- `render3d::AnimationLoop` = `Loop`, `Once`, `PingPong`. `play` of the animation already
  playing does nothing unless `restart`; an unknown name warns and keeps the current one.
- `blendTime` (0.15 s; 0 = cut) cross-fades from the pose at the moment of the change;
  `speed` (1; 0 = paused); `stop()` blends to the default pose.
- `playing()`, `isPlaying(name)`, `animationName()`, `finished()`, `time()`, `length()`.
- `pose()` is empty until the first `play()`; pass `nullptr` or skip drawing until then if
  that matters (the demo plays Idle in `onInit`).
- Keeps a pointer to the `Model`: keep the model alive and in place (a member).
- Animations move **nodes**: rigid parts, armature bones (which are nodes), and through
  the bones, skinned meshes. One animation per animator (plus cross-fades).

**Skinned meshes** (Blender: a mesh with an Armature modifier, parented *With Automatic
Weights*) are bent on the GPU automatically: load and draw them like any model, with a
`ModelAnimator` pose. Nothing else to call.

- Per vertex up to 4 joints + weights (`JOINTS_0`/`WEIGHTS_0`; `JOINTS_1` ignored with a
  warning). `ModelSkin{name, joints (node indices), inverseBind}`, `model.skins()`,
  `node(i).skin` (-1 = none), `ModelPrimitive::skinned`, `ModelData::skinVertices`
  (`SkinVertex{u16vec4 joints; vec4 weights}`, parallel to `vertices`).
- `draw(model, transform, &pose)`: per skinned node, joint matrix = node matrix (model
  space) × inverse bind; appended to the frame's joint buffer (storage buffer, set 0
  binding 2); `material.w` push constant = first joint; `mesh_skinned.vert` /
  `shadow_skinned.vert` blend them (`skin.glsl`). The skinned node's own transform is
  ignored (glTF rule); `transform` places the model.
- Both passes append, so each skinned draw costs its joint count twice with shadows;
  `maxJoints` (8192) is per frame; overflow skips draws with one warning. Bounds are of
  the bent default pose.
- Unusable skins (joints outside the scene, joint indices beyond the skin) draw unbent
  with a warning. Tested with Khronos RiggedSimple, RiggedFigure, CesiumMan, Fox.
- Blender export: *Data > Skinning* on, *Include All Bone Influences* off (4 per vertex);
  apply scale. Linear blend skinning: extreme twists lose volume (no dual quaternions).
- `Game3D/assets/models/gunner.glb` (CC0, 1.9 m soldier, 296 triangles): nodes `Gunner >
  Hips > Torso > Head > Helmet`, `UpperArm/Forearm/Hand_L/R`, `Thigh/Shin_L/R`,
  `Socket_Hand_R` (rifle goes here); animations `Idle` (4 s loop), `Fire` (0.45 s), `Load`
  (1.5 s; magazine out at 0.3 s, in at 1.0 s), `Walk` (1 s loop, made for ~1.35 m/s) and
  `Run` (0.6 s loop, ~3.4 m/s; play at `speed = actualSpeed / that` so the feet don't
  slide). The arms keep the rifle ready in every animation. `rifle.glb` has a `Muzzle` node.
  `soldier.glb` (1128 triangles) is the same soldier **skinned**: the same nodes (root
  `Soldier`), animations and socket, plus `Body`, one mesh bent by a 14-joint skin (smooth
  tube limbs; bind pose: straight, arms hanging 12° out; 5 draw calls vs the gunner's 19).
  Swap one for the other with no code changes beyond the file name. Generated
  by `tools/make_gunner.py` (arm poses solved with a small IK solver; `gltf_builder.py`
  writes skins); sounds
  `rifle_shot.wav`, `mag_click.wav` by `tools/make_gunner_sounds.py`. Tutorial steps 13
  (gunner) and 16 (skinned soldier).
- `tree.glb` (CC0, `tools/make_tree.py`): a low-poly pine, 4.2 m, 148 triangles, one node
  `Tree` with two parts (`Bark`, `Needles`); the demo's forest (tutorial step 17).
- **Blender workflow, tested by Claude through the Blender MCP connection (Blender 5.2.3,
  2026-11-14):** build characters from separate objects (same names and hierarchy, origins
  at the joints). Recommended: bone-parent them to an armature (Ctrl+P > Bone), one Action
  per animation; the export has the bones as nodes with the parts as children, plus an
  unused `skins` entry that the loader ignores (0 warnings), and played correctly in the
  viewer. Without an armature, every part must share **one Action per animation** (Blender
  4.4+ slots); keying parts separately gives one Action each, exported as separate
  animations. Export *Mode: Actions* (default). **Not NLA Tracks:** re-exporting
  the imported gunner that way put Fire 205 mm out and the rest pose 23 mm out (other
  tracks mixed in), even with the active actions cleared; Actions mode matched (rest
  exact, Idle within 2 mm). Keep *Always Sample Animations* on (off kept the key times
  but moved a rest pose 24 mm). Blender rounds keys to whole frames (`make_gunner.py`'s
  0.45 s Fire became 0.417 s), so sub-frame keys don't survive a round trip. Frame 1
  exports as 1/24 s unless *Set All glTF Animation Starting at 0* is ticked. Importing
  `gunner.glb` gives the 16 objects, the `Socket_Hand_R` empty and the actions Idle, Fire
  and Load, each shared by the parts it moves. Sockets are empties (`Socket_Hand_R`).
- **Testing through Blender:** the owner's Blender MCP add-on runs Python inside his
  Blender 5.2.3 (`H:\Program Files\Blender Foundation\Blender 5.2`); Steam's Blender 5.3.0
  is also installed. Work in a new scene (`bpy.data.scenes.new`) so his open file is left
  alone, write test files to `tempfile.gettempdir()`, and save a test .blend with
  `bpy.data.libraries.write` (doesn't rename his session). The background tool
  (`execute_blender_code_for_cli`) failed on 2026-11-15 ("Blender executable not found at
  'blender'"): its process doesn't see `BLENDER_PATH`; use the live-session tool as above.
  Binary results come back as
  base64 in `result`; pad it past the size limit and the tool saves it to a file you can
  decode. To check an export, load both files with `loadModelData` and compare every
  node's world matrix at many times.

**`render3d::ThirdPersonCamera`** (`render3d/ThirdPersonCamera.h`): follows a target from
behind, orbits, zooms, keeps out of walls. Per frame, **after** moving the player:

```cpp
follow_.orbit(in.mouseDelta().x * 0.0025f, -in.mouseDelta().y * 0.0025f); // when the mouse is captured
follow_.zoom(in.mouseWheel());                       // + = 15% closer per step
follow_.update(playerPos_, dt, [this](const render3d::Ray& r) { return raycastScene(r); });
follow_.apply(camera_);                              // position, yaw, pitch into the Camera3D
// move the player relative to the view:
glm::vec3 wanted = follow_.flatForward() * input.y + follow_.flatRight() * input.x;
```

- Settings (public): `yaw`/`pitch` (Camera3D convention: view direction; negative pitch
  looks down), `distance` (1.5..12), `targetOffset` (feet → look-at, default 1.55 m up),
  `shoulder` (0.45 m right; 0 = centred), `minPitch`/`maxPitch`, `followSharpness` (12; 0 =
  rigid), `zoomSharpness`, `returnSharpness` (4), `collisionRadius` (0.25 m).
- `snapTo(target)` places it with no easing (spawn, teleport, switching to this camera);
  the first `update` does it too. `blocked()`, `currentDistance()`, `position()`,
  `lookAtPoint()` for HUD/debug.
- Collision: the optional ray function returns the nearest hit distance (or nothing); the
  camera casts five rays backwards (a fake sphere cast) plus one sideways for the
  shoulder. Blocked → jumps in at once; clear → eases out. No function = no collision.
- Not built in: fading the player when the camera is very close, auto-swinging behind a
  running player, a character controller (the demo pushes its player out of circles).

**`render3d/Raycast.h`:** `Ray{origin, direction (normalised)}`, `ray.at(d)`;
`raycastPlane(ray, point, normal)`, `raycastSphere(ray, centre, r)`,
`raycastBox(ray, modelMatrix, halfExtents = 0.5)` → `std::optional<float>` distance. Pass a
box the **same matrix it's drawn with** (the default half-extents fit `primitives::cube(1)`).
Rays starting inside a sphere/box don't hit it. Typical ray function: the nearest of
`raycast*` over the level's colliders (see `DemoGame::raycastScene`). Also for shooting,
line of sight and mouse picking.

**Demo (tutorial steps 14–17):** **G** toggles shadows (the debug UI has a Shadows
section). Starts in third person behind a second soldier (`soldier_`: `soldier.glb`,
skinned, own `ModelAnimator`; the gunner stays rigid for comparison); **C** / gamepad
Start toggles the fly camera. WASD/left stick
walk (camera-relative), Shift/L3 run, right mouse/Tab/right stick orbit, wheel/d-pad zoom.
Shared layout helpers (`bigCrateMatrix`, `ringCrateMatrix`, `sphereCentre`) feed drawing,
the player's push-out circles and the camera's `raycastScene`, so they can't disagree.
The forest: 800 tree matrices (`trees_`), scattered once in `onInit` between 22 and 90 m
with a small xorshift (`random01`, the same on every compiler), drawn with one
`drawInstanced`; the ring of crates is one `drawInstanced` too. The trunks push the player
out, and trunk and crown boxes (`kTrunkBox`, `kCrownBox` × the tree's matrix) stop the
camera (only trees within 15 m of the ray are tested). Debug UI → *Forest*: tree count
slider and an *Instancing* checkbox (off: one `draw()` per tree, 1647 draw calls instead
of 49). Debug UI → *Culling*: *Frustum culling* and *Freeze* checkboxes and the culled
counts; the top lines show draw calls and triangles for both passes. Looking out from the
clearing, about 700 of the 812 copies are culled (17–24k triangles instead of 135k).

**`render3d::SceneLighting`** (all colours sRGB): `sunDirection` (the direction light
travels), `sunColor`, `sunIntensity`, `skyColor` and `groundColor` (hemisphere ambient),
`ambientIntensity`, `fogColor`, `fogDensity` (0 = no fog). Set the renderer's clear colour
to the fog colour **converted to linear** so distant objects fade into the sky.

**HUD on top of 3D:** a `render2d::SpriteBatch` can draw inside the 3D pass (it ignores
depth); draw it after `meshes.end()` with a 1:1 `Camera2D` (see the minimal game).

## 8. HUD sprites and text (`render2d`)

**`render2d::SpriteBatch`** (`SpriteBatch(app.gpu(), app.renderer())`): batched textured
quads, one draw call per run of the same texture.

- `begin(frame, camera.viewProjection())` … `end()`. Several begin/end pairs per frame are
  fine (world, then HUD).
- `draw(texture, Sprite)`; `draw(texture, position, scale = 1, rotation = 0, color)` (native
  size, centred); `drawRect(topLeft, size, color)`; `drawRectOutline(topLeft, size,
  thickness, color)`; `drawLine(a, b, thickness, color)`;
  `drawText(font, utf8, topLeftOfFirstLine, TextStyle)` (returns the size drawn).
- `Sprite`: `position` (of the pivot), `size`, `origin` (pivot, normalised: 0,0 = top-left,
  0.5 = centre), `rotation` (radians, clockwise on screen), `color`, `uv`.
- `UvRect::fromPixels(texture, x, y, w, h)` for sprite-sheet frames; `.flippedX()`,
  `.flippedY()`.
- Draw order = call order (no depth sorting). `stats()` gives sprites and draw calls.
- `render2d::Color` is `glm::vec4`, **sRGB** 0..1 like an image editor; `colors::White`,
  `Black`, `Red`, `Green`, `Blue`, `Yellow`.

**`render2d::Camera2D`:** `position` (world point at the screen centre), `zoom` (>1 zooms
in), `rotation`, `referenceHeight` (if > 0, always shows that many world units vertically,
so 720p, Deck 800p and 4K look the same). Call `setViewport(w, h)` each frame from
`frame.extent`. `viewProjection()`, `screenToWorld(pixels)`, `worldToScreen(world)`,
`visibleBounds(min, max)` (for culling), `scale()` (pixels per world unit).
**HUD camera:** `referenceHeight = 0` and `position = viewport * 0.5f` gives 1 unit =
1 pixel with (0, 0) at the top-left.

**`render2d::Font`:** `Font::load(app.gpu(), core::assetPath("fonts/Roboto-Medium.ttf"),
FontDesc)` bakes characters into an atlas with stb_truetype (default 48 px, printable
ASCII + Latin-1; UTF-8 strings; characters not baked show as `?`). `measure(text, size)`,
`lineHeight(size)`, `ascent(size)`, `texture()`. Text is sharpest at or below the baked size.
`TextStyle`: `size` (world units; pixels with the HUD camera), `color`, `align`
(`TextAlign::Left/Center/Right` relative to the position), `lineSpacing`, `snapToPixel`
(use with the HUD camera). `'\n'` starts a new line. The project ships Roboto Medium
(Apache 2.0).

**Animation** (`render2d/Animation.h`):
- `render2d::SpriteSheet`: `SpriteSheet::loadAseprite(app.gpu(), core::assetPath("sprites/x.json"))`
  reads Aseprite's *Export Sprite Sheet* JSON (Array or Hash; image next to it; export
  **untrimmed**): each tag becomes a clip with its frames' durations; *Ping-pong* tags
  ping-pong, *Reverse* play backwards, *Repeat 1* plays once; no tags = one "default" clip.
  **Sprites from 3D models:** *SpriteCreator*, in the separate Game Developer Tools project
  (`blender/SpriteCreator/`; drag a `.blend` onto `SpriteCreator.bat`), renders a Blender
  animation into this format:
  timeline markers name the clips (`hover`, `explode:once`, `bob:pingpong`), an automatic
  orthographic camera fits every frame (`--view front|top|iso|dimetric|scene`), `--size N`,
  `--pixel-art`, `--still` for icons. Output `sprites/<name>.png/.json` beside the .blend.
  `SpriteSheet::loadGrid(gpu, path, {w, h}, count = 0)` splits an image into equal frames
  (left to right, top to bottom); add clips with `addClip(name, first, count, fps, loop)` or
  `addClip(AnimationClip)`. Default `TextureDesc` is `SpriteSheet::pixelArt()` (nearest, no
  mipmaps, clamped: mipmaps would blend frames). `texture()`, `frameCount()`,
  `frame(i).uv / .size / .duration`, `clip(name)` (nullptr + one warning if missing),
  `clipNames()`. `clip("hop")->addEvent(5, "land")` names a frame.
- `AnimationClip`: `frames` (sheet indices), `durations` (seconds), `loop`
  (`AnimationLoop::Loop / Once / PingPong`), `events`, `length()`.
- `render2d::Animator` (one per animated object; points at its sheet, so keep the sheet
  alive and in place): `play(sheet, "clip", restart = false)` (no-op if already playing:
  call it every step with the clip that *should* play), `update(dt)` (in `onFixedUpdate`
  for gameplay timing; frames change on exactly the right tick), `uv(flipX)` for
  `Sprite::uv` (draw art facing right), `frameSize()`, `frame()`, `clipName()`,
  `isPlaying("clip")`, `finished()` (Once clips), `eventFired("land")` / `events()` (events of
  frames entered during the last update, even skipped ones), `speed` (0 = paused), `stop()`.
- Many identical looping objects don't need an Animator each: pick
  `sheet.frame(int(time * fps + offset) % sheet.frameCount()).uv`.

## 9. Audio (`audio::AudioEngine`, `app.audio()`)

miniaudio underneath. Never fatal: with no device (or `GAME_AUDIO=0`) every call is a
harmless no-op. WAV, FLAC, MP3, OGG Vorbis. Call from the game thread only.

**Sound effects:** decoded on first play and cached; `preload(path)` in `onInit` avoids a
hitch. `SoundHandle h = play(path, PlayParams)`; handles are never reused, and a stale
handle is simply ignored. `stop(h, fadeSeconds)`, `stopAllSounds()`, `isPlaying(h)`,
`setVolume/setPitch/setPan(h, x)`, `setPosition(h, vec3)`, `unload(path)`. Up to 64 at once
(`AudioDesc::maxVoices`); when full, the oldest one-shot is cut (loops never are).

`PlayParams`: `volume`, `pitch` (2 = an octave up and twice as fast), `pan` (-1..1,
non-spatial only), `loop`, `spatial`, `position`, `minDistance`, `maxDistance`, `rolloff`,
`lowPassHz` (this sound muffled; `audio::kLowPassOff` = off), `reverbSend` (0 = stays
dry, e.g. UI sounds), `doppler` (this sound's Doppler amount; 0 = none).

**3D sound and Doppler:** `setListener(position, forward, up)` every frame (usually the
camera, right-handed, +Y up). Spatial sounds attenuate by distance (inverse model) and pan
by direction; use **mono** files. Doppler is automatic: velocities are estimated from
`setPosition`/`setListener` calls each frame (teleports ignored, speeds capped).
`setVelocity(h, v)` / `setListenerVelocity(v)` override the estimate. `setDopplerFactor`
(0 off, 1 real, 2-3 exaggerated), `setSpeedOfSound` (world units/s; 343 = metres),
`dopplerPitch(h)`. 

**Music:** one streamed track at a time. `playMusic(path, MusicParams{volume, loop = true,
fadeInSeconds, fadeOutPreviousSeconds = 1})` (crossfades from the current track),
`stopMusic(fade)`, `pauseMusic()`, `resumeMusic()`, `isMusicPlaying()`. Put tracks in
`assets/music/`. Music is never spatial and never gets reverb.

**Mixer:** `setMasterVolume`, `setSfxVolume`, `setMusicVolume` (default 0.8), `setMuted`
(keeps the volumes), plus the getters.

**Effects** (every change glides; default 0.3 s, pass `glideSeconds` to change):
- Per bus (`audio::Bus::Sfx`, `Bus::Music`, `Bus::Master`): `setLowPass(bus, hz)` (muffle;
  ~700 Hz = under water), `setHighPass(bus, hz)` (thin/radio; `kHighPassOff` = off),
  `setEcho(bus, EchoParams{delaySeconds, feedback, mix})` (mix 0 = off).
- Reverb (shared; sound effects send to it, music never): `setReverb(ReverbParams)`;
  `ReverbParams::preset(ReverbPreset::Off/Room/Hall/Cave)` returns plain values (`level`,
  `roomSize`, `damping`, `width`) that can be tweaked. Its tail rings on after the sound ends.
- Per sound: `setLowPass(handle, hz)` (occlusion: the game decides when something blocks a
  sound), `setReverbSend(handle, amount)`.
- `resetEffects()`, getters `lowPass(bus)`, `highPass(bus)`, `echo(bus)`, `reverb()`.
- Change effects on **transitions** (entering a zone), not every frame.

`AudioDesc` (in `AppConfig::audio`): `enabled`, `masterVolume`, `sfxVolume`, `musicVolume`,
`maxVoices`, `speedOfSound`, `dopplerFactor`, `noDevice` (tests: mix in memory with
`readFrames`).

## 10. Debug UI (`debugui::DebugUI`, Dear ImGui)

- **Debug builds only** by default (`AppConfig::debugUI.enabled`); not created in
  RelWithDebInfo/Release, where `app.debugUI()` is null. Toggle with **`** (the key below
  Esc; `DebugUIDesc::toggleKey`). On the Deck: bind a button to that key in Steam Input; the
  touchscreen acts as the mouse.
- Write ImGui code **only** in `Game::onDebugUI(app)`. `imgui.h` comes with
  `core/Application.h`. Always pair `ImGui::Begin` with `ImGui::End`, even when `Begin`
  returns false.
- The built-in **Engine** window: frame-time graph, GPU and swapchain info, vsync and
  fullscreen, audio volumes, all audio effects with live sliders, input devices, and a switch
  for ImGui's demo window (the best widget reference).
- `app.debugUI()->texture(gfx::Texture)` returns an `ImTextureID` for `ImGui::Image`.
- Useful patterns: `SetNextWindowPos(..., ImGuiCond_FirstUseEver)`,
  `ImGuiWindowFlags_AlwaysAutoResize`, `SliderFloat(..., ImGuiSliderFlags_Logarithmic)`,
  `ColorEdit3("Fog", &v.x)`, `SliderAngle` (radians shown as degrees), `"Label##id"` for
  repeated labels, `ImGui::GetStyle().FontScaleDpi` for DPI-aware sizes.
- The layout isn't saved between runs (no `imgui.ini`).

## 11. Conventions and rules for writing code here

- **Settings are the framework's job** (`app.settings()`, saved automatically); progress
  goes in a `core::SaveData`, saved when it changes. Never save next to the exe.
- **Menus and pausing are game states** (`app.states()`), not flags checked all over the
  game.
- **Gameplay controls are named actions** (`app.actions()`), never hard-coded keys: they
  must be rebindable, and help text uses `prompt()`. Raw `app.input()` is for the wheel,
  cursor and debug/editor keys.
- **Read input in `onUpdate`, simulate in `onFixedUpdate`, draw in `onRender`.**
  Interpolate rendered positions with `interpolationAlpha()`.
- **GPU object lifetime:** create in `onInit`; destroy in `onShutdown` (the GPU is idle)
  or in the game's destructor. Never destroy a texture/buffer mid-game that an in-flight
  frame may still use; call `app.renderer().waitIdle()` first. GPU wrappers (`Texture`,
  `Buffer`, `Image`, `Pipeline`, `Mesh`, `Font`) are RAII and move-only.
- **Colours:** author tints in **sRGB** (as in an image editor); the shaders convert to
  linear. Exception: `RendererConfig::clearColor` / `setClearColor` take **linear** values.
- **Sizes for different screens:** HUD sizes scale with `max(1, viewportHeight / 720)` in
  the demos; ImGui sizes with `ImGui::GetStyle().FontScaleDpi`.
- **Steam Deck:** 1280×800, gamepad-only must work, text must be readable on 7".
- **Errors:** unrecoverable → `core::fatal`; missing optional things → `LOG_WARN` and carry
  on. Vulkan calls go through `VK_CHECK` in framework code.
- **Validation layers** are on in Debug builds (`GAME_VALIDATION=0/1` overrides). Code must
  run with **zero** validation messages. `GAME_MAX_FRAMES=N` quits after N frames (tests).
- **Style:** C++20, 4-space indent, `camelCase` functions, `trailingUnderscore_` members,
  `kConstants`, namespaces per module (`core`, `gfx`, `render2d`, `render3d`, `audio`,
  `debugui`). Comments explain *why*. Warnings are treated seriously (MSVC `/W4`;
  GCC/Clang `-Wall -Wextra`).
- **Framework vs game code:** game code lives in `src/game/` (and new folders the game
  needs). If a game needs a change to the shared framework code (`core/`, `gfx/`, `audio/`,
  `debugui/`, `render2d/`, `render3d/`), make it carefully, keep it general, and
  **tell the user**, so the change can be brought back to the base frameworks (both 2D and
  3D share that code).

## 12. How this user works (important)

- The user develops on **Windows in Visual Studio 2022** ("Open Folder" with the CMake
  presets) and also targets Linux and Steam Deck. They are learning as they go: explain
  clearly, keep a friendly tone, and say exactly which files changed.
- **Base frameworks** live at `F:\Pojects\Claude\Game2D` and `F:\Pojects\Claude\Game3D`
  (the folder is really spelled "Pojects"). Work is written to those original folders only.
  The user copies to `WorkingVersion`, `TestingVersion`, `GITHUB\2DBaseFramework` /
  `GITHUB\3DBaseFramework`, `BackUps` and an external drive themselves. Each game is its own
  copy of a framework; ask where it lives if that isn't clear.
- **Before overwriting any file on the user's computer, check it hasn't changed since you
  last wrote it** (compare checksums). The user sometimes edits files (they have edited
  `TUTORIAL.md`); never overwrite their changes.
- At the end of a change, **list the files to copy** and remind them to leave out the
  `build` and `.vs` folders, which are tied to their location.
- **Visual Studio pitfall:** don't add files with *Add → New Item* in the *CMake Targets
  View*: it writes the file into CMake scripts and breaks the configure. Add files with File
  Explorer or the *Folder View*. If a copied project fails with "CMakeCache.txt directory …
  is different", delete its `build` (and `.vs`) folder.
- **Testing before handing over:** compile with warnings as errors, run with validation
  layers (SwiftShader works in a cloud container), check behaviour (including audio, by
  mixing in memory), and look at screenshots. Say plainly what couldn't be tested (e.g. not
  yet built in Visual Studio).
- Docs are Markdown in `docs/`, with Mermaid diagrams (validate with `mmdc`). When a
  feature is added, update `README.md`, `docs/GAME3D_ARCHITECTURE.md`, `docs/BUILDING.md` if
  relevant, the tutorial, **and this briefing**.

## 13. Known limitations (as of 2026-11-19)

**Roadmap (the owner's order):** model animation, third-person camera, shadows and
skinned characters are all done (skinning 2026-11-15), and so are instancing (2026-11-15)
and frustum culling and normal maps (2026-11-17; normal maps asked for by the
CharacterCreator project, stage 2 of its bake plan). The owner built and ran instancing,
frustum culling and normal maps in Visual Studio 2022 with a clean log (2026-11-15 and
2026-11-17). The colour-override `draw()` was added after that (2026-11-17) and copied,
with the rest of `render3d/` and the shaders, into CharacterCreator; the owner built and
ran both projects with it in Visual Studio 2022 the same day: all fine. **Next (decided 2026-11-19): Wild West Frontier** is to be made in Game3D, starting as
soon as the owner can (the Unreal Engine 5 version is paused); `docs/WILD_WEST_FRONTIER_IN_GAME3D.md`
lists what it needs and a suggested order. Morph targets for CharacterCreator's body
sliders were built here on 2026-11-19 and copied into CharacterCreator (the owner built and
ran it in Visual Studio 2022 the same day: all fine). Other candidates:
upper-body animation layering (walk + fire), shadow cascades, gamepad polish for the Steam Deck (the owner gets one late
November 2026). The 3D "Drone Invaders" will have a soldier gunner on the
gun turret (Load and Fire animations), built from swappable parts so other characters
can be mixed and matched from the same pieces.

- Models: no occlusion/metallic-roughness textures, KTX2/WebP images or Draco/meshopt
  compression (refused with a message); morph targets without tangent offsets or weight
  animations. Normal maps: first UV map only, no
  parallax. Skinning is linear blend,
  4 joints per vertex. One animation per animator at a time (plus cross-fades); no
  per-body-part layering; no animation events beyond `reached(time)`.
- Third-person camera: simple collider shapes only (no triangle-mesh ray tests), no
  player fade when close, no auto-follow swing; no character controller.
- One directional light; only it casts shadows: one shadow map (no cascades), so shadows
  stop at `shadows.distance` and are equally sharp near and far; constant softness; Blend
  parts cast none. Not PBR (roughness/metallic shape a Blinn-Phong
  highlight), no anti-aliasing, no skybox (the fog-coloured
  clear colour stands in); culling has no spatial structure (every object is tested every
  pass) and no occlusion culling; instanced copies share material and pose (no per-copy
  colour).
- Saves: no encryption/compression/cloud API (Steam Auto-Cloud works without code), no
  automatic `version` migration; window size/position aren't remembered.
- Game states: no built-in transitions (fades) or menu widgets (copy the Game2D demo's
  `Menu`); states are made in code.
- Input actions: one gamepad (no local multiplayer yet), no chords/combos ("Ctrl+S",
  double-tap; a fighting-game move list would be game code on top of actions), the wheel
  and cursor aren't bindable (rebound controls are saved in settings.json).
- One audio listener (no split-screen); one music track at a time; one shared reverb.
- The debug UI draws inside the game window (no ImGui docking or separate OS windows) and
  doesn't save its layout.
- No deferred GPU resource destruction (destroy only when idle); uploads are blocking
  (`immediateSubmit`); single graphics queue.
- No physics, no scene/entity system, no asset hot-reload, no save-game system: the game
  writes these (or adds a library; Box2D v3 for 2D, Jolt for 3D are good fits).

## 14. Where to look

| Need | File |
|---|---|
| Design, internals, diagrams | `docs/GAME3D_ARCHITECTURE.md` |
| Building on Windows, Linux, Steam Deck; shipping on Steam; troubleshooting | `docs/BUILDING.md` |
| The demo, rebuilt step by step | `docs/TUTORIAL.md` |
| Overview, controls, quick start | `README.md` |
| A complete example game | `src/game/DemoGame.h/.cpp` |
| Every ImGui widget with code | debug UI → Engine → "Dear ImGui demo window" |
