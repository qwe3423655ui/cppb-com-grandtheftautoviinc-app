// NEON CITY - Android launcher (NativeActivity, no Java code, no GL: the software-rendered frame is blitted
// straight into the window buffer). Touch input is forwarded to Game::touch().
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <time.h>
#include "game.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NeonCity", __VA_ARGS__)

namespace {
struct App {
  android_app* app = nullptr;
  Game game;
  Renderer R;
  bool haveWindow = false, resumed = false, inited = false;
  int rw = 640, rh = 360, sw = 1, sh = 1;
  bool active() const { return haveWindow && resumed; }
};

double nowSec() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void setupWindow(App& a) {
  ANativeWindow* win = a.app->window;
  a.sw = std::max(1, ANativeWindow_getWidth(win));      // physical size; touch coordinates use the same space
  a.sh = std::max(1, ANativeWindow_getHeight(win));
  internalSize(a.sw, a.sh, a.rw, a.rh);
  ANativeWindow_setBuffersGeometry(win, a.rw, a.rh, WINDOW_FORMAT_RGBA_8888);   // the compositor scales it to the screen
  a.R.init(a.rw, a.rh);
  if (!a.inited) { a.game.init(a.rw, a.rh, 1); a.game.showTouch = true; a.inited = true; }
  else a.game.resize(a.rw, a.rh);
  a.haveWindow = true;
  LOGI("window %dx%d -> render %dx%d, %d render thread(s)", a.sw, a.sh, a.rw, a.rh, a.R.threads);
}

void onCmd(android_app* app, int32_t cmd) {
  App* a = static_cast<App*>(app->userData);
  switch (cmd) {
    case APP_CMD_INIT_WINDOW: if (app->window) setupWindow(*a); break;
    case APP_CMD_TERM_WINDOW: a->haveWindow = false; break;
    case APP_CMD_RESUME: a->resumed = true; break;
    case APP_CMD_PAUSE: a->resumed = false; break;
    default: break;
  }
}

int32_t onInput(android_app* app, AInputEvent* ev) {
  App* a = static_cast<App*>(app->userData);
  if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
  int32_t act = AMotionEvent_getAction(ev);
  int32_t kind = act & AMOTION_EVENT_ACTION_MASK;
  size_t idx = (size_t)((act & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
  auto fx = [&](size_t i) { return AMotionEvent_getX(ev, i) * (float)a->rw / (float)a->sw; };
  auto fy = [&](size_t i) { return AMotionEvent_getY(ev, i) * (float)a->rh / (float)a->sh; };
  size_t n = AMotionEvent_getPointerCount(ev);
  switch (kind) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN: a->game.touch(AMotionEvent_getPointerId(ev, idx), 0, fx(idx), fy(idx)); break;
    case AMOTION_EVENT_ACTION_MOVE: for (size_t i = 0; i < n; i++) a->game.touch(AMotionEvent_getPointerId(ev, i), 1, fx(i), fy(i)); break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP: a->game.touch(AMotionEvent_getPointerId(ev, idx), 2, fx(idx), fy(idx)); break;
    case AMOTION_EVENT_ACTION_CANCEL: for (size_t i = 0; i < n; i++) a->game.touch(AMotionEvent_getPointerId(ev, i), 2, 0, 0); break;
    default: return 0;
  }
  return 1;
}
}  // namespace

void android_main(android_app* app) {
  static App a;                                           // static: lives as long as the process
  a.app = app;
  app->userData = &a;
  app->onAppCmd = onCmd;
  app->onInputEvent = onInput;
  ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
  double last = nowSec();
  for (;;) {
    int events = 0;
    android_poll_source* src = nullptr;
    int ident;
    while ((ident = ALooper_pollOnce(a.active() ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&src))) >= 0) {
      if (src) src->process(app, src);
      if (app->destroyRequested) return;
    }
    (void)ident;
    if (!a.active()) { last = nowSec(); continue; }

    double t = nowSec();
    float dt = (float)std::min(0.05, t - last);
    last = t;
    a.game.update(dt);
    a.game.draw(a.R);

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(app->window, &buf, nullptr) == 0) {
      uint32_t* dst = static_cast<uint32_t*>(buf.bits);
      int rows = std::min(buf.height, a.rh), cols = std::min(buf.width, a.rw);
      for (int y = 0; y < rows; y++) std::memcpy(dst + (size_t)y * buf.stride, a.R.px.data() + (size_t)y * a.rw, (size_t)cols * 4);
      ANativeWindow_unlockAndPost(app->window);
    }
  }
}
