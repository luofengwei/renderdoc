/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "renderdoccmd.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <locale.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <stdio.h>
#include <sys/system_properties.h>

#include <android_native_app_glue.h>

#include <android/log.h>
#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, "renderdoccmd", __VA_ARGS__);

struct android_app *android_state;
pthread_t cmdthread_handle = 0;

struct PThreadLock
{
  PThreadLock(const char *n) : name(n)
  {
    ANDROID_LOG("Creating lock %s", name);
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &mutexattr);
  }
  ~PThreadLock()
  {
    ANDROID_LOG("Destroying lock %s", name);
    pthread_mutex_destroy(&mutex);
    pthread_mutexattr_destroy(&mutexattr);
  }
  bool trylock() { return pthread_mutex_trylock(&mutex) == 0; };
  void lock() { pthread_mutex_lock(&mutex); }
  void unlock() { pthread_mutex_unlock(&mutex); }
private:
  const char *name;
  pthread_mutex_t mutex;
  pthread_mutexattr_t mutexattr;
};

PThreadLock m_DrawLock("m_DrawLock"), m_CmdLock("m_CmdLock");

void Daemonise()
{
}

// every 15 minutes we fade briefly to avoid burn-in
const float splashFadePeriod = 45 * 60.0f;

float curtime()
{
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return float(ts.tv_sec) + float(ts.tv_nsec & 0xffffffff) / 1000000000.0f;
}

