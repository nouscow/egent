#include "runcommandtool.h"

#include "core/tools/pathguard.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

QString RunCommandTool::description() const
{
    return QStringLiteral(
        "在工作区目录下执行一条 shell 命令并返回输出。"
        "用于查看 git 状态、编译、运行测试等。危险命令会被直接拒绝。");
}

QJsonObject RunCommandTool::parametersSchema() const
{
    const QJsonObject cmd{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"), QStringLiteral("要执行的命令，单行")},
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("command"), cmd}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("command")}},
    };
}

bool RunCommandTool::isBlocked(const QString &command, QString *reason)
{
    // 硬性黑名单：破坏性、不可逆、或会波及工作区之外的操作。
    // 这些不看用户是否点了「允许」—— 一旦执行就没有回头路。
    struct Rule
    {
        const char *pattern;
        const char *why;
    };
    static const Rule rules[] = {
        {"format ", "格式化磁盘"},
        {"diskpart", "分区操作"},
        {"shutdown", "关机/重启"},
        {"rd /s", "递归删除目录"},
        {"rmdir /s", "递归删除目录"},
        {"del /f /s", "强制递归删除"},
        {"del /s", "递归删除"},
        {"rm -rf", "递归强制删除"},
        {"rm -fr", "递归强制删除"},
        {"mkfs", "格式化文件系统"},
        {"reg delete", "删除注册表项"},
        {"reg add", "修改注册表"},
        {"net user", "改动系统账户"},
        {"cipher /w", "擦除磁盘空闲空间"},
    };

    const QString lower = command.toLower();
    for (const Rule &r : rules) {
        if (lower.contains(QString::fromLatin1(r.pattern))) {
            if (reason)
                *reason = QString::fromUtf8(r.why);
            return true;
        }
    }
    return false;
}

ToolResult RunCommandTool::execute(const QJsonObject &args)
{
    QString command = args.value(QStringLiteral("command")).toString().trimmed();
    if (command.isEmpty())
        return ToolResult::failure(QStringLiteral("缺少 command 参数"));

    QString blocked;
    if (isBlocked(command, &blocked)) {
        return ToolResult::failure(
            QStringLiteral("命令被拒绝执行：涉及%1，属于硬性禁止的操作。").arg(blocked));
    }

    const QString workDir = PathGuard::instance().workspace();
    if (workDir.isEmpty())
        return ToolResult::failure(QStringLiteral("未设置工作区，无法确定命令的执行目录"));

    QProcess proc;
    proc.setWorkingDirectory(workDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    // Windows 下统一走 cmd，保证 &&、重定向等写法可用
    proc.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
    if (!proc.waitForStarted(5000))
        return ToolResult::failure(QStringLiteral("命令启动失败: %1").arg(proc.errorString()));

    if (!proc.waitForFinished(kTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        return ToolResult::failure(
            QStringLiteral("命令超时（超过 %1 秒），已强制结束。").arg(kTimeoutMs / 1000));
    }

    QByteArray out = proc.readAll();
    const bool truncated = out.size() > kMaxOutputBytes;
    if (truncated)
        out = out.left(kMaxOutputBytes);

    QString text = QString::fromUtf8(out);
    if (text.isEmpty())
        text = QStringLiteral("（无输出）");

    const int code = proc.exitCode();
    QString head = QStringLiteral("[退出码 %1] ").arg(code);
    if (truncated)
        head += QStringLiteral("[输出超过 %1KB，已截断] ").arg(kMaxOutputBytes / 1024);

    const bool ok = (code == 0);
    return ok ? ToolResult::success(head + QLatin1Char('\n') + text)
              : ToolResult::failure(head + QLatin1Char('\n') + text);
}
