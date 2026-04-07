# RenderDoc Development Log

> Source: `E:\UGitSpace\RDCLoopRunner\renderdoc_src` (v1.43, branch rdc-patches-v1.43-feb27)
> Build output: `x64\Development\`

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
