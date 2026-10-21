#include "core/Application.h"
#include "game/DemoGame.h"

// SDL_main.h provides the platform entry point (WinMain on Windows when the
// console is disabled) and forwards to main() below. Include it in exactly
// one file.
#include <SDL3/SDL_main.h>

int main(int, char**)
{
    core::AppConfig config;
    config.window.title = "Game3D";
    config.window.width = 1280;
    config.window.height = 720;
    config.renderer.vsync = true;
    config.renderer.depthBuffer = true; // 3D needs a depth buffer

    core::Application app(config);
    DemoGame game;
    return app.run(game);
}
