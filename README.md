# Egent

Egent is a local AI desktop client for Windows. Chat with any OpenAI-compatible API: Bailian, DeepSeek, OpenAI and more. Switch model on the fly. Chats persist in SQLite; themes come from JSON tokens. Optional Agent mode reads, searches, runs and writes files in a sandbox, gated by a permission prompt and command blacklist. Keys stay local.

本地 AI 客户端：多厂商对话（百炼 / DeepSeek / OpenAI）、可切换主题、会话持久化，并支持可选的本地工具调用（读文件 / 执行命令 / 写文件）。

## 构建

依赖 Qt 6.5+（需包含 Core / Widgets / Network / Sql / Svg / LinguistTools）与 CMake 3.19+。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt 路径>
cmake --build build
cmake --install build --prefix <绝对路径>/dist   # 注意 prefix 必须是绝对路径
```

打包后 `dist/bin/egent.exe` 可独立运行（已通过 `windeployqt` 携带 Qt 运行时）。

## 运行配置

API Key 读取顺序：**环境变量优先，其次设置面板中保存的值**。

| 厂商 | 环境变量 | 默认端点 |
| --- | --- | --- |
| 百炼 | `DASHSCOPE_API_KEY` | `dashscope.aliyuncs.com/compatible-mode/v1` |
| DeepSeek | `DEEPSEEK_API_KEY` | `api.deepseek.com/v1` |
| OpenAI | `OPENAI_API_KEY` | `api.openai.com/v1` |

## 测试

```bash
cmake --build build --target theme_selftest session_test tool_test cmd_test skill_test
./build/theme_selftest.exe   # 主题令牌与 QSS 渲染（无需网络）
./build/session_test.exe     # SQLite 会话存储
./build/tool_test.exe        # 只读工具与路径越界拦截
./build/cmd_test.exe         # 命令执行、文件写入、界面不冻结
./build/skill_test.exe       # 核心技能系统（注册表 / help / time / calc）
```

另外两个需要网络与 API Key：`provider_live_test`（流式接口）、`agent_test`（工具调用闭环）。

## 第三方组件与许可

本项目的**唯一第三方依赖是 Qt**，其余全部为自研代码。

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
- 生图尚未入会话（弹窗预览）；识图（视觉理解）依赖 qwen3-vl-plus，图片附件暂不参与模型上下文