void DisplayGenericSplash()
{
  ANDROID_LOG("Trying to splash");
  // if something else is drawing and holding the lock, then bail
  if(!m_DrawLock.trylock())
    return;
  ANDROID_LOG("Doing a splash");

  // since we're not pumping this continually and we only draw when we need to, we can just do the
  // full initialisation and teardown every time. This means we don't have to pay attention to
  // whether something else needs to create a context on the window - we just do it
  // opportunistically when we can hold the draw lock.
  // This only takes about 30ms anyway, so it's still technically realtime, right!?

  // fetch everything dynamically. We can't link against libEGL otherwise it messes with the hooking
  // in the main library. Just declare local function pointers with the real names
  void *libEGL = dlopen("libEGL.so", RTLD_NOW);

#define DLSYM_GET(name) decltype(&::name) name = (decltype(&::name))dlsym(libEGL, #name);
#define GPA_GET(name) decltype(&::name) name = (decltype(&::name))eglGetProcAddress(#name);

  DLSYM_GET(eglBindAPI);
  DLSYM_GET(eglGetDisplay);
  DLSYM_GET(eglInitialize);
  DLSYM_GET(eglGetError);
  DLSYM_GET(eglChooseConfig);
  DLSYM_GET(eglCreateContext);
  DLSYM_GET(eglCreateWindowSurface);
  DLSYM_GET(eglMakeCurrent);
  DLSYM_GET(eglDestroySurface);
  DLSYM_GET(eglDestroyContext);
  DLSYM_GET(eglGetProcAddress);
  DLSYM_GET(eglSwapBuffers);
  DLSYM_GET(eglTerminate);

  GPA_GET(glCreateShader);
  GPA_GET(glShaderSource);
  GPA_GET(glCompileShader);
  GPA_GET(glCreateProgram);
  GPA_GET(glAttachShader);
  GPA_GET(glLinkProgram);
  GPA_GET(glGetUniformLocation);
  GPA_GET(glUniform2f);
  GPA_GET(glUniform1f);
  GPA_GET(glUseProgram);
  GPA_GET(glVertexAttribPointer);
  GPA_GET(glEnableVertexAttribArray);
  GPA_GET(glDrawArrays);
  GPA_GET(glGetShaderiv);
  GPA_GET(glGetShaderInfoLog);
  GPA_GET(glGetProgramiv);
  GPA_GET(glGetProgramInfoLog);

  eglBindAPI(EGL_OPENGL_ES_API);

  EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  if(eglDisplay && android_state && android_state->window)
  {
    ANativeWindow *previewWindow = android_state->window;

    int major = 0, minor = 0;
    EGLBoolean initialised = eglInitialize(eglDisplay, &major, &minor);

    if(initialised && major >= 1)
    {
      const EGLint configAttribs[] = {EGL_RED_SIZE,
                                      8,
                                      EGL_GREEN_SIZE,
                                      8,
                                      EGL_BLUE_SIZE,
                                      8,
                                      EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT,
                                      EGL_COLOR_BUFFER_TYPE,
                                      EGL_RGB_BUFFER,
                                      EGL_RENDERABLE_TYPE,
                                      EGL_OPENGL_ES2_BIT,
                                      EGL_NONE};

      EGLint numConfigs;
      EGLConfig config;
      if(eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs))
      {
        // we only need GLES 2 for this
        static const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

        EGLContext ctx = eglCreateContext(eglDisplay, config, NULL, ctxAttribs);
        if(ctx)
        {
          EGLSurface surface = eglCreateWindowSurface(eglDisplay, config, previewWindow, NULL);

          if(surface)
          {
            eglMakeCurrent(eglDisplay, surface, surface, ctx);

            // simple pass through shader for the fullscreen triangle verts
            const char *vertex =
                "attribute vec2 pos;\n"
                "void main() { gl_Position = vec4(pos, 0.5, 1.0); }";

            const char *fragment = R"(
precision highp float;

float circle(in vec2 uv, in vec2 centre, in float radius)
{
  return length(uv - centre) - radius;
}

// distance field for RenderDoc logo
float logo(in vec2 uv)
{
  // add the outer ring
  float ret = circle(uv, vec2(0.5, 0.5), 0.275);

  // add the upper arc
  ret = min(ret, circle(uv, vec2(0.5, -0.37), 0.71));

  // subtract the inner ring
  ret = max(ret, -circle(uv, vec2(0.5, 0.5), 0.16));

  // subtract the lower arc
  ret = max(ret, -circle(uv, vec2(0.5, -1.085), 1.3));

  return ret;
}

uniform vec2 iResolution;
uniform float fFade;

void main()
{
  vec2 uv = gl_FragCoord.xy / iResolution.xy;

  // centre the UVs in a square. This assumes a landscape layout.
  uv.x = 0.5 - (uv.x - 0.5) * (iResolution.x / iResolution.y);

  // this constant here can be tuned depending on DPI to increase AA
  float edgeWidth = 10.0/max(iResolution.x, iResolution.y);

  float smoothdist = smoothstep(0.0, edgeWidth, clamp(logo(uv), 0.0, 1.0));

  // the green is #3bb779
  gl_FragColor = mix(vec4(1.0), vec4(0.2314, 0.7176, 0.4745, 1.0), smoothdist)*fFade;
}
)";

            // compile the shaders and link into a program
            GLuint vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vertex, NULL);
            glCompileShader(vs);

            GLint status = 0;
            char buffer[1025] = {0};
            glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
            if(status == 0)
            {
              glGetShaderInfoLog(vs, 1024, NULL, buffer);
              ANDROID_LOG("VS error: %s", buffer);
            }

            GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fragment, NULL);
            glCompileShader(fs);

            status = 0;
            glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
            if(status == 0)
            {
              glGetShaderInfoLog(fs, 1024, NULL, buffer);
              ANDROID_LOG("FS error: %s", buffer);
            }

            GLuint prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glLinkProgram(prog);

            status = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &status);
            if(status == 0)
            {
              glGetProgramInfoLog(prog, 1024, NULL, buffer);
              ANDROID_LOG("Program Error: %s", buffer);
            }

            glUseProgram(prog);

            // set the resolution
            GLuint loc = glGetUniformLocation(prog, "iResolution");
            glUniform2f(loc, float(ANativeWindow_getWidth(previewWindow)),
                        float(ANativeWindow_getHeight(previewWindow)));

            // loop every 15 minutes, with the fade at the end of the period
            float x = splashFadePeriod - fmod(curtime(), splashFadePeriod);

            // multiply by 4 so the fade happens over a little more than a second instead of
            // 6.28 seconds.
            x = float(x) * 4.0f;

            // do one cos loop and finish
            if(x >= M_PI * 2.0f)
              x = M_PI * 2.0f;

            float fade = cosf(x);

            // set the fade
            loc = glGetUniformLocation(prog, "fFade");
            glUniform1f(loc, fade);

            // fullscreen triangle
            float verts[] = {
                -1.0f, -1.0f,    // vertex 0
                3.0f,  -1.0f,    // vertex 1
                -1.0f, 3.0f,     // vertex 2
            };

            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, verts);
            glEnableVertexAttribArray(0);

            glDrawArrays(GL_TRIANGLES, 0, 3);

            eglSwapBuffers(eglDisplay, surface);
          }
          else
          {
            ANDROID_LOG("failed making surface: %x", eglGetError());
          }

          eglMakeCurrent(eglDisplay, 0L, 0L, NULL);
          eglDestroyContext(eglDisplay, ctx);
          eglDestroySurface(eglDisplay, surface);
        }
        else
        {
          ANDROID_LOG("failed making context: %x", eglGetError());
        }
      }
      else
      {
        ANDROID_LOG("failed choosing config");
      }

      eglTerminate(eglDisplay);
    }
    else
    {
      ANDROID_LOG("failed to initialise EGL");
    }
  }

  m_DrawLock.unlock();
  ANDROID_LOG("Done splashing");
}

