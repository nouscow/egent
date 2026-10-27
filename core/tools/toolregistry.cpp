#include "toolregistry.h"

#include "core/tools/readonlytools.h"
#include "core/tools/runcommandtool.h"

#include <QDebug>

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::add(std::unique_ptr<ITool> tool)
{
    if (!tool)
        return;
    const QString key = tool->name();
    if (key.isEmpty()) {
        qWarning().noquote() << "[tools] 忽略一个没有名字的工具";
        return;
    }
    m_tools[key] = std::move(tool);
}

void ToolRegistry::registerDefaults(bool includeCommandTool)
{
    // 幂等到「单个工具」而不是整体：先注册了只读工具，
    // 之后再打开命令执行时，不能因为容器非空就整批跳过。
    const auto addIfMissing = [this](std::unique_ptr<ITool> tool) {
        if (tool && m_tools.find(tool->name()) == m_tools.end())
            add(std::move(tool));
    };

    addIfMissing(std::make_unique<ListDirTool>());
    addIfMissing(std::make_unique<ReadFileTool>());
    addIfMissing(std::make_unique<SearchTextTool>());

    if (includeCommandTool)
        addIfMissing(std::make_unique<RunCommandTool>());

    qInfo().noquote() << "[tools] 当前可用:" << names().join(QStringLiteral(", "));
}

ITool *ToolRegistry::find(const QString &name) const
{
    auto it = m_tools.find(name);
    return it == m_tools.end() ? nullptr : it->second.get();
}

QStringList ToolRegistry::names() const
{
    QStringList out;
    for (const auto &kv : m_tools)
        out << kv.first;
    return out;
}

QJsonArray ToolRegistry::toolsForModel() const
{
    QJsonArray arr;
    for (const auto &kv : m_tools) {
        const ITool *tool = kv.second.get();
        const QJsonObject fn{
            {QStringLiteral("name"), tool->name()},
            {QStringLiteral("description"), tool->description()},
            {QStringLiteral("parameters"), tool->parametersSchema()},
        };
        arr.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                               {QStringLiteral("function"), fn}});
    }
    return arr;
}
