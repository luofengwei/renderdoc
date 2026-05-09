# RenderDoc Development Log

> Source: `E:\UGitSpace\RDCLoopRunner\renderdoc_src` (v1.43, branch rdc-patches-v1.43-feb27)
> Build output: `x64\Development\`

---

## 2026-05-09: SP-Capturable Local ReplayLoop + EGL Profiler Visibility

### Problem

Snapdragon Profiler (SP) 无法对 RenderDoc replay 做 Trace Capture / Take Snapshot：
1. renderdoccmd 被 SP Launch 后无 Intent args → 原逻辑直接退出
2. RenderDoc replay 的 EGL 函数通过 `dlsym` 获取，绕过了 SP 的 profiler hook 层
3. replay 默认用 `Balanced` optimisation → `FillWithDiscardPattern` 每帧插 15 个全屏 blit（+27.6% fragment 开销）

### Fix

**Files**: `renderdoccmd/renderdoccmd_android.cpp`, `renderdoc/driver/gl/egl_platform.cpp`

#### 改动 1: SP Local ReplayLoop (`renderdoccmd_android.cpp`)

新增 `RunLocalReplayLoop()` 函数（~220 行），当 `cmdthread()` 无 Intent args 时调用：
- 读设备上的 `sp_replay_config.json`（rdc 路径 + duration）
- 本地 `RENDERDOC_OpenCaptureFile` → `OpenCapture(Fastest)` → `ReplayLoop(window, texid)`
- timer 线程到时间后 `CancelReplayLoop()`
- 画面渲染到 ANativeWindow → eglSwapBuffers → SP 帧边界可见

隔离性：
- 有 Intent args（`remoteserver` 等）→ 走原 `renderdoccmd(env, args)` 逻辑，零改动
- 无 Intent args 且无 config 文件 → 打印日志后退出（与原行为一致）

#### 改动 2: EGL Profiler Dispatch (`egl_platform.cpp`)

`PopulateForReplay()` 新增运行时检测：
```cpp
__system_property_get("debug.egl.profiler", profilerProp);
if(profilerProp == "1") {
    // 优先用 eglGetProcAddress 获取 EGL 函数 → SP 能拦截
} else {
    // 原逻辑：dlsym 获取 → 不影响正常 remote replay
}
```

隔离性：
- 只在 Android 平台 + `debug.egl.profiler=1`（SP 活跃时设置）才走新路径
- 正常 remote replay（PC 连设备）→ 该属性不存在 → 走原逻辑
- Windows/Linux → 编译期排除（`#if ENABLED(RDOC_ANDROID)`）

#### 改动 3: OpenCapture 用 Fastest 优化级别

`ReplayOptions.optimisation = Fastest` 跳过 `FillWithDiscardPattern`，消除 27.6% 的 fragment shader 虚高。
- 真机上 `glInvalidateFramebuffer` 是零开销 TBR 提示
- `Balanced` 模式将其变成全屏 draw（纯 debug 可视化）
- `Fastest` 模式跳过 → 与真机一致

### Build

APK 重编（`renderdoccmd_android.cpp` + `egl_platform.cpp` 改动）。DLL/PYD 不需要。

### Result

| 验证项 | 结果 |
|--------|------|
| SP Launch → 画面显示 | ✅ |
| SP Realtime Counter | ✅ |
| SP Take Snapshot（重启 SP 后）| ✅ |
| SP Start Capture（重启 SP 后）| ✅ |
| 正常 remote replay（无 SP）| ✅ 不受影响 |

已知 UX 问题：rdc 加载期（~40s）无帧 → SP 需重启后才能 arm。后续可加 splash 解决。

---

## 2026-04-10: D3D11 SkipInitialContents

### Problem

D3D11 driver 没有 `m_SkipInitialContents`，每帧 `ReplayLog` 都执行 `ApplyInitialContents`。GL driver 已有此优化（2026-04-01），D3D11 需要对齐。

### Fix

**Files**: `renderdoc/driver/d3d11/d3d11_device.h`, `renderdoc/driver/d3d11/d3d11_device.cpp`

对齐 GL driver 实现：

`d3d11_device.h`:
```cpp
bool m_SkipInitialContents = false;  // RDCLoopRunner: skip repeated ApplyInitialContents in ReplayLoop
```

`d3d11_device.cpp` — `ReplayLog` 函数:
```cpp
if(!partial && !m_SkipInitialContents)
{
    ApplyInitialContents();
    m_SkipInitialContents = true;  // skip on subsequent full replays
}
```

### Build

仅 DLL 重编（driver 内部改动，无 API/ABI 变更，PYD/APK 不需要）。

### Result

本地 ReplayLoop 验证（`error.rdc`, D3D11, 32 actions）：
- 683 FPS（2 分钟稳定），帧时间 1.46ms
- SkipInitialContents 从第一秒就生效，FPS 平稳无跳变

