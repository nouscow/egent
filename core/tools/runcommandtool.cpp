#include "runcommandtool.h"

#include "core/tools/pathguard.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

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
    // 破坏性、不可逆、或会波及工作区之外的操作。
    // 不看用户是否点了「允许」—— 这类操作没有回头路。
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

void RunCommandTool::abort()
{
    m_abort.storeRelaxed(1);
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
}

ToolResult RunCommandTool::execute(const QJsonObject &args)
{
    const QString command = args.value(QStringLiteral("command")).toString().trimmed();
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

    m_abort.storeRelaxed(0);

    QProcess proc;
    proc.setWorkingDirectory(workDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    m_proc = &proc;

    QEventLoop loop;
    bool timedOut = false;
    bool failedToStart = false;

    QObject::connect(&proc, &QProcess::errorOccurred, &loop,
                     [&](QProcess::ProcessError err) {
                         if (err == QProcess::FailedToStart) {
                             failedToStart = true;
                             loop.quit();
                         }
                     });
    QObject::connect(&proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, [&](int, QProcess::ExitStatus) { loop.quit(); });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    // Windows 下统一走 cmd，保证 &&、重定向等写法可用
    proc.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
    timeout.start(kTimeoutMs);

    // 关键：用局部事件循环等信号，而不是 waitForFinished()。
    // 嵌套循环仍在处理界面事件，因此窗口不会卡死，停止按钮也能生效。
    loop.exec();

    timeout.stop();
    m_proc = nullptr;

    const bool aborted = m_abort.loadRelaxed() != 0;

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        // 这里必须短等，确保进程真的退出、输出能读到
        proc.waitForFinished(1000);
    }

    if (aborted)
        return ToolResult::failure(QStringLiteral("命令被用户中止。"));
    if (failedToStart)
        return ToolResult::failure(QStringLiteral("命令启动失败: %1").arg(proc.errorString()));
    if (timedOut)
        return ToolResult::failure(
            QStringLiteral("命令超时（超过 %1 秒），已强制结束。").arg(kTimeoutMs / 1000));

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
