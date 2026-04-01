# MISTAKES.md — RenderDoc Fork

编译、修改、SWIG 绑定相关的错题集。

**新 CLI 窗口必读此文件。**

---

## M001: ReplayLoop 传入超大 eventId 导致远程预览黑屏

**错误**: `ReplayLoop()` 在 Android 远程设备上只显示棋盘格背景，模型不可见。

**根因**: `ReplayLog(10000000, eReplay_Full)` 设置 `m_EventID=10000000`，`FindAction()` 找不到对应 action → 预览窗口无法获取输出纹理。

**正确做法**: 使用 `m_Actions.back()->eventId` 获取实际最后一个 event ID。

**教训**: RenderDoc 内部很多地方依赖 `m_EventID` 做查找，传入的值必须是真实存在的 eventId。

---

## M002: ReplayLoop 不释放 GIL 导致 Python 死锁

**错误**: Python 调用 `ReplayLoop()` 后，主线程调用 `CancelReplayLoop()` 永远 hang。

**根因**: `ReplayLoop()` 是阻塞调用，持有 GIL。另一个线程的 `CancelReplayLoop()` 需要 GIL → 死锁。

**正确做法**: 在 `renderdoc.i` 中为 `ReplayLoop` 添加 `%exception` + `Py_BEGIN_ALLOW_THREADS`。

**教训**: 任何 SWIG 绑定的长时间阻塞 C++ 函数都必须释放 GIL。

---

## M003: 编译 pyd 必须用 skill

**错误**: 手动 MSBuild 编译反复失败（flag 顺序、环境变量、toolset 版本）。

**根因**: 编译流程有多个非显而易见的细节。

**正确做法**: 使用 RDCLoopRunner 的 `/build-renderdoc` skill。

**教训**: 见 RDCLoopRunner 全局 MISTAKES.md M002。

---

## M004: DLL git hash 与 APK versionName 不匹配 → 远程回放降速 3x

**错误**: ReplayLoop 从 60 FPS 降到 18.9 FPS，`StartRemoteServer` 返回 `Failed to verify installed Android remote server`。

**根因**: MSBuild 的 `renderdoc_version.vcxproj` 自动读取 git HEAD hash 作为 `GIT_COMMIT_HASH`。在 patch 分支上 HEAD 是 patch commit（`f4735a45`），而 APK 的 `versionName` 是 upstream base commit（`286e07140d96...`）。`android.cpp` 的 `CheckAndroidServerVersion()` 比较两者不一致 → verify 失败 → 重装 APK 但版本仍不匹配 → 权限丢失/server 异常 → 降级通信。

**正确做法**: 在 `renderdoc/replay/version.cpp` 中 `#undef` + `#define GIT_COMMIT_HASH` 为 upstream base commit 的完整 40 字符 hash。上游注释本身就建议："local patches should still point to the hash of the base tree"。

**修改文件**: `renderdoc/replay/version.cpp`（已应用）

**教训**: 在 fork 分支上做 patch 后，**必须** pin base commit hash。不要依赖 MSBuild 自动取 HEAD——它会取到 patch commit 而非 base。

---

## M005: 修改 replay_driver.h 虚函数 → DLL/PYD ABI 不兼容 → segfault

**错误**: 新 DLL 加载后，Python 调用 `controller.GetRootActions()` 时 segfault。

**根因**: 在 `IRemoteDriver`（`replay_driver.h`）中新增虚函数改变了虚表布局。旧 PYD 的虚函数偏移与新 DLL 不一致 → 任何虚函数调用都可能跳到错误地址。

**正确做法**: 避免修改接口类的虚函数。改在具体实现类（如 `WrappedOpenGL`）内部用成员变量解决。若必须改接口，DLL 和 PYD 同时重编。

**教训**: `replay_driver.h` 的 `IRemoteDriver`/`IReplayDriver` 是 DLL ↔ PYD 的 ABI 边界。新增虚函数 = 破坏 ABI。