WindowingData DisplayRemoteServerPreview(bool active, const rdcarray<WindowingSystem> &systems)
{
  static bool wasActive = false;

  // detect when the preview starts or stops
  if(wasActive != active)
  {
    wasActive = active;

    // if we're opening it, aquire the draw lock, otherwise release it.
    if(active)
    {
      ANDROID_LOG("Locking for preview");
      m_DrawLock.lock();
    }
    else
    {
      m_DrawLock.unlock();
      ANDROID_LOG("Unlocking from preview");

      // when we release it, re-draw the splash
      DisplayGenericSplash();
    }
  }

  WindowingData ret = {WindowingSystem::Unknown};

  if(android_state && android_state->window)
    ret = CreateAndroidWindowingData(android_state->window);

  return ret;
}

void DisplayRendererPreview(IReplayController *renderer, TextureDisplay &displayCfg, uint32_t width,
                            uint32_t height, uint32_t numLoops)
{
  ANativeWindow *connectionScreenWindow = android_state->window;

  m_DrawLock.lock();

  IReplayOutput *out = renderer->CreateOutput(CreateAndroidWindowingData(connectionScreenWindow),
                                              ReplayOutputType::Texture);

  out->SetTextureDisplay(displayCfg);

  if(numLoops == 0)
    numLoops = 100;

  for(uint32_t i = 0; i < numLoops; i++)
  {
    renderer->SetFrameEvent(10000000, true);

    ANDROID_LOG("Frame %u", i);
    out->Display();

    usleep(100000);
  }

  m_DrawLock.unlock();
}

