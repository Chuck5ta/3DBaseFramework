# Game3D — C++20 / Vulkan 1.3 starter for 3D games

A small, engine-free base for 3D games on **Windows, Linux and Steam Deck**. You own all
the code: a thin platform layer (SDL3), a Vulkan 1.3 renderer with a depth buffer, a
lit, textured mesh renderer with sun shadows, normal maps, instancing and frustum culling that also draws glTF models
from Blender (animated, rigid or skinned), fly and third-person cameras and 3D audio, plus
a demo scene showing how they fit together.

| Layer | What it does | Library |
|---|---|---|
| Platform | Window, keyboard/mouse, gamepads (incl. Steam Deck controls), hi-DPI | SDL3 |
| Input actions | Named actions ("jump") with rebindable keys/buttons/sticks, button prompts for the device in use (Xbox, PlayStation, Nintendo labels), buffered presses, bindings as JSON | nlohmann/json |
| Game states | A stack of screens (title, pause, game over) above the game: each can pause or hide what's below; safe push/pop/replace | your code |
| Settings & saves | The player's settings (fullscreen, vsync, volumes, mute, rebound controls) remembered automatically; JSON save files with crash-safe writes, in the player's own folder (Steam Cloud friendly) | nlohmann/json |
| GPU | Instance/device setup, swapchain, frames in flight, buffers, textures, pipelines | Vulkan 1.3 via volk + VMA |
| 3D | Meshes + primitives (cube, plane, sphere), perspective camera, third-person follow camera (orbit, zoom, over-the-shoulder, keeps out of walls), ray tests (plane, sphere, box), sun + hemisphere ambient + fog lighting, normal maps (MikkTSpace tangents, so Blender bakes line up), soft sun shadows (a stable shadow map around the camera), instancing (hundreds of copies in one draw call), frustum culling (what the camera or the sun can't see isn't drawn), wireframe | your code, MikkTSpace |
| Models | glTF 2.0 (`.glb`/`.gltf`, e.g. exported from Blender): textured, emissive, normal-mapped, alpha-masked and see-through materials; Blender animations played with blending, moments (`reached`) and sockets for attaching models; skinned meshes bent on the GPU (4 bones per vertex); shape keys (morph targets) blended on the GPU with weights per draw, for sliders; named parts posed in code (spinning rotors, turrets) | cgltf |
| HUD & text | 2D overlay (sprites, rectangles) and TrueType text on top of the 3D scene | stb_truetype |
| Audio | 3D positional sound with Doppler, sound effects, streamed music with crossfades, mixer, mute; effects: reverb, low-pass (per bus or per sound), high-pass, echo | miniaudio |
| Debug UI | In-game tool windows (Debug builds): frame graph, GPU info, volumes, your own sliders and buttons | Dear ImGui |
| Math / images | Vectors & matrices, PNG/JPG loading | GLM, stb_image |

All dependencies are downloaded automatically by CMake at a pinned version.

For a deeper look at how it all fits together, see [docs/GAME3D_ARCHITECTURE.md](docs/GAME3D_ARCHITECTURE.md).
Step-by-step build instructions for Windows, Linux and Steam Deck are in [docs/BUILDING.md](docs/BUILDING.md).
To learn the framework by rebuilding the demo step by step, follow [docs/TUTORIAL.md](docs/TUTORIAL.md).
Starting a chat with an AI assistant about a game built on this framework? Attach
[docs/GAME3D_AI_BRIEFING.md](docs/GAME3D_AI_BRIEFING.md): a compact summary of the API, conventions and workflow.

---

## 1. Prerequisites

### Windows
- **Visual Studio 2022 or 2026** with the *Desktop development with C++* workload (includes CMake + Ninja)
- **Vulkan SDK** (recommended): <https://vulkan.lunarg.com/sdk/home>. Gives you the
  validation layers and `glslc`. Without it the project still builds (glslang is built
  from source, which needs **Python 3** on the PATH) but you lose validation messages.
- An up-to-date GPU driver.

### Linux (desktop)
Compiler, CMake ≥ 3.22, Ninja, Git, plus the headers SDL3 needs for X11 and Wayland.

Ubuntu / Debian:
```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev \
  libdecor-0-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev \
  libasound2-dev libpulse-dev \
  glslang-tools vulkan-validationlayers mesa-vulkan-drivers
```
Fedora: `sudo dnf install gcc-c++ cmake ninja-build git libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libEGL-devel libdecor-devel systemd-devel dbus-devel alsa-lib-devel pulseaudio-libs-devel glslang vulkan-validation-layers`