---

## 2026-04-07: Phase 10 — Remote ReplayLoop (fire-and-forget RPC)

### Problem

ReplayLoop 的 while 循环跑在 Desktop DLL，每帧走一次 adb 端口转发的 proxy 网络包。断 USB = 断连 = loop 挂掉。PerfDog 功耗测量需要断 USB（USB 充电干扰功耗数据）。

### Fix

**Files**: `replay_proxy.h`, `replay_proxy.cpp`, `renderdoc_replay.h`, `replay_controller.h`, `replay_controller.cpp`

三层修改：

1. **Proxy 协议** (`replay_proxy.h/.cpp`):
   - 新增 enum `eReplayProxy_StartRemoteReplayLoop` / `GetRemoteLoopFrameCount` / `CancelRemoteReplayLoop`（追加末尾，见 MISTAKES M004）
   - `Proxied_RemoteReplayLoopChunk(lastEID, durationMs)`: server 端 `SERIALISE_RETURN` 后进 while loop，GPU 本地执行 `ReplayLog` + `RefreshPreviewWindow`，结束后写结果文件

2. **公共 API** (`renderdoc_replay.h`, `replay_controller.h`):
   - 新增 `virtual uint32_t RemoteReplayLoop(uint32_t durationMs) = 0`
   - SWIG 自动从 `renderdoc_replay.h` 生成 Python 绑定，无需改 `.i`

3. **Controller** (`replay_controller.cpp`):
   - `RemoteReplayLoop()`: 读 `lastEID`，调 `proxy->RemoteReplayLoopChunk(lastEID, durationMs)`，非阻塞返回
   - `ReplayLoop()`: remote 分支改为报错提示用 `RemoteReplayLoop()`

### Architecture

```
Python                              Desktop (DLL)                    Android (APK .so)

controller.RemoteReplayLoop(60000)
  → ReplayController::              → proxy->RemoteReplayLoopChunk
     RemoteReplayLoop(60000)           (lastEID=13306, 60000ms)
                                       ↓ [一次 RPC]
                                    Proxied_RemoteReplayLoopChunk:
                                       SERIALISE_RETURN(0)  ← 立即返回
  ← return 0                          while(timer < 60000ms):
                                         m_Remote->ReplayLog(13306)
  [断 USB]                              RefreshPreviewWindow()
  [sleep 60s]                            frameCount++
  [重连 USB]                          写 remote_loop_result.txt
  adb pull result → parse FPS
```

### Result

| Test | Frames | Elapsed | FPS | Frame ms |
|------|--------|---------|-----|----------|
| 60s 不断 USB | 1506 | 60.0s | 25.1 | 39.9ms |
| 5min 断 USB | 6858 | 300.0s | 22.9 | 43.8ms |

5 分钟下降到 22.9 FPS 是 GPU thermal throttle（长时间满载降频），属正常。

### Build

仅 DLL + PYD 重编，**APK 不需要改**（`RemoteReplayLoopChunk` 的 server 端逻辑已在之前编进 APK）。

---

## 2026-04-01: Skip ApplyInitialContents after first ReplayLog

### Problem

ReplayLoop 每帧调用 `WrappedOpenGL::ReplayLog(0, endEID, eReplay_Full)`，`startEventID=0` 使 `partial=false`，每帧都执行 `ApplyInitialContents()`（重传所有 texture/buffer 到 GPU）。对 `with vft limit.rdc`（503 个 Initial Contents），帧时间从真机 ~33ms 膨胀到 130ms。

### Fix

**Files**: `gl_driver.h`, `gl_driver.cpp`

`WrappedOpenGL` 新增成员 `bool m_SkipInitialContents = false`。`ReplayLog` 中：

```cpp
if(!partial && !m_SkipInitialContents)
{
    ApplyInitialContents();
    m_SkipInitialContents = true;  // auto-skip after first full replay
}
```

第一帧完整执行（资源初始化），后续帧自动跳过。

### Why this approach

- **不改接口类**：不修改 `IRemoteDriver`/`IReplayDriver` 虚函数 → DLL/PYD ABI 不变（见 MISTAKES M005）
- **不改 proxy 协议**：无需新增 `ReplayProxyPacket` enum → 旧 APK 兼容（见 RDCLoopRunner MISTAKES M004）
- **Android 端自管理**：改动编进 APK 的 `.so`，ReplayLoop 通过 proxy 发 ReplayLog packet，Android 端自动跳过后续帧的 ApplyInitialContents

### Architecture insight

