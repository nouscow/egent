# Egent

> ⚠️ **Archived / 已归档.** This project is no longer maintained. No new features, fixes, or PR merges are planned — feel free to fork and build on it. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full open-source manual (build, architecture, how to extend, license).

Egent is a local AI desktop client for Windows. Chat with any OpenAI-compatible API: Bailian, DeepSeek, OpenAI and more. Switch model on the fly. Chats persist in SQLite; themes come from JSON tokens. Optional Agent mode reads, searches, runs and writes files in a sandbox, gated by a permission prompt and command blacklist. Keys stay local.

本地 AI 客户端：多厂商对话（百炼 / DeepSeek / OpenAI）、可切换主题、会话持久化，并支持可选的本地工具调用（读文件 / 执行命令 / 写文件）。

> ⚠️ **本项目已归档，停止维护。** 不再有新功能、修复或合并 PR；欢迎 fork 自行发展。完整开源手册（构建 / 架构 / 扩展方式 / 许可）见 [`CONTRIBUTING.md`](./CONTRIBUTING.md)。

当前版本：**v0.2.0**（tag `v0.2`；已覆盖 Phase 0–5：视觉/认证 → 核心技能 → 体验补强 → 插件生态 → 工程收尾）。

## 构建

依赖 Qt 6.5+（需包含 Core / Widgets / Network / Sql / Svg / LinguistTools）与 CMake 3.19+。开发验证环境：**Qt 6.11.0（MinGW 13.1.0, posix-seh）+ CMake 4.2.3 + Ninja**。

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt 路径>
cmake --build build-release
```

构建产物统一输出到 `build-release/`（测试程序、egent.exe 与插件均在其中；根目录 `build/` 为早期遗留，已废弃）。

### 打包（dist）

```bash
cmake --install build-release --prefix D:/code/QTcode/egent/dist   # 注意 prefix 必须是绝对路径
```

install 后 `dist/bin/egent.exe` 可直接运行（Qt 运行时随包携带）。发布完整可分发包还需两步：

1. 部署应用插件：`build-release/plugins/helloplugin.dll` → `dist/bin/plugins/`（`dist/plugins/` 是 Qt 系统插件目录，二者勿混淆）；
2. 执行 `windeployqt dist/bin/egent.exe` 补齐 Qt 平台插件 / 翻译 / 编译器运行库，保证纯净环境自包含运行。

## 运行配置

API Key 读取顺序：**环境变量优先，其次设置面板中保存的值**。

| 厂商 | 环境变量 | 默认端点 |
| --- | --- | --- |
| 百炼 | `DASHSCOPE_API_KEY` | `dashscope.aliyuncs.com/compatible-mode/v1` |
| DeepSeek | `DEEPSEEK_API_KEY` | `api.deepseek.com/v1` |
| OpenAI | `OPENAI_API_KEY` | `api.openai.com/v1` |

## 测试

共 9 个测试程序；前 7 个离线可跑，后 2 个需网络与 API Key。

```bash
cmake --build build-release --target theme_selftest session_test tool_test cmd_test skill_test plugin_test settingsdialog_test
./build-release/theme_selftest.exe        # 主题令牌与 QSS 渲染（无需网络）
./build-release/session_test.exe          # SQLite 会话存储
./build-release/tool_test.exe             # 只读工具、命令黑名单与工具审计日志
./build-release/cmd_test.exe              # 命令执行、文件写入、界面不冻结
./build-release/skill_test.exe            # 核心技能系统（注册表 / help / time / calc）
./build-release/plugin_test.exe           # 插件加载 / 卸载 / 失败隔离
./build-release/settingsdialog_test.exe   # 设置对话框与额度本地记录（方案 B）
```

需要网络与 API Key（`provider_live_test` / `agent_test`）：

```bash
cmake --build build-release --target provider_live_test agent_test
./build-release/provider_live_test.exe   # 流式接口（DashScope 真实调用）
./build-release/agent_test.exe           # Agent 工具调用闭环
```

## 插件生态（阶段4）

项目支持通过 Qt 插件机制扩展技能：`core/plugins/ibailianplugin.h` 定义插件契约 `IBailianPlugin`（IID `com.egent.IBailianPlugin/1.0`，沿用 `IModelProvider` 的 `Q_DECLARE_INTERFACE` 范式）。`PluginContext` 向插件注入 **SkillRegistry / ProviderRegistry**、插件专属数据目录（`<AppData>/egent/plugins/<pluginId>`，权限最小化）、日志回调与可选父窗口（core 层零 UI 依赖）。`PluginLoader` 在启动时扫描应用目录 `plugins/*.dll` 自动加载，单插件失败隔离、不中断主程序，退出时统一卸载、不留悬垂上下文。

编写一个插件的三要素：

1. 继承 `IBailianPlugin`，实现元数据（`id()` / `name()` / `version()` / `description()`）与生命周期钩子 `install(PluginContext*)` / `uninstall()`：在 install 中把能力注册进注册表（如 `skillRegistry()->registerSkill(...)`），uninstall 中对称注销；
2. 导出工厂：`Q_PLUGIN_METADATA(IID "com.egent.IBailianPlugin/1.0")` + 返回 `IBailianPlugin*` 的工厂函数；
3. 以 `qt_add_plugin` 加入 CMake，构建后部署到应用目录 `plugins/`（参考 `plugins/helloplugin` 示例——注册 `/hello` 技能，返回问候与插件元数据）。

插件注册的技能会随 `/命令菜单` 与 `/help` 自动出现；卸载时统一注销，不留悬垂上下文。

**工具审计日志**：所有 Agent 工具调用（只读工具 / 命令执行 / 文件写入，含被用户拒绝的调用）以「时间 / 工具 / 参数 / 结果摘要」写入本地日志 `<AppData>/egent/logs/audit.log`，仅追加不覆盖，单行字段截断防膨胀（企划书安全底线）。

## 第三方组件与许可

本项目的**唯一第三方依赖是 Qt**，其余全部为自研代码。Egent 自身源码以 **LGPL-3.0-only** 许可发布（见根目录 [`LICENSE`](./LICENSE)）。

- **Qt 6.x** — 双许可（商业 / LGPLv3）。本项目按 **LGPLv3** 使用：
  - 未修改 Qt 源码
  - **动态链接**（`dist/bin/*.dll` 与可执行文件分离，用户可自行替换）
  - 许可全文见 `licenses/LGPL-3.0-only.txt`，分发时随包携带为 `LICENSE-Qt-LGPLv3.txt`
  - Qt 源码获取：https://www.qt.io/download-open-source
- **Markdown 渲染**：使用 Qt 内置的 `QTextDocument::setMarkdown`（底层 md4c），未额外引入库。
- **字体**：使用系统字体，不内嵌字体文件。

> 若将来需要静态链接 Qt（单文件分发），LGPLv3 会要求提供目标文件供用户重链接；该场景建议改走 Qt 商业许可。

## 已知限制

- 仅 Windows（MinGW 构建）
- 免费额度查询不可行：需阿里云 RAM AccessKey + BSS OpenAPI，DashScope API Key 无权访问。因此 P1.4 采用**本地记录方案 B**：设置面板手动录入月度额度上限 / 本月已用（跨月自动清零），左栏实时展示，并提供「打开厂商控制台」按钮跳转查看真实用量（与 `bl` 的 Console 登录额度能力存在差异）
- 插件启停 / 排序 / 元数据查看的设置界面尚未提供（当前为启动时自动加载，状态栏提示加载数量），插件管理 UI 属后续迭代
- Agent 工具调用记录本地审计日志（见「插件生态」一节），不做云端上传