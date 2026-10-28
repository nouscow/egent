# Egent — Open-Source Manual / 开源手册

> **Bilingual document.** English is the primary language; a Chinese summary follows at the end.

---

## ⚠️ Project Status: Archived (已归档)

This repository is **archived and no longer actively maintained**. The author has moved on to other work; no new features, bug fixes, or pull-request merges are planned.

You are, however, completely free to:

- **Fork** it and build your own version.
- **Study** the architecture and reuse any part under the terms of the **LGPL-3.0** license.
- **Modify and redistribute** it, as long as you comply with LGPL-3.0 (see *License & Compliance* below).

If you create something useful on top of Egent, please maintain your own fork — this upstream will not be updated.

---

## 1. What is Egent

Egent is a **local AI desktop client for Windows**, built with **Qt 6 Widgets + C++17**. It lets you chat with any **OpenAI-compatible API** (Bailian/DashScope, DeepSeek, OpenAI, and any service that speaks the OpenAI `/chat/completions` protocol). Highlights:

- Multi-provider chat with on-the-fly model switching.
- Streaming responses (token-by-token rendering).
- Conversations persisted in **SQLite**; exportable to HTML.
- Theming driven by **JSON tokens + QSS** (no hardcoded colors, no third-party QSS library).
- An **optional Agent mode** that can read/search/run/write files in a sandbox, gated by a permission prompt and a command blacklist.
- A **plugin & skill system** (Qt plugin mechanism) for extending behavior without touching core code.
- **Local-only API keys** — read from environment variables or the settings panel, never uploaded.

Current version: **v0.2.0** (tag `v0.2`), covering development phases 0–5 (visuals/auth → core skills → UX polish → plugin ecosystem → engineering wrap-up).

---

## 2. Architecture Overview

Egent follows a strict two-layer split:

| Layer | Location | Responsibility | Hard rule |
| --- | --- | --- | --- |
| **Core** | `core/` | Pure logic: network, parsing, storage, skills, plugins, tools | **No `QWidget` / no `QtGui`.** Must be unit-testable without a GUI. |
| **UI** | `ui/` + `main.cpp` / `mainwindow.*` | Qt Widgets, chat bubbles, settings dialog, docking | Depends on core; renders core data. |

Image data crosses the core↔UI boundary as **`QByteArray`** (never `QPixmap` in core).

### `core/` module map

| Module | Files | Purpose |
| --- | --- | --- |
| **providers** | `core/providers/` | `IModelProvider` interface, `OpenAiCompatProvider` (generic OpenAI-compatible chat + image gen), `ProviderRegistry` (singleton, holds built-in providers, persists current selection). |
| **sessionstore** | `core/sessionstore.*` | SQLite multi-session storage (date grouping, message CRUD, image-path persistence). |
| **skills** | `core/skills/` | `ISkill` interface, `SkillRegistry` (case-insensitive command registry, supports override/unregister), built-in skills (`/help`, `/time`, `/calc`). |
| **plugins** | `core/plugins/` | `IBailianPlugin` contract (IID `com.egent.IBailianPlugin/1.0`), `PluginContext` (injected registries + data dir + log callback + optional parent window), `PluginLoader` (scans `plugins/*.dll`, isolates failures, unloads on exit). |
| **tools** | `core/tools/` | Read-only tools (`list_dir`/`read_file`/`search_text` via `PathGuard` workspace allow-list), `runcommandtool` (30s timeout, 14-command blacklist, 4-tier confirm), `writefiletool` (create/overwrite, ≤1 MB, no delete), `ToolRegistry`, `AuditLog` (append-only local log of every tool call). |
| **agentloop** | `core/agentloop.*` | Drives the SSE `tool_calls` loop: asks the model, executes tools, feeds results back. |
| **sseparser** | `core/sseparser.*` | Server-Sent-Events streaming parser. |
| **models** | `core/models.*` | Message/role data structures. |

### Theming

Themes are **JSON token sets + QSS templates**. All colors come from tokens; nothing is hardcoded. No external QSS/styling library is used (architectural iron rule).

---

## 3. Directory Layout

```
egent/
├── CMakeLists.txt          # Build + test targets
├── main.cpp                # App entry; loads plugins, builds MainWindow
├── mainwindow.{h,cpp,ui}   # Top-level window, provider/model UI, session menu
├── core/                   # Pure logic (no QtGui)
│   ├── providers/  skills/  plugins/  tools/  agentloop.*  sseparser.*  sessionstore.*  models.*
├── ui/                     # Qt Widgets
│   ├── chatview.*  messagebubble.*  avatarwidget.*  avatarstore.*  settingsdialog.*
├── plugins/
│   └── helloplugin/        # Example plugin: registers /hello skill
├── theme/                  # JSON token themes + QSS templates
├── resources/              # Icons (SVG), etc.
├── tests/                  # 9 test programs
├── licenses/
│   └── LGPL-3.0-only.txt   # Qt's license text (shipped with distribution)
├── dist/                   # (gitignored) packaged output from `cmake --install`
├── build-release/          # (gitignored) build directory
└── temp/                   # (gitignored) scratch files
```

