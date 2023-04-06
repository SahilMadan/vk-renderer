#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

#include <cmath>
#include <iostream>
#include <vector>

#include "renderer.hpp"

constexpr int kWindowWidth = 1700;
constexpr int kWindowHeight = 900;

vk::Renderer renderer;

void MainLoop() {
  SDL_Event e;
  bool quit = false;
  while (!quit) {
    Uint64 start = SDL_GetPerformanceCounter();

    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quit = true;
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {
          renderer.ToggleShader();
        }
      }
    }
    renderer.Draw();

    Uint64 end = SDL_GetPerformanceCounter();

    // Hacky method of controlling rendering time by capping at 60FPS.
    float elapsed_millisecs =
        (end - start) /
        static_cast<float>(SDL_GetPerformanceFrequency() * 1000.f);
    SDL_Delay(std::floor(16.666f - elapsed_millisecs));
  }
}

int main(int argc, char *argv[]) {
  // Initialize SDL2
  SDL_Init(SDL_INIT_VIDEO);

  // Create a window
  SDL_Window *window = SDL_CreateWindow(
      "vk-renderer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      kWindowWidth, kWindowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);

  unsigned int extension_count = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(window, &extension_count, nullptr)) {
    std::cerr << "Unable to query the number of Vulkan instance extensions.\n";
    return -1;
  }
  std::vector<const char *> extensions(extension_count);
  if (!SDL_Vulkan_GetInstanceExtensions(window, &extension_count,
                                        extensions.data())) {
    std::cerr << "Unable to query the number of Vulkan instance extensions.\n";
    return -1;
  }

  SDL_SysWMinfo window_info;
  SDL_VERSION(&window_info.version);
  SDL_GetWindowWMInfo(window, &window_info);

  vk::Renderer::InitParams renderer_params;
  renderer_params.width = kWindowWidth;
  renderer_params.height = kWindowHeight;
  renderer_params.application_name = "Vulkan Renderer";
  renderer_params.extensions = std::move(extensions);
  renderer_params.window_handle = window_info.info.win.window;

  if (!renderer.Init(renderer_params)) {
    renderer.Shutdown();
    std::cerr << "Unable to initialize the renderer." << std::endl;
    return -1;
  }

  MainLoop();

  // Clean up and exit
  renderer.Shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}