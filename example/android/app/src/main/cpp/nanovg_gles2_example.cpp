#include <string>
#include <vector>
#include <chrono>
#include <cassert>

#include <android/log.h>
#include <android_native_app_glue.h>
#include "example.hpp"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "nanovg.h"
#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include "demo.h"
#include "perf.h"

static const char *kTAG = "nanoVG Example";
#define LOGI(...) \
  ((void)__android_log_print(ANDROID_LOG_INFO, kTAG, __VA_ARGS__))
#define LOGW(...) \
  ((void)__android_log_print(ANDROID_LOG_WARN, kTAG, __VA_ARGS__))
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, kTAG, __VA_ARGS__))

struct GLContext {

  EGLDisplay display_;
  EGLSurface surface_;
  EGLContext context_;
  int32_t width_;
  int32_t height_;

    bool initialized_;
};
GLContext glcontext;

struct ExampleData {
  NVGcontext *vg;
  DemoData data;
  PerfGraph fps;

  std::chrono::steady_clock::time_point startt;
  std::chrono::steady_clock::time_point prevt;
};
ExampleData exampleData;

android_app *androidAppCtx = nullptr;

void CreateExampleData() {

  exampleData.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

  if (loadDemoData(exampleData.vg, &exampleData.data) == -1)
    abort();

  initGraph(&exampleData.fps, GRAPH_RENDER_FPS, "Frame Time");

  exampleData.startt = std::chrono::steady_clock::now();
  exampleData.prevt = exampleData.startt;
}

bool InitExample(android_app *app) {
  androidAppCtx = app;

  // copy resource from assets and change cwd for demo
  std::string resource_dir = app->activity->externalDataPath + std::string("/example");
  copyFromAssets(app->activity->assetManager, "", resource_dir);
  copyFromAssets(app->activity->assetManager, "images", resource_dir + "/images");
  chdir(resource_dir.c_str());

  const EGLint attribs[] = {
      EGL_BLUE_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_RED_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_STENCIL_SIZE, 8,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE};

  const EGLint contextAttribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE};


  glcontext.display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  eglInitialize(glcontext.display_, 0, 0);

  EGLint numConfigs;
  EGLConfig config;

  GLint ret = eglChooseConfig(glcontext.display_, attribs, &config, 1, &numConfigs);
  assert(ret);
  assert(numConfigs > 0);

  glcontext.surface_ = eglCreateWindowSurface(glcontext.display_, config, androidAppCtx->window, NULL);
  glcontext.context_ = eglCreateContext(glcontext.display_, config, NULL, contextAttribs);

  if (eglMakeCurrent(glcontext.display_, glcontext.surface_, glcontext.surface_, glcontext.context_) == EGL_FALSE) {
    LOGW("Unable to eglMakeCurrent");
    return -1;
  }

    EGLint w, h;
  eglQuerySurface(glcontext.display_, glcontext.surface_, EGL_WIDTH, &w);
  eglQuerySurface(glcontext.display_, glcontext.surface_, EGL_HEIGHT, &h);
    glcontext.width_ = w;
    glcontext.height_ = h;

  CreateExampleData();

    glcontext.initialized_ = true;

  return true;
}

void DeleteExampleData() {

  freeDemoData(exampleData.vg, &exampleData.data);
  nvgDeleteGLES2(exampleData.vg);
}

void DeleteExample(void) {
  DeleteExampleData();

  eglMakeCurrent(glcontext.display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (glcontext.context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(glcontext.display_, glcontext.context_);
  }
  if (glcontext.surface_ != EGL_NO_SURFACE) {
    eglDestroySurface(glcontext.display_, glcontext.surface_);
  }
  eglTerminate(glcontext.display_);

    glcontext.initialized_ = false;
}

bool IsExampleReady(void) {
  return glcontext.initialized_;
}

bool ExampleDrawFrame(void) {

    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    double dt = std::chrono::duration_cast<std::chrono::milliseconds>(t - exampleData.prevt).count() / 1000.0;
    double st = std::chrono::duration_cast<std::chrono::milliseconds>(t - exampleData.startt).count() / 1000.0;
    exampleData.prevt = t;

  glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);


    updateGraph(&exampleData.fps, (float)dt);

    nvgBeginFrame(exampleData.vg, glcontext.width_, glcontext.height_, 1.0f);
    renderDemo(exampleData.vg, 0, 0, glcontext.width_, glcontext.height_, (float)st, 0, &exampleData.data);
    renderGraph(exampleData.vg, 5, 5, &exampleData.fps);

    nvgEndFrame(exampleData.vg);

  eglSwapBuffers(glcontext.display_, glcontext.surface_);
  return true;
}