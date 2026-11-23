// NEON CITY - desktop launcher (SDL2). Handy for testing on a PC; mobile uses platform_android.cpp.
// Keys: WASD move/drive, Space handbrake, J / Ctrl / left mouse fire, E use/enter/exit/sell/buy, Q swap weapon,
//       arrows or right-mouse-drag look around, Esc quit.
#include <SDL2/SDL.h>
#include "game.h"

int main(int, char**) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }
  SDL_Window* win = SDL_CreateWindow("NEON CITY", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
  if (!win) { std::fprintf(stderr, "window failed: %s\n", SDL_GetError()); return 1; }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError()); return 1; }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

  Game game;
  Renderer R;
  int ww = 1280, wh = 720, rw = 640, rh = 360;
  SDL_GetWindowSize(win, &ww, &wh);
  internalSize(ww, wh, rw, rh);
  game.init(rw, rh, 1);
  game.showTouch = false;
  R.init(rw, rh);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, rw, rh);

  bool running = true, mouseFire = false;
  Uint64 last = SDL_GetPerformanceCounter();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_KEYDOWN:
          if (e.key.repeat) break;
          if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
          if (e.key.keysym.sym == SDLK_e) game.kb.use = true;
          if (e.key.keysym.sym == SDLK_q) game.kb.swap = true;
          break;
        case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) mouseFire = true; break;
        case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) mouseFire = false; break;
        case SDL_MOUSEMOTION:
          if (e.motion.state & SDL_BUTTON_RMASK) { game.kb.camDX += e.motion.xrel * (float)rw / std::max(1, ww); game.kb.camDY += e.motion.yrel * (float)rh / std::max(1, wh); }
          break;
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:    // touch screens (tablets / 2-in-1 laptops)
          game.showTouch = true;
          game.touch((int)e.tfinger.fingerId, e.type == SDL_FINGERDOWN ? 0 : (e.type == SDL_FINGERMOTION ? 1 : 2), e.tfinger.x * rw, e.tfinger.y * rh);
          break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            SDL_GetWindowSize(win, &ww, &wh);
            internalSize(ww, wh, rw, rh);
            R.init(rw, rh);
            game.resize(rw, rh);
            SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, rw, rh);
          }
          break;
        default: break;
      }
    }
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
    last = now;
    dt = std::min(dt, 0.05f);

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    game.kb.mx = (ks[SDL_SCANCODE_D] ? 1.f : 0.f) - (ks[SDL_SCANCODE_A] ? 1.f : 0.f);
    game.kb.my = (ks[SDL_SCANCODE_W] ? 1.f : 0.f) - (ks[SDL_SCANCODE_S] ? 1.f : 0.f);
    game.kb.fire = mouseFire || ks[SDL_SCANCODE_J] || ks[SDL_SCANCODE_LCTRL];
    game.kb.brake = ks[SDL_SCANCODE_SPACE] != 0;
    game.kb.camDX += ((ks[SDL_SCANCODE_RIGHT] ? 1.f : 0.f) - (ks[SDL_SCANCODE_LEFT] ? 1.f : 0.f)) * 420.f * dt;
    game.kb.camDY += ((ks[SDL_SCANCODE_DOWN] ? 1.f : 0.f) - (ks[SDL_SCANCODE_UP] ? 1.f : 0.f)) * 260.f * dt;

    game.update(dt);
    game.draw(R);
    SDL_UpdateTexture(tex, nullptr, R.px.data(), rw * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
