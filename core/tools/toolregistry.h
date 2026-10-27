#pragma once

#include "core/tools/itool.h"

#include <QJsonArray>
#include <QStringList>
#include <map>
#include <memory>

/**
 * 工具注册表（单例）。
 *
 * 两件事：
 *  1. 给模型生成 OpenAI `tools` 描述（toolsForModel）
 *  2. 收到 tool_calls 后按名字派发（find）
 *
 * M5 只注册只读工具；M6 加执行类工具时只需在这里多注册一个，
 * AgentLoop 与界面都不用改。
 */
class ToolRegistry
{
public:
    static ToolRegistry &instance();

    /** 注册 L2 只读工具（幂等，重复调用不会重复注册） */
    void registerDefaults();

    void add(std::unique_ptr<ITool> tool);

    ITool *find(const QString &name) const;
    QStringList names() const;
    bool isEmpty() const { return m_tools.empty(); }

    /** OpenAI 兼容的 tools 数组 */
    QJsonArray toolsForModel() const;

private:
    ToolRegistry() = default;

    // 用 std::map 而不是 QMap：QMap::insert 只接受 const T&，无法移动 unique_ptr
    std::map<QString, std::unique_ptr<ITool>> m_tools;
};
