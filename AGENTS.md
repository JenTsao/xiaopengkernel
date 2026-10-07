# AGENTS.md — XiaopengKernel 开发规范

本文件是 AI 助手与贡献者在本仓库工作时的操作规范。项目背景与技术架构见 [README.md](README.md)。

## 核心规则：验证只在云端 CI

**本地不构建、不运行测试。GitHub Actions 是唯一验证入口。**

标准工作流：

1. 修改代码（本地只做静态检查：读代码、grep、对齐既有风格）
2. `git commit`（遵循提交规范）
3. `git push` 到 `main` 或功能分支
4. 用 `gh run watch` / `gh run list --limit 3` 盯 CI 结果
5. CI 失败 → 读日志定位 → 修复 → 再次提交推送，直到绿

**Why:** 本地工具链与 CI 不一致（不同版本 GCC/MinGW、DLL 依赖差异），本地结果不可信；且 CI 矩阵覆盖 Linux + Windows 双平台，本地无法复现。

## 仓库结构

- `include/`（69 个 .hpp）— **绝大多数实现是 header-only**：CSS/DOM 解析、布局算法（Block/Inline/Flex/Grid）、层叠上下文、Loader 层都在这里
- `src/`（20 个 .cpp）— 引擎入口（`main.cpp`、`demo_minimal_main.cpp`）、SDL 窗口、渲染器、脚本绑定、事件循环等需要独立编译单元的部分
- `tests/`（24 个 .cpp）— 自研极简测试框架 `test_framework.hpp` + 各模块测试，经 CTest 管理（19 个测试目标）
- `third_party/` — Windows/MinGW 预编译依赖（curl、SDL2、QuickJS、FreeType、HarfBuzz），**不要修改第三方代码**
- `demo/`、`docs/` — 演示页面与 API 文档

## 构建与测试命令（CI 与环境参考）

Linux（需 `libcurl4-openssl-dev libsdl2-dev libgl1-mesa-dev libfreetype6-dev libharfbuzz-dev pkg-config`）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_FREETYPE=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

Windows / MinGW（依赖已打包在 `third_party/`，开箱即用）：

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DENABLE_FREETYPE=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

说明：

- CMake 会在配置阶段自动把 MinGW 运行时 DLL（`libstdc++-6.dll` 等）复制到 `bin/`，ctest 无需手工配置 PATH。
- `ENABLE_FREETYPE` 默认关闭；CI 显式开启以覆盖 FreeType/HarfBuzz 代码路径。
- 测试可执行文件统一输出到 `bin/`。

## 提交规范

- Conventional Commits + 中文描述，与仓库既有历史一致：
  - `feat(layout, renderer): xxx` / `fix(script): xxx` / `ci: xxx` / `docs: xxx` / `chore: xxx` / `test: xxx`
- 一次提交一个逻辑单元；不提交与本项目无关的文件。
- push 前确认 `git status` 干净（无意外未跟踪文件混入）。

## CI

- 工作流：`.github/workflows/ci.yml` — Linux + Windows 双平台矩阵，Release 构建 + `ctest`，push 到 `main`/`master` 与 PR 触发。
- CI 是事实上的回归门禁：任何行为变更必须伴随测试更新。

## 其他约定

- 文档（README 等）必须反映真实状态：占位/未实现/已知问题要如实标注，不写未兑现的特性。
- 新功能必须带测试；修 BUG 必须先有能复现问题的测试（在 CI 上验证红→绿）。
- 代码风格跟随现有文件：2 空格缩进为主，命名混用 `camelCase` 成员与 `m_` 前缀私有字段，改动时保持文件内一致。
