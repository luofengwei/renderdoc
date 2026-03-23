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
