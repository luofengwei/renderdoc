# CLAUDE.md — RenderDoc Fork (RDCLoopRunner patches)

## 项目身份

这是 [baldurk/renderdoc](https://github.com/baldurk/renderdoc) 的 fork，用于 RDCLoopRunner 项目的自定义修改。
**不是独立项目**，是 RDCLoopRunner 的上游依赖。

## 分支策略

| 分支 | 用途 |
|------|------|
| `v1.x` | 跟踪上游 baldurk/renderdoc（不直接修改） |
| `rdc-patches-v1.43-feb27` | **工作分支** — 所有 RDCLoopRunner 需要的修改 |

## 当前修改清单

1. `replay_controller.cpp` — ReplayLoop 使用 `m_Actions.back()->eventId`（修复远程预览）
2. `renderdoc.i` — SWIG 绑定添加 GIL 释放（防止 Python 死锁）
3. `renderdoc_replay.h` + `replay_controller.h` — `GetReplayLoopFrameCount()` API
4. `QRDInterface.h` — 新增接口声明

## 工作原则

继承 RDCLoopRunner 的 CLAUDE.md 规范：
- 苏格拉底式需求确认
- 修改代码前必须先读目标文件
- 禁止大规模猜测性修改
- 改完代码同步更新 DEVLOG.md

## 上下文加载

```
CLAUDE.md（本文件）
├── @MISTAKES.md          # 编译/修改错题集
├── @DEVLOG.md            # 开发日志（修改记录）
└── @build_pyd.bat        # 编译 pyd 脚本
```

## 编译

使用 RDCLoopRunner 的 `/build-renderdoc` skill，不要手动编译。

## 环境信息

- 上游仓库: `upstream` → https://github.com/baldurk/renderdoc
- Fork 仓库: `origin` → https://github.com/luofengwei/renderdoc
- 宿主项目: `E:\UGitSpace\RDCLoopRunner\`（本目录是其子目录，外层 .gitignore 忽略）
- 构建输出: `x64\Development\`
- Python 绑定: `x64\Development\pymodules\renderdoc.pyd`（Python 3.10）

## 任务路由

| 任务类型 | 先读什么 |
|---------|---------|
| 任何任务 | `MISTAKES.md` |
| 接续开发 | `DEVLOG.md` |
| 编译问题 | RDCLoopRunner 的 `/build-renderdoc` skill |
| 新修改 | DEVLOG → 相关源码 → 出 Plan |
