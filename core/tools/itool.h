#pragma once

#include <QJsonObject>
#include <QString>

/**
 * 工具执行结果。
 * ok=false 时不代表程序出错，而是「这个动作没做成」，内容会原样回灌给模型，
 * 让模型自己决定换个路径还是换个办法。
 */
struct ToolResult
{
    bool ok = false;
    QString content;

    static ToolResult success(const QString &text) { return {true, text}; }
    static ToolResult failure(const QString &text) { return {false, text}; }
};

/**
 * Agent 工具接口。
 *
 * 约束：
 *  - 实现里**不许**做任何权限判断，路径一律交给 PathGuard
 *  - 输出要控制长度，模型上下文很贵
 *  - 只读工具不得修改文件系统
 */
class ITool
{
public:
    virtual ~ITool() = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    /** OpenAI function calling 的 parameters，JSON Schema 形式 */
    virtual QJsonObject parametersSchema() const = 0;

    virtual ToolResult execute(const QJsonObject &args) = 0;
};
