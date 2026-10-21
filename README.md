# Game3D — C++20 / Vulkan 1.3 starter for 3D games

A small, engine-free base for 3D games on **Windows, Linux and Steam Deck**.
You own all the code: a thin platform layer (SDL3), a Vulkan 1.3 renderer with a
depth buffer, and a lit, textured mesh renderer with a fly camera, plus a demo
scene showing how they fit together.

| Layer | What it does | Library |
|---|---|---|
| Platform | Window, keyboard/mouse, gamepads (incl. Steam Deck controls), hi-DPI | SDL3 |
| GPU | Instance/device setup, swapchain, frames in flight, buffers, textures, pipelines | Vulkan 1.3 via volk + VMA |
| 3D | Meshes + primitives (cube, plane, sphere), perspective camera, sun + hemisphere ambient + fog lighting, wireframe | your code |
| Math / images | Vectors & matrices, PNG/JPG loading | GLM, stb_image |

All dependencies are downloaded automatically by CMake at a pinned version.

For a deeper look at how it all fits together, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
Step-by-step build instructions for Windows, Linux and Steam Deck are in [docs/BUILDING.md](docs/BUILDING.md).

---

## 1. Prerequisites

### Windows
- **Visual Studio 2022 or 2026** with the *Desktop development with C++* workload (includes CMake + Ninja)
- **Vulkan SDK** (recommended): <https://vulkan.lunarg.com/sdk/home>. Gives you the
  validation layers and `glslc`. Without it the project still builds (glslang is built
  from source) but you lose validation messages.
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
  core/                 platform layer (shared with Game3D)
    Application         owns everything, runs the main loop
    Window              SDL window, Vulkan surface, fullscreen
    Input               keyboard, mouse, gamepad (pressed / down / released)
    Paths               shaderPath(), assetPath()
    Log.h               LOG_INFO/WARN/ERROR, core::fatal()
  gfx/                  Vulkan layer (shared with Game3D)
    VulkanContext       instance, GPU selection, device, queues, VMA, immediateSubmit
    Swapchain           creation/recreation, per-image semaphores
    Renderer            frames in flight, acquire/submit/present, main pass
    Buffer, Image       RAII GPU memory
    Texture             image + sampler + ready-to-bind descriptor set
    Pipeline            PipelineBuilder (dynamic rendering, no render passes)
    Descriptors         layout builder, growable allocator, writer
  render3d/
    Mesh                vertex/index buffers + primitives::cube/plane/sphere
    MeshRenderer        lit textured meshes (frame UBO, per-object push constants)
    Camera3D            perspective camera (yaw/pitch, Vulkan clip conventions)
  game/
    DemoGame            the example — replace with your game
shaders/                GLSL; every *.vert/*.frag/*.comp is compiled automatically
                        (*.glsl files are shared includes, e.g. common.glsl)
assets/                 copied next to the exe
cmake/                  dependency + shader build logic
```

`core/` and `gfx/` are identical in Game2D and Game3D, so improvements can be copied
between them (or later moved into a shared library).

---

## 4. How a frame works

```
Application::run()
 ├─ poll SDL events → Input, window resize → Renderer
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
- Your own geometry: fill a `render3d::MeshData` (positions, normals, UVs, indices —
  counter-clockwise triangles) and construct a `Mesh`. A glTF loader (cgltf or
  fastgltf) is the natural next step.
- New shaders: drop `foo.vert`/`foo.frag` in `shaders/`, load
  `core::shaderPath("foo.vert.spv")`, build a pipeline with `gfx::PipelineBuilder`
  (copy the one in `MeshRenderer` and use `renderer.depthFormat()`).
- Off-screen passes (shadow maps, post-processing) go in `Game::onPreRender`, which runs
  before the main pass begins.
- Destroying a texture/buffer that an in-flight frame still uses is an error; call
  `app.renderer().waitIdle()` first (or destroy in `onShutdown`).
- Validation layers are on in Debug builds (set `GAME_VALIDATION=0/1` to override). Keep
  the console clean of validation errors.

### Demo controls
| Action | Keyboard / mouse | Gamepad / Steam Deck |
|---|---|---|
| Look | Hold right mouse button, or Tab to capture the mouse | Right stick |
| Move | WASD | Left stick |
| Up / down | Space or E / Ctrl or Q | Right / left trigger |
| Move faster | Shift | Click left stick |
| Wireframe | F1 | Y |
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

Things worth testing on the Deck: 800p resolution, GPU time at 40/60 Hz frame
limits (Steam's performance overlay helps), suspend/resume (swapchain recreation), on-screen keyboard if you need text input.

---

## 7. Where to go next

- **Model loading:** glTF 2.0 via cgltf or fastgltf → `MeshData` + textures.
- **Shadows:** a depth-only pass for the sun in `onPreRender`, sampled in `mesh.frag`.
- **PBR materials:** metallic/roughness + image-based lighting.
- **MSAA or FXAA/TAA** for smoother edges.
- **Debug UI:** Dear ImGui (`imgui_impl_sdl3` + `imgui_impl_vulkan` work with dynamic rendering).
- **Physics:** Jolt Physics.
- **Audio:** SDL3 audio streams, or miniaudio.
- **Frustum culling and instancing** once scenes get large.