---

## 4. Build from Source (Windows / MinGW)

### Prerequisites

- **Qt 6.5+** with modules: `Core`, `Widgets`, `Network`, `Sql`, `Svg`, `LinguistTools`.
  - Verified dev environment: Qt 6.11.0 (MinGW 13.1.0, posix-seh) + CMake 4.2.3 + Ninja.
- **CMake 3.19+**.
- **MinGW** toolchain (the one bundled with Qt Creator works: `mingw1310_64`).
- **Ninja** generator (recommended).

> ⚠️ **Windows-only.** Egent is built with MinGW and uses `QDockWidget`/Win-specific packaging. Other platforms are neither supported nor tested.

### Configure & build

```bash
# from the repo root
cmake -S . -B build-release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.11.0/mingw_64"
cmake --build build-release
```

All artifacts (the `egent.exe`, test programs, and plugins) land in `build-release/`.

### Package (optional)

```bash
cmake --install build-release --prefix "D:/code/QTcode/egent/dist"   # prefix MUST be absolute
# then deploy Qt runtime:
windeployqt dist/bin/egent.exe
# and copy the example plugin:
copy build-release/plugins/helloplugin.dll dist/bin/plugins/
```

`dist/bin/egent.exe` then runs standalone on a clean machine.

---

## 5. Running Tests

There are **9 test programs**. The first **7 run fully offline**; the last **2 require network + a real API key**.

```bash
cmake --build build-release --target ^
  theme_selftest session_test tool_test cmd_test ^
  skill_test plugin_test settingsdialog_test

./build-release/theme_selftest.exe          # theme tokens + QSS render
./build-release/session_test.exe            # SQLite session storage
./build-release/tool_test.exe               # read-only tools, command blacklist, audit log
./build-release/cmd_test.exe                # command exec, file write, non-freezing UI
./build-release/skill_test.exe              # core skill system
./build-release/plugin_test.exe             # plugin load/unload/failure-isolation
./build-release/settingsdialog_test.exe     # settings dialog + local quota record (Plan B)
```

Network + key required:

```bash
cmake --build build-release --target provider_live_test agent_test
./build-release/provider_live_test.exe     # streaming call (real DashScope key)
./build-release/agent_test.exe              # agent tool-call loop
```

### Headless / CI note

GUI tests can run without a display using Qt's offscreen platform:

```bash
set QT_QPA_PLATFORM=offscreen
set QT_QPA_PLATFORM_PLUGIN_PATH=C:/path/to/Qt/6.11.0/mingw_64/plugins
set QT_FORCE_STDERR_LOGGING=1
./build-release/settingsdialog_test.exe
```

---

## 6. How to Extend Egent

### 6.1 Add a chat provider (`IModelProvider`)

Most OpenAI-compatible services need **no new code** — just a new `Config`. Built-ins are registered in `core/providers/providerregistry.cpp → registerBuiltins()`:

```cpp
OpenAiCompatProvider::Config myProviderConfig() {
    return {
        /*id*/          QStringLiteral("myprovider"),
        /*displayName*/ QStringLiteral("My Provider"),
        /*endpoint*/    QUrl(QStringLiteral("https://api.myprovider.com/v1/chat/completions")),
        /*apiKeyEnv*/   QStringLiteral("MYPROVIDER_API_KEY"),   // env var name
        /*models*/      {QStringLiteral("model-a"), QStringLiteral("model-b")},
    };
}

void ProviderRegistry::registerBuiltins() {
    for (const auto &cfg : {bailianConfig(), deepseekConfig(),
                            openaiConfig(), myProviderConfig()}) {
        registerProvider(new OpenAiCompatProvider(cfg));
    }
}
```

- The API key is read from the **environment variable** named in `apiKeyEnv`, falling back to the value saved in the settings panel.
- For a **non-OpenAI protocol**, subclass `IModelProvider` directly and implement `chatStream()` / `generateImage()` / `cancel()`, then register your instance the same way.

### 6.2 Add a skill (`ISkill`)

Skills are local commands (prefixed with `/`) executed without calling the model.

```cpp
// in core/skills/
class MySkill : public ISkill {
public:
    QString command()  const override { return QStringLiteral("mycmd"); }
    QString summary()  const override { return QStringLiteral("Does something useful"); }
    QString help()     const override { return QStringLiteral("Usage: /mycmd <args>"); }
    QString execute(const QString &args) override {
        return QStringLiteral("result for: ") + args;
    }
};

// register (e.g. in builtinskills.cpp or a plugin's install()):
SkillRegistry::instance().registerSkill(new MySkill);
```

- Command names are **case-insensitive**; registration can be overridden/unregistered (used by plugin uninstall).
- Registered skills automatically appear in the `/命令菜单` (command menu) and `/help`.

### 6.3 Add a plugin (`IBailianPlugin`)

Plugins use Qt's plugin mechanism and are scanned from `plugins/*.dll` at startup.