Arch: `sudo pacman -S base-devel cmake ninja git libx11 libxext libxrandr libxcursor libxi libxss libxtst libxkbcommon wayland wayland-protocols libdecor glslang vulkan-validation-layers`

### Steam Deck
See [section 6](#6-steam-deck).

---

## 2. Build & run

### Command line (any platform)
```bash
cmake --preset debug          # configure (first run downloads dependencies)
cmake --build --preset debug  # build
./build/debug/bin/Debug/Game3D          # Linux
build\debug\bin\Debug\Game3D.exe        # Windows
```
Presets: `debug`, `relwithdebinfo` (profiling), `release` (no console window on Windows).

On Windows, run the command-line build from a **Developer PowerShell for VS** so the
compiler is on the PATH.

### Visual Studio
*File → Open → Folder…* and pick this folder. VS reads `CMakePresets.json`; choose the
`debug` preset, select **Game3D.exe** as the startup item, press F5.
Or generate a classic solution: `cmake --preset vs2022` (or `vs2026`) and open the `.sln` in `build/`.

### VS Code / CLion
Both understand `CMakePresets.json` directly (VS Code: install *CMake Tools*).

The output folder contains everything the game needs:
```
bin/<Config>/
  Game3D(.exe)
  shaders/*.spv      compiled from shaders/ on every build
  assets/...         copied from assets/ on every build
```
Files are found relative to the executable, so it runs from any working directory.

For a distributable folder: `cmake --install build/release --prefix dist`.

---

## 3. Project layout

```
src/
  main.cpp              configure window/renderer, run the game
  core/                 platform layer (shared with Game2D)
    Application         owns everything, runs the main loop
    Window              SDL window, Vulkan surface, fullscreen
    Input               keyboard, mouse, gamepad (pressed / down / released)
    Actions             named input actions: rebindable, with button prompts
    GameState           title / pause / game-over screens on a stack (app.states())
    Paths               shaderPath(), assetPath(), saveFolder() (the player's folder)
    SaveData            save files: get/set values, crash-safe save, backup; app.settings()
    Log.h               LOG_INFO/WARN/ERROR, core::fatal()
  audio/                sound effects, music, 3D sound (shared with Game2D)
    AudioEngine         miniaudio: cached effects, streamed music, listener, mixer
  debugui/              debug overlay, ` key (shared with Game2D)
    DebugUI             Dear ImGui setup; your windows go in Game::onDebugUI
  gfx/                  Vulkan layer (shared with Game2D)
    VulkanContext       instance, GPU selection, device, queues, VMA, immediateSubmit
    Swapchain           creation/recreation, per-image semaphores
    Renderer            frames in flight, acquire/submit/present, main pass
    Buffer, Image       RAII GPU memory
    Texture             image + sampler + ready-to-bind descriptor set
    Pipeline            PipelineBuilder (dynamic rendering, no render passes)
    Descriptors         layout builder, growable allocator, writer
  render2d/             shared with Game2D, used for the HUD
    SpriteBatch         batched quads + drawText
    Font                .ttf/.otf baked into a texture atlas
  render3d/
    Mesh                vertex/index buffers + primitives::cube/plane/sphere
    Tangents            generateTangents: MikkTSpace tangents for normal maps
    Model               glTF 2.0 models: parts, materials, textures, poses, animations, skins,
                        shape keys (morph targets)
    ModelAnimator       plays a model's animations (blending, looping, moments)
    MeshRenderer        lit textured meshes and models (frame UBO, per-object push constants),
                        sun shadows (a shadow pass in onPreRender), GPU skinning, morph targets,
                        normal maps, instancing, frustum culling
    Camera3D            perspective camera (yaw/pitch, Vulkan clip conventions)
    Frustum             the six planes of a view: is a box or sphere in it?
    ThirdPersonCamera   follows a character: orbit, zoom, shoulder offset, collision
    Raycast             ray tests against planes, spheres and boxes
  game/
    DemoGame            the example — replace with your game
shaders/                GLSL; every *.vert/*.frag/*.comp is compiled automatically
                        (*.glsl files are shared includes, e.g. common.glsl)
assets/                 copied next to the exe (fonts/: Roboto Medium, Apache 2.0;
                        sounds/: demo sounds, CC0; models/: the demo's drone, soldiers
                        and rifle, CC0)
tools/                  make_drone.py, make_gunner.py, make_gunner_sounds.py: write the demo's
                        models and gunner sounds (plain Python; gltf_builder.py is shared)
cmake/                  dependency + shader build logic
```

`core/`, `gfx/`, `audio/`, `debugui/` and `render2d/` are identical in Game2D and Game3D, so improvements can be
copied between them (or later moved into a shared library).

---

## 4. How a frame works

```
Application::run()
 ├─ poll SDL events → Input, window resize → Renderer
 ├─ Actions update             named actions from this frame's input
 ├─ States: apply push/pop    queued last frame (onEnter/onExit, onPause/onResume)
 │    (below, "Game::" = the Game, then each state, skipping paused or hidden ones)
 ├─ Game::onFixedUpdate(dt)   0..n times at a fixed 60 Hz (gameplay/physics)
 ├─ Game::onUpdate(dt)        once per frame (input, camera)
 └─ Renderer
     ├─ beginFrame()          wait for frame slot, acquire swapchain image
     ├─ Game::onPreRender()   work outside the main pass (uploads, offscreen)
     ├─ beginMainPass()       dynamic rendering, clear, viewport
     ├─ Game::onRender()      your draw calls
     └─ endMainPass(), endFrame()   submit + present
```

* **Frames in flight:** 2 by default. Anything you write from the CPU each frame
  (vertex data, uniforms) needs one copy per frame — index it with `frame.frameIndex`
  (see `MeshRenderer`'s per-frame uniform buffers).
* **Fixed timestep + interpolation:** simulate in `onFixedUpdate`, keep the previous
  state, and render `mix(prev, current, app.interpolationAlpha())` for smooth motion on
  any refresh rate (the demo does this for its animation).
* **Colour:** the swapchain is sRGB, shaders output linear colour. Material and light
  colours are given in sRGB and converted for you.
* **Conventions:** right-handed, +Y up, counter-clockwise front faces, depth 0 (near)
  → 1 (far), `Camera3D::projection()` flips Y for Vulkan.
* **Resizing, minimising, vsync toggling, fullscreen (F11 / Alt+Enter)** are handled.

---

## 5. Making your game

1. Copy `game/DemoGame.*` as a starting point or write a new `core::Game` subclass.
2. In `main.cpp`, construct your game instead of `DemoGame`.
3. Create GPU resources in `onInit`, draw in `onRender`:

```cpp
void MyGame::onInit(core::Application& app) {
    meshes_ = std::make_unique<render3d::MeshRenderer>(app.gpu(), app.renderer());
    cube_   = render3d::Mesh(app.gpu(), render3d::primitives::cube());
    crate_  = gfx::Texture::fromFile(app.gpu(), core::assetPath("textures/crate.png"));
}

void MyGame::onRender(core::Application&, gfx::FrameContext& frame) {
    camera_.setAspect(float(frame.extent.width) / float(frame.extent.height));
    meshes_->begin(frame, camera_, lighting_);
    meshes_->draw(cube_, glm::translate(glm::mat4(1.0f), {0, 1, 0}), {&crate_});
    meshes_->draw(cube_, someMatrix, {nullptr, {1.0f, 0.2f, 0.2f, 1.0f}}); // plain red
    meshes_->end();
}
```

Tips
- Models from Blender: export **glTF Binary (.glb)** into `assets/models/`, then
  `model_ = render3d::Model::load(app.gpu(), core::assetPath("models/ship.glb"))` and
  `meshes_->draw(model_, transform)`. Spin or aim named parts with a `render3d::ModelPose`
  (`pose[model_.findNode("Turret")].rotation = ...`, then `draw(model_, transform, &pose)`).
  Front is +Z (Blender's -Y). Tutorial step 12 walks through it.
- Animated characters: build them in Blender from separate rigid parts (each with its
  origin at its joint), one Action per animation, and play them with a
  `render3d::ModelAnimator` (`play(model, "Fire", render3d::AnimationLoop::Once)`,
  `update(dt)`, `meshes_->draw(model, transform, &animator.pose())`). Attach weapons or
  hats at socket empties with `model.nodeMatrix(node, transform, &pose)`. Tutorial step 13.
- Skinned characters (one mesh bending at the joints): parent the mesh to an armature
  *With Automatic Weights* in Blender and export glTF with *Skinning* on. Load, animate and
  draw them exactly like rigid ones; the renderer bends them. Tutorial step 16.
- One model in many colours (team colours, a character creator's outfits): pass one
  linear colour per material, `meshes_->draw(model_, transform, &pose, colors)`; they
  replace the materials' base colours for that draw.
- Shape sliders (body shape, facial expressions): make shape keys in Blender (exported
  as glTF morph targets), find one with `model_.findMorphTarget("Belly")`, keep a weight
  list per character (`std::vector<float> w = model_.defaultMorphWeights()`, then
  `w[belly] = 0.7f`) and draw with `meshes_->draw(model_, transform, &pose, {}, w)`. The
  GPU blends them before skinning, shadows included.
- Many copies of the same thing (trees, rocks, crates): put a matrix per copy in a
  `std::vector<glm::mat4>` and call `meshes_->drawInstanced(model_, matrices)` (or
  `drawInstanced(mesh, matrices, material)`): one draw call per part for all of them.
  Tutorial step 17.
- Off-screen things aren't drawn: `MeshRenderer` culls every draw, model part and
  instanced copy against the camera (and, for shadows, the sun's view) by itself. Use
  `render3d::Frustum(camera_.viewProjection()).intersects(min, max, matrix)` to skip your
  own work for things off screen. Tutorial step 18.
- A third-person game: move the player with `follow.flatForward()`/`flatRight()`, then
  `follow.update(playerPos, dt, rayFn)` and `follow.apply(camera_)` with a
  `render3d::ThirdPersonCamera follow`. `rayFn` returns the nearest hit of your level's
  colliders (`render3d::raycastBox(ray, crateMatrix)` and friends) so the camera stays out
  of walls. Tutorial step 14.
- Shadows: put your 3D drawing in one function and call it twice, between
  `meshes_->beginShadows(frame, camera_, lighting_)` and `endShadows()` in
  `onPreRender`, then between `begin()` and `end()` in `onRender`. Tune them with
  `lighting_.shadows` (distance, strength, softness, biases); leave something out with
  `material.castShadows = false`. Tutorial step 15.
- Your own geometry: fill a `render3d::MeshData` (positions, normals, UVs, indices —
  counter-clockwise triangles) and construct a `Mesh`.
- Normal maps (bumps, grooves, grain without extra triangles): load the map with
  `gfx::TextureDesc` `srgb = false` and set `material.normalTexture`. Primitives and glTF
  models come with tangents; for your own `MeshData`, call `render3d::generateTangents(data)`
  first. A glTF material's normal texture is used automatically. Tutorial step 19.
- New shaders: drop `foo.vert`/`foo.frag` in `shaders/`, load
  `core::shaderPath("foo.vert.spv")`, build a pipeline with `gfx::PipelineBuilder`
  (copy the one in `MeshRenderer` and use `renderer.depthFormat()`).
- Other off-screen passes (post-processing, reflections) go in `Game::onPreRender` too,
  which runs before the main pass begins.
- Destroying a texture/buffer that an in-flight frame still uses is an error; call
  `app.renderer().waitIdle()` first (or destroy in `onShutdown`).
- Sound: `app.audio().play(path, {.spatial = true, .position = p})` for 3D sound, with
  `app.audio().setListener(camera.position, camera.forward(), camera.up())` each frame;
  music: `app.audio().playMusic(core::assetPath("music/theme.ogg"))`. WAV, FLAC, MP3, OGG.
- Effects: `app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset::Hall))`;
  muffle one sound behind a wall with `app.audio().setLowPass(handle, 1200.0f)`, or every
  effect with `setLowPass(audio::Bus::Sfx, ...)`. Changes glide smoothly. Try them live in the
  debug UI (Engine → Audio → Audio effects).
- Doppler is automatic for 3D sounds: move a sound with `app.audio().setPosition(handle, p)`
  every frame and it rises in pitch as it approaches. Set `AppConfig::audio.speedOfSound` to
  match your units (343 = metres) and `dopplerFactor` to exaggerate it.
- Settings and saves: the framework remembers fullscreen, vsync, volumes, mute and
  rebound controls in `settings.json` by itself. Add your own with
  `app.settings().set("game.difficulty", 2)`. For progress, use a `core::SaveData`:
  `save.load()`, `save.getInt("bestScore")`, `save.set(...)`, `save.save()`.
  Files go in the player's save folder: `%APPDATA%\MyStudio\Game3D\` on Windows,
  `~/.local/share/MyStudio/Game3D/` on Linux and Steam Deck. Set `config.organization`
  and `config.application` in `main.cpp` before you release (and don't change them
  afterwards, or players lose their saves). While developing: debug UI → Engine →
  **Settings & saves** to open the folder or reset settings.
- Game states: derive from `core::GameState`, then `app.states().push<PauseState>(...)`.
  By default a state pauses the game below it (its `onUpdate`/`onFixedUpdate` stop) and
  lets it show through; `pop()` resumes. Changes apply at the start of the next frame, so
  a state can pop itself. (The Game2D tutorial, step 14, builds a title, pause and game-over screen.)
- Input actions: define once in `onInit` with
  `app.actions().define("jump", {core::Binding::key(SDL_SCANCODE_SPACE), core::Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)})`,
  then ask `actions.pressed("jump")`, `actions.vector("left", "right", "up", "down")`, or
  `actions.consume("jump")` in `onFixedUpdate` (a buffered press). `actions.prompt("jump")`
  gives "Space" or "A" for the help text; players can rebind with `capture()` + `addBinding()`.
- Debug UI: press **`** (below Esc) in a Debug build. Add your own windows by overriding
  `onDebugUI(core::Application&)` and calling ImGui there, e.g.
  `ImGui::SliderFloat("Speed", &speed_, 0.0f, 500.0f);`.
- Validation layers are on in Debug builds (set `GAME_VALIDATION=0/1` to override). Keep
  the console clean of validation errors.

### Demo controls
The demo starts in third person, behind a (skinned) soldier you can walk around, in a
clearing ringed by an instanced forest of 800 trees.

| Action | Keyboard / mouse | Gamepad / Steam Deck |
|---|---|---|
| Switch camera: third person / fly | C | Start (Menu) |
| Look / orbit around the soldier | Hold right mouse button, or Tab to capture the mouse | Right stick |
| Walk (third person) / move (fly) | WASD | Left stick |
| Run (third person) / move faster (fly) | Shift | Click left stick |
| Zoom (third person) | Mouse wheel | D-pad up / down |
| Up / down (fly) | Space or E / Ctrl or Q | Right / left trigger |
| Gunner fires / reloads | F / L | B / X |
| Wireframe | F1 | Y |
| Shadows on / off | G | |
| Mute sound | M | |
| Reverb: off / room / hall / cave | R | |
| Debug UI (Debug builds) | ` (key below Esc) | |
| Show/hide help | H | |
| Toggle vsync | V | |
| Fullscreen | F11 / Alt+Enter | |
| Quit | Esc (first press releases the mouse) | View (Back) |

---

## 6. Steam Deck

The Deck runs SteamOS (Arch-based Linux) with an AMD GPU using Mesa's RADV
Vulkan driver, which fully supports everything used here. Screen: 1280×800.
Steam Input presents the built-in controls as a standard gamepad, so `Input`'s
gamepad API just works.

**Getting a build onto the Deck — pick one:**
1. **Build on the Deck (quick testing).** In Desktop Mode, SteamOS's system partition is
   read-only, so build inside a container: `distrobox create -i archlinux` (or
   `ubuntu:24.04`), enter it, install the Linux packages above, then build normally.
   Add the resulting executable to Steam with *Add a Non-Steam Game* to run it in Game Mode.
2. **Build on a Linux PC and copy the `bin/Release` folder** to the Deck (e.g. via
   `scp`, a USB stick or Steam's dev kit tools). Building on an older/LTS distro
   gives the most compatible binary.
3. **For release on Steam,** build inside Valve's **Steam Linux Runtime 4.0 SDK** container
   (<https://gitlab.steamos.cloud/steamrt/steamrt4/sdk>) so the binary runs on every SteamOS
   and Linux Steam installation.

Full step-by-step instructions: [docs/BUILDING.md](docs/BUILDING.md#4-steam-deck).

Also handy: the **Steam Deck devkit client** (Steamworks docs → *Steam Deck → Developing
for Steam Deck*) deploys and runs builds from your PC over the network.

The Windows build also runs on the Deck through Proton, but shipping the native Linux build is better.

Saves and settings live in `~/.local/share/<organization>/<application>/` on the Deck. Steam
Cloud can sync them with Auto-Cloud (no code): root `WinAppDataRoaming` plus your
`<organization>/<application>` subfolder, with a Linux root override to `LinuxXdgDataHome`.

Things worth testing on the Deck: 800p resolution, GPU time at 40/60 Hz frame
limits (Steam's performance overlay helps), suspend/resume (swapchain recreation), on-screen keyboard if you need text input.

---

## 7. Where to go next

- **Animation layers:** walk with the legs while the arms fire, by combining two animators' poses.
- **Shadow cascades:** several shadow maps from near to far, for sharp shadows close up that also reach the horizon.
- **PBR materials:** metallic/roughness + image-based lighting.
- **MSAA or FXAA/TAA** for smoother edges.
- **Tools in the debug UI:** grow `onDebugUI` into a level editor or a tuning panel that saves to a file.
- **Physics:** Jolt Physics.
- **Music:** add a track to `assets/music/` and call `app.audio().playMusic(...)`.
- **Bigger worlds:** a grid of cells, so culling skips whole areas at once instead of testing every object.
