#pragma once

#include "core/tools/itool.h"

#include <QAtomicInt>

class QProcess;

/**
 * L3 命令执行工具。
 *
 * 三重保护：
 *  1. requiresConfirmation() 恒 true —— 要不要弹窗由界面按策略决定
 *  2. 硬性黑名单，即便用户选「全部允许」也拦得住
 *  3. 工作目录锁定 PathGuard 工作区，不能指定任意路径
 *
 * 关于阻塞：**绝不能用 waitForFinished()**。
 * 工具是在 UI 线程执行的，同步等待会让整个窗口卡死（最长 30 秒，
 * 连「停止」按钮都点不了）。这里改用局部 QEventLoop 等信号，
 * 嵌套事件循环仍在处理界面事件，窗口保持响应。
 */
class RunCommandTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("run_command"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
    bool requiresConfirmation() const override { return true; }
    void abort() override;

    /** 命令是否被硬性禁止；reason 返回命中的规则 */
    static bool isBlocked(const QString &command, QString *reason = nullptr);

    static constexpr int kTimeoutMs = 30000;
    static constexpr int kMaxOutputBytes = 64 * 1024;

private:
    QProcess *m_proc = nullptr; // 仅在 execute 期间有效
    QAtomicInt m_abort{0};
};