```cpp
// myplugin.h
class MyPlugin : public IBailianPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.egent.IBailianPlugin/1.0")
public:
    QString id()          const override { return "com.example.myplugin"; }
    QString name()        const override { return "My Plugin"; }
    QString version()     const override { return "1.0.0"; }
    QString description() const override { return "Registers /mycmd"; }
    QString install(PluginContext *ctx) override {
        ctx->skillRegistry()->registerSkill(new MySkill);
        return {};                                   // empty = success
    }
    void uninstall() override { /* symmetric teardown */ }
};

// factory
extern "C" IBailianPlugin *createPlugin() { return new MyPlugin; }
```

Build with `qt_add_plugin` in CMake, then deploy the resulting `.dll` into the app's `plugins/` directory (see `plugins/helloplugin` for a complete example). `PluginContext` injects `SkillRegistry`, `ProviderRegistry`, a per-plugin `dataDir`, a log callback, and an optional parent window — **core stays free of any UI dependency**.

A single plugin failing to load is **isolated** and never crashes the host; all plugins are unloaded in reverse order on exit (no dangling contexts).

---

## 7. License & LGPL-3.0 Compliance

- **Egent's own source code** is licensed under **LGPL-3.0-only** — see the root [`LICENSE`](./LICENSE) file.
- **Qt** is also LGPL-3.0; its full license text ships in [`licenses/LGPL-3.0-only.txt`](./licenses/LGPL-3.0-only.txt) and is copied to `dist/LICENSE-Qt-LGPLv3.txt` on packaging.
- **Markdown rendering** uses Qt's built-in `QTextDocument::setMarkdown` (md4c) — no extra library.
- **No fonts are bundled**; system fonts are used.

Egent complies with LGPLv3 by:

1. **Not modifying** Qt's source.
2. **Dynamic linking** — Qt DLLs (`Qt6*.dll`) ship separately from `egent.exe` and can be replaced by the user.
3. Shipping the **license text** with the distribution.
4. Making the **corresponding source** available (this repository).

> If you ever statically link Qt (single-file distribution), LGPL-3.0 requires you to offer object files so users can relink — or purchase a commercial Qt license. Egent itself uses dynamic linking and does not face this constraint.

---

## 8. How to Fork

1. Click **Fork** on GitHub, or `git clone` and point to your own remote.
2. Build per §4.
3. Hack away. No Contributor License Agreement (CLA) is required — just keep the code under LGPL-3.0 and preserve the license/copyright notices.

Pull requests to this archived upstream will not be merged, but forks are warmly encouraged.

---

## 中文说明（摘要）

**项目状态：已归档，不再维护。** 作者已停止开发，不会新增功能、修复或合并 PR。你可以自由 fork、学习、修改、再分发（须遵守 LGPL-3.0）。

**Egent 是什么**：Windows 本地 AI 桌面客户端（Qt 6 Widgets + C++17），对接任意 OpenAI 兼容 API（百炼/DeepSeek/OpenAI 等），支持流式对话、SQLite 会话持久化、JSON 令牌+QSS 主题、可选 Agent 工具（读/搜/执行/写文件，带权限确认与黑名单）、以及基于 Qt 插件机制的插件/技能系统。API Key 仅留本地。

**架构**：严格分层 —— `core/`（纯逻辑，**不依赖 QWidget/QtGui**，可单测）与 `ui/` + `mainwindow`（Qt 控件）。图片以 `QByteArray` 跨边界。详见上文模块表。

**构建**（仅 Windows/MinGW）：Qt 6.5+（含 Core/Widgets/Network/Sql/Svg/LinguistTools）+ CMake 3.19+ + MinGW 13.1.0 + Ninja。命令见 §4。

**测试**：9 个程序，前 7 个离线可跑，后 2 个需网络与真实 Key；无显示器可用 `QT_QPA_PLATFORM=offscreen` 跑 GUI 测试（见 §5）。

**扩展方式**：
- 加厂商：在 `ProviderRegistry::registerBuiltins()` 增加一个 `OpenAiCompatProvider::Config`（非 OpenAI 协议则子类化 `IModelProvider`）。
- 加技能：实现 `ISkill` 并 `SkillRegistry::registerSkill(...)`，命令名大小写不敏感，自动进 `/命令菜单` 与 `/help`。
- 加插件：实现 `IBailianPlugin`（IID `com.egent.IBailianPlugin/1.0`）+ `Q_PLUGIN_METADATA` + 工厂函数，用 `qt_add_plugin` 构建，部署到 `plugins/`，`PluginLoader` 自动扫描加载、失败隔离、退出逆序卸载。

**许可**：Egent 自身代码 LGPL-3.0（根目录 `LICENSE`）；Qt 同为 LGPL-3.0（`licenses/` 内随包携带）。合规要点：不改 Qt 源码、动态链接、随包带许可文本、提供对应源码。

**Fork**：直接 fork 改远端即可，无需 CLA；本上游归档不再合并 PR。
