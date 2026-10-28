#pragma once

#include <QString>

/**
 * 本地审计日志（单例）。
 *
 * 企划书安全底线：「所有工具调用写本地审计日志（时间 / 工具 / 参数 / 结果摘要）」。
 * 覆盖 Agent 只读工具、命令执行、文件写入以及权限被拒的调用。
 * 日志仅追加不覆盖、不删除，存 <AppData>/egent/logs/audit.log；
 * 单行参数 / 结果截断，防止超长参数撑爆磁盘。
 * 落盘失败不打断工具调用，仅 qWarning 提示一次。
 */
class AuditLog
{
public:
    enum class Outcome { Ok, Fail, Denied };

    static AuditLog &instance();

    void append(const QString &tool, const QString &args,
                const QString &result, Outcome outcome);

    /** 当前生效的日志文件完整路径（测试可用 setLogPathForTest 覆盖） */
    static QString logFilePath();

    /** 测试专用：重定向日志路径（传空串恢复默认） */
    static void setLogPathForTest(const QString &path);

private:
    AuditLog() = default;
};