```
Desktop (DLL)                          Android (APK .so)
ReplayController::ReplayLoop()
  while(!cancel):
    m_pDevice->ReplayLog(endEID)
      → ReplayProxy(client)
        → network packet ──────→ ReplayProxy(server) Tick()
                                   → m_Remote->ReplayLog()
                                      → GLReplay::ReplayLog(0, endEID)
                                         → WrappedOpenGL::ReplayLog()
                                            → ApplyInitialContents (skipped after 1st)
                                            → replay all GL commands (~33ms GPU)
                                   → RefreshPreviewWindow() (render to phone screen)
        ← void return ←────────
    output->Display()              (headless, ~0.1ms, no network)
```

- ReplayLoop 跑在 Desktop DLL，每帧通过 proxy 发 1 个 ReplayLog packet
- Display() 是 headless + 空 ResourceId，直接 clear，不走网络
- 手机画面由 Android 端 `RefreshPreviewWindow()` 本地渲染

### Result

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Avg FPS | 7.6 | **23.6** | **3.1x** |
| Frame time | 130ms | **42ms** | **-88ms (-68%)** |

### Build

DLL + APK 都需要重编（改动在 `gl_driver.h/cpp`，编进两边）。

```bash
# DLL
/build-renderdoc  (DLL target)

# APK (需 JAVA_HOME + cmake reconfigure)
export JAVA_HOME='C:/Program Files/Android/jdk/jdk-8.0.302.8-hotspot/jdk8u302-b08'
cmake .. [flags]  # see /build-renderdoc skill
cmake --build . --target apk

# 部署 APK（必须彻底卸载再装，见 RDCLoopRunner MISTAKES M007）
adb uninstall org.renderdoc.renderdoccmd.arm64
adb push xxx.apk //data/local/tmp/renderdoc.apk
adb shell 'pm install -r /data/local/tmp/renderdoc.apk'
adb shell appops set org.renderdoc.renderdoccmd.arm64 MANAGE_EXTERNAL_STORAGE allow
```

---

## 2026-03-20: Fix ReplayLoop remote preview window rendering

### Problem

`ReplayController::ReplayLoop()` shows incorrect/partial rendering on Android remote device preview window, while `SetFrameEvent()` renders correctly.

### Root Cause

`replay_controller.cpp` line 1871:
```cpp
m_pDevice->ReplayLog(10000000, eReplay_Full);
```

This passes `endEventID=10000000` to `ReplayLog`, which then sets `m_EventID=10000000` in the proxy layer (`replay_proxy.cpp` line 2070). After each `ReplayLog` RPC, the Android-side `RefreshPreviewWindow()` calls `FindAction(m_EventID)` to find the current action's output texture and render it to the preview window. Since no action exists at EID 10000000, `FindAction` returns NULL, and only a black checkerboard is shown.

Call chain on Android remote:
```
PC: ReplayLoop() → ReplayLog(10000000, eReplay_Full)
                         ↓ network RPC
Android: Tick() → ReplayLog() → m_EventID = 10000000
                → RefreshPreviewWindow()
                    → FindAction(10000000) → NULL  ← action not found!
                    → only draws checkerboard background
```

### Fix

**File**: `renderdoc/replay/replay_controller.cpp`

```diff
- m_pDevice->ReplayLog(10000000, eReplay_Full);
+ m_pDevice->ReplayLog(m_Actions.back()->eventId, eReplay_Full);
```

`m_Actions` is the flattened action list populated during controller initialization. `m_Actions.back()->eventId` returns the actual last event ID of the capture (e.g. 1296 for the test .rdc). The GPU driver already stops at the last event regardless of the number passed, so the replay behavior is identical — only the `m_EventID` value changes, which fixes the preview window lookup.

### SWIG binding change (prior session)

**File**: `qrenderdoc/Code/pyrenderdoc/renderdoc.i` (line 265-271)

Added GIL release for `ReplayLoop` to prevent deadlock when calling `CancelReplayLoop()` from another Python thread:

```swig
%exception IReplayController::ReplayLoop {
  Py_BEGIN_ALLOW_THREADS
  $action
  Py_END_ALLOW_THREADS
}
```

Without this, `ReplayLoop` holds the Python GIL while blocking, and `CancelReplayLoop()` from the main thread cannot acquire GIL → deadlock.

### Verification

```
python scripts/spike_replay_loop.py "examples\三黑开场模型变黑.rdc" --remote
```

- Before fix: phone shows only background (black/checkerboard), models missing
- After fix: phone shows correct full rendering, matching SetFrameEvent output

### Build

Only PC-side `renderdoc.dll` rebuild needed (the fix is in ReplayController which runs on PC):

```
MSBuild renderdoc\renderdoc.vcxproj -p:Configuration=Development -p:Platform=x64 -p:SolutionDir=C:\Users\fengweiluo\src\renderdoc\ -p:PlatformToolset=v143 -v:minimal
```

Output: `x64\Development\renderdoc.dll`