// Returns the renderdoccmd arguments passed via am start
// Examples: am start ... -e renderdoccmd "remoteserver"
// -e renderdoccmd "replay /sdcard/capture.rdc"
std::vector<std::string> getRenderdoccmdArgs()
{
  JNIEnv *env;
  android_state->activity->vm->AttachCurrentThread(&env, 0);

  jobject me = android_state->activity->clazz;

  jclass acl = env->GetObjectClass(me);    // class pointer of NativeActivity
  jmethodID giid = env->GetMethodID(acl, "getIntent", "()Landroid/content/Intent;");
  jobject intent = env->CallObjectMethod(me, giid);    // Got our intent

  jclass icl = env->GetObjectClass(intent);    // class pointer of Intent
  jmethodID gseid =
      env->GetMethodID(icl, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");

  jstring jsParam1 = (jstring)env->CallObjectMethod(intent, gseid, env->NewStringUTF("renderdoccmd"));

  std::vector<std::string> ret;
  if(jsParam1)    // Check if arg value found
  {
    ret.push_back("renderdoccmd");
    const char *param1 = env->GetStringUTFChars(jsParam1, 0);
    std::istringstream iss(param1);
    while(iss)
    {
      std::string sub;
      iss >> sub;
      ret.push_back(sub);
    }
  }
  android_state->activity->vm->DetachCurrentThread();

  return ret;
}

// Get the app folder path on Android (mirrors FileIO::GetAppFolderFilename logic)
static std::string GetAndroidAppFolder()
{
  char platformVersionChar[92];
  __system_property_get("ro.build.version.sdk", platformVersionChar);
  int platformVersion = atoi(platformVersionChar);

  const char *package = "org.renderdoc.renderdoccmd.arm64";
  if(platformVersion < 30)
    return std::string("/sdcard/Android/data/") + package + "/files";
  else
    return std::string("/sdcard/Android/media/") + package + "/files";
}

// Simple JSON string value extractor (no dependency on external JSON lib)
static std::string JsonGetString(const std::string &json, const char *key)
{
  std::string searchKey = std::string("\"") + key + "\"";
  size_t pos = json.find(searchKey);
  if(pos == std::string::npos)
    return "";
  pos = json.find(':', pos + searchKey.size());
  if(pos == std::string::npos)
    return "";
  pos = json.find('"', pos + 1);
  if(pos == std::string::npos)
    return "";
  size_t end = json.find('"', pos + 1);
  if(end == std::string::npos)
    return "";
  return json.substr(pos + 1, end - pos - 1);
}

// Simple JSON number value extractor
static double JsonGetNumber(const std::string &json, const char *key)
{
  std::string searchKey = std::string("\"") + key + "\"";
  size_t pos = json.find(searchKey);
  if(pos == std::string::npos)
    return 0.0;
  pos = json.find(':', pos + searchKey.size());
  if(pos == std::string::npos)
    return 0.0;
  // skip whitespace
  pos++;
  while(pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  return atof(json.c_str() + pos);
}

// SP-capturable local ReplayLoop: loads rdc on device, replays to Window Surface with eglSwapBuffers.
// Called when SP launches renderdoccmd without Intent args and sp_replay_config.json exists.
// Uses ReplayLoop(window, texid) — same path as remote replay loop (ReplayLog + Display + swap).
static IReplayController *g_spRenderer = NULL;
static double g_spDurationSeconds = 0;

static void *spTimerThread(void *)
{
  // Sleep for the configured duration, then cancel the replay loop
  usleep((useconds_t)(g_spDurationSeconds * 1000000.0));
  if(g_spRenderer)
  {
    ANDROID_LOG("RunLocalReplayLoop: duration reached, cancelling loop");
    g_spRenderer->CancelReplayLoop();
  }
  return NULL;
}

static void RunLocalReplayLoop()
{
  std::string appFolder = GetAndroidAppFolder();
  std::string configPath = appFolder + "/sp_replay_config.json";

  ANDROID_LOG("RunLocalReplayLoop: checking config at %s", configPath.c_str());

  // Read config file
  FILE *f = fopen(configPath.c_str(), "r");
  if(!f)
  {
    ANDROID_LOG("RunLocalReplayLoop: no config file found, skipping");
    return;
  }

  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);

  std::string jsonContent(fileSize, '\0');
  fread(&jsonContent[0], 1, fileSize, f);
  fclose(f);

  // Parse config
  std::string rdcPath = JsonGetString(jsonContent, "rdc_path");
  double durationSeconds = JsonGetNumber(jsonContent, "duration_seconds");
  int targetFPS = (int)JsonGetNumber(jsonContent, "target_fps");
  (void)targetFPS;    // FPS control not used in ReplayLoop path (runs at GPU native speed)

  if(rdcPath.empty())
  {
    ANDROID_LOG("RunLocalReplayLoop: rdc_path is empty in config");
    return;
  }

  ANDROID_LOG("RunLocalReplayLoop: rdc=%s, duration=%.0fs, targetFPS=%d", rdcPath.c_str(),
              durationSeconds, targetFPS);

  // Wait for window to be available
  int waitCount = 0;
  while((!android_state || !android_state->window) && waitCount < 50)
  {
    usleep(100000);    // 100ms
    waitCount++;
  }

  if(!android_state || !android_state->window)
  {
    ANDROID_LOG("RunLocalReplayLoop: no window available after 5s wait");
    return;
  }

  // Acquire draw lock
  m_DrawLock.lock();
  ANDROID_LOG("RunLocalReplayLoop: draw lock acquired");

  // Open capture file
  ICaptureFile *capFile = RENDERDOC_OpenCaptureFile();
  ResultDetails openResult = capFile->OpenFile(rdcPath.c_str(), "rdc", NULL);

  if(openResult.code != ResultCode::Succeeded)
  {
    ANDROID_LOG("RunLocalReplayLoop: OpenFile failed: %s", openResult.Message().c_str());
    capFile->Shutdown();
    m_DrawLock.unlock();
    return;
  }

  ANDROID_LOG("RunLocalReplayLoop: capture file opened");

  // Open capture → get IReplayController
  // Use Fastest optimisation to skip FillWithDiscardPattern (matches real device behavior)
  ReplayOptions opts;
  opts.optimisation = ReplayOptimisationLevel::Fastest;
  IReplayController *renderer = NULL;
  ResultDetails replayResult = {};
  rdctie(replayResult, renderer) = capFile->OpenCapture(opts, NULL);

  capFile->Shutdown();

  if(replayResult.code != ResultCode::Succeeded || !renderer)
  {
    ANDROID_LOG("RunLocalReplayLoop: OpenCapture failed: %s", replayResult.Message().c_str());
    m_DrawLock.unlock();
    return;
  }

  ANDROID_LOG("RunLocalReplayLoop: replay controller created");

  // Find texture ID for display (same logic as renderdoccmd.cpp DisplayRendererPreview)
  ResourceId texid;

  // Strategy 1: find SwapBuffer texture
  rdcarray<TextureDescription> texs = renderer->GetTextures();
  for(const TextureDescription &desc : texs)
  {
    if(desc.creationFlags & TextureCategory::SwapBuffer)
    {
      texid = desc.resourceId;
      ANDROID_LOG("RunLocalReplayLoop: found SwapBuffer texture");
      break;
    }
  }

  // Strategy 2: if last action is Present, use its copyDestination
  if(texid == ResourceId())
  {
    const rdcarray<ActionDescription> &actions = renderer->GetRootActions();
    if(!actions.empty())
    {
      const ActionDescription *lastAction = &actions.back();
      while(!lastAction->children.empty())
        lastAction = &lastAction->children.back();
      if(lastAction->flags & ActionFlags::Present)
      {
        ResourceId id = lastAction->copyDestination;
        if(id != ResourceId())
        {
          texid = id;
          ANDROID_LOG("RunLocalReplayLoop: using Present copyDestination");
        }
      }
    }
  }

  // Strategy 3: fallback — find the largest 2D texture
  if(texid == ResourceId())
  {
    uint64_t bestArea = 0;
    for(const TextureDescription &desc : texs)
    {
      if(desc.width < 64 || desc.height < 64)
        continue;
      if(desc.dimension != 2)
        continue;
      uint64_t area = (uint64_t)desc.width * (uint64_t)desc.height;
      if(area > bestArea)
      {
        bestArea = area;
        texid = desc.resourceId;
      }
    }
    if(texid != ResourceId())
      ANDROID_LOG("RunLocalReplayLoop: using largest texture (fallback)");
  }

  if(texid == ResourceId())
    ANDROID_LOG("RunLocalReplayLoop: WARNING — no texture found, display may be blank");

  // Use ReplayLoop(window, texid) — same proven path as remote replay loop
  // ReplayLoop internally does: ReplayLog(lastEID) → Display() → eglSwapBuffers, in a loop.
  // It blocks until CancelReplayLoop() is called from another thread.
  ANativeWindow *window = android_state->window;
  WindowingData wnd = CreateAndroidWindowingData(window);

  ANDROID_LOG("RunLocalReplayLoop: starting ReplayLoop (duration=%.0fs)", durationSeconds);

  // Start timer thread to cancel loop after duration
  g_spRenderer = renderer;
  g_spDurationSeconds = durationSeconds;
  pthread_t timerThread;
  pthread_create(&timerThread, NULL, spTimerThread, NULL);

  // This blocks until CancelReplayLoop() is called
  renderer->ReplayLoop(wnd, texid);

  pthread_join(timerThread, NULL);
  g_spRenderer = NULL;

  uint32_t frameCount = renderer->GetReplayLoopFrameCount();
  ANDROID_LOG("RunLocalReplayLoop: DONE — %u frames", frameCount);

  // Write result file
  std::string resultPath = appFolder + "/sp_replay_loop_result.txt";
  FILE *resultFile = fopen(resultPath.c_str(), "w");
  if(resultFile)
  {
    fprintf(resultFile, "frames=%u\nmode=sp_local_replay\n", frameCount);
    fclose(resultFile);
    ANDROID_LOG("RunLocalReplayLoop: result written to %s", resultPath.c_str());
  }

  // Cleanup
  renderer->Shutdown();
  m_DrawLock.unlock();
  ANDROID_LOG("RunLocalReplayLoop: cleanup done");
}

void *cmdthread(void *)
{
  std::vector<std::string> args = getRenderdoccmdArgs();
  if(args.size())
  {
    ANDROID_LOG("Entering cmd thread");
    m_CmdLock.lock();
    GlobalEnvironment env;
    renderdoccmd(env, args);
    m_CmdLock.unlock();
    ANDROID_LOG("Exiting cmd thread");
  }
  else
  {
    // No Intent args — check if SP launched us for local replay
    ANDROID_LOG("No Intent args, checking sp_replay_config.json");
    m_CmdLock.lock();
    RunLocalReplayLoop();
    m_CmdLock.unlock();
    ANDROID_LOG("SP local replay thread done");
  }

  // activity is done and should be closed
  ANativeActivity_finish(android_state->activity);

  return NULL;
}

void handle_cmd(android_app *app, int32_t cmd)
{
  ANDROID_LOG("handle_cmd(%i)", cmd);
  switch(cmd)
  {
    case APP_CMD_INIT_WINDOW:
    {
      ANDROID_LOG("APP_CMD_INIT_WINDOW");
      // if we already have a thread handle, see if it's still running
      if(cmdthread_handle != 0)
      {
        ANDROID_LOG("thread handle exists");
        // if the thread isn't running anymore, we can acquire m_CmdLock. If so, we need to join the
        // thread and start afresh. If we can't acquire the lock, the thread is still running so we
        // leave it alone.
        if(m_CmdLock.trylock())
        {
          ANDROID_LOG("thread is dead, reaping");
          m_CmdLock.unlock();
          // safe to join here, thread will terminate soon if it hasn't already
          pthread_join(cmdthread_handle, NULL);
          cmdthread_handle = 0;
        }
      }

      // if we don't have a command thread, start one.
      if(cmdthread_handle == 0)
      {
        ANDROID_LOG("spawning command thread");
        pthread_create(&cmdthread_handle, NULL, cmdthread, NULL);
      }

      DisplayGenericSplash();
      break;
    }
    case APP_CMD_WINDOW_REDRAW_NEEDED:
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
    {
      ANDROID_LOG("doing misc splash");
      DisplayGenericSplash();
      break;
    }
  }
}

void android_main(struct android_app *state)
{
  android_state = state;
  android_state->onAppCmd = handle_cmd;

  ANDROID_LOG("android_main android_state->window: %p", android_state->window);

  // Used to poll the events in the main loop
  int events;
  android_poll_source *source;
  float lastSplash = curtime();
  do
  {
    // if we're near the end of the splash period force splashes at 30Hz
    if(splashFadePeriod - fmod(curtime(), splashFadePeriod) < 10.0f)
    {
      if(curtime() - lastSplash > 1.0f / 30.0f)
      {
        DisplayGenericSplash();
        lastSplash = curtime();
      }
    }

#if __NDK_MAJOR__ >= 27
    if(ALooper_pollOnce(1, nullptr, &events, (void **)&source) >= 0)
#else
    if(ALooper_pollAll(1, nullptr, &events, (void **)&source) >= 0)
#endif
    {
      if(source != NULL)
        source->process(android_state, source);
    }
  } while(android_state->destroyRequested == 0);

  ANDROID_LOG("android_main exiting");
  android_state = NULL;
}
