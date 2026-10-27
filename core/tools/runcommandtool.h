#pragma once

#include "core/tools/itool.h"

/**
 * L3 命令执行工具。
 *
 * 这是整个项目里唯一会改变系统状态的工具，因此：
 *  - requiresConfirmation() 恒为 true（要不要弹窗由界面按策略决定）
 *  - 内部还有一层**硬性黑名单**，即便用户选了「全部允许」也拦得住
 *  - 工作目录固定为 PathGuard 的工作区，不能指定任意目录
 *  - 有超时（默认 30s）和输出上限（64KB），防止死循环或刷屏撑爆上下文
 */
class RunCommandTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("run_command"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
    bool requiresConfirmation() const override { return true; }

    /** 命令是否被硬性禁止；reason 返回命中的规则 */
    static bool isBlocked(const QString &command, QString *reason = nullptr);

    static constexpr int kTimeoutMs = 30000;
    static constexpr int kMaxOutputBytes = 64 * 1024;
};
