# RenderDoc Development Log

> Source: `C:\Users\fengweiluo\src\renderdoc` (v1.43, branch v1.x)
> Build output: `C:\Users\fengweiluo\src\renderdoc\x64\Development\`

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
