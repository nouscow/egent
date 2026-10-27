#include "toolregistry.h"

#include "core/tools/readonlytools.h"

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

void ToolRegistry::registerDefaults()
{
    if (!m_tools.empty())
        return; // 幂等

    add(std::make_unique<ListDirTool>());
    add(std::make_unique<ReadFileTool>());
    add(std::make_unique<SearchTextTool>());

    qInfo().noquote() << "[tools] 已注册:" << names().join(QStringLiteral(", "));
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
