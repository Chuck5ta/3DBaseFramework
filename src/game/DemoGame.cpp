#include "game/DemoGame.h"

#include "core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdio>

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

void DemoGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    meshRenderer_ = std::make_unique<MeshRenderer>(gpu, app.renderer());

    cube_ = Mesh(gpu, primitives::cube(1.0f), "cube");
    sphere_ = Mesh(gpu, primitives::sphere(0.5f, 48, 24), "sphere");
    ground_ = Mesh(gpu, primitives::plane(200.0f, 100.0f), "ground"); // 200 x 200, texture repeated 100x

    crateTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/crate.png"));
    checkerTexture_ = makeCheckerTexture(gpu);

    // Clear to the fog colour, so distant geometry fades into the "sky".
    const glm::vec3 fog = lighting_.fogColor;
    app.renderer().setClearColor({srgbToLinear(fog.r), srgbToLinear(fog.g), srgbToLinear(fog.b), 1.0f});

    // 2D overlay: a SpriteBatch works inside the 3D pass too (it ignores depth).
    hud_ = std::make_unique<render2d::SpriteBatch>(gpu, app.renderer());
    font_ = render2d::Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf"));

    camera_.position = {0.0f, 3.0f, 10.0f};
    camera_.lookAt({0.0f, 1.0f, 0.0f});
}

void DemoGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
    meshRenderer_.reset();
    hud_.reset();
}

void DemoGame::onFixedUpdate(core::Application&, float dt)
{
    // Animation advances at a fixed rate, independent of the frame rate.
    prevSpin_ = spin_;
    prevSimTime_ = simTime_;
    spin_ += dt * 0.6f; // radians per second
    simTime_ += dt;
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
    if (in.keyPressed(SDL_SCANCODE_H))
        showHelp_ = !showHelp_;

    updateCamera(app, dt);

    // Slowly move the sun around the scene.
    const float a = float(app.time()) * 0.1f;
    lighting_.sunDirection = glm::normalize(glm::vec3(std::cos(a), -0.8f, std::sin(a)));
}

void DemoGame::updateCamera(core::Application& app, float dt)
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

void DemoGame::onRender(core::Application& app, gfx::FrameContext& frame)
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
}
