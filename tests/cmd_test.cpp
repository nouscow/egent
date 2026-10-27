// 命令执行工具自测：黑名单必须拦住破坏性命令，正常命令要在工作区里执行。

#include "core/tools/pathguard.h"
#include "core/tools/runcommandtool.h"
#include "core/tools/toolregistry.h"
#include "core/tools/writefiletool.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char *what, const QString &detail = QString())
{
    if (cond) {
        ++g_pass;
        qInfo().noquote() << "  PASS " << what;
    } else {
        ++g_fail;
        qWarning().noquote() << "  FAIL " << what
                             << (detail.isEmpty() ? QString() : ("  -> " + detail));
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ---------------- 1. 黑名单 ----------------
    qInfo().noquote() << "\n[1] 硬性黑名单";
    struct Case { const char *cmd; bool blocked; };
    const Case cases[] = {
        {"format C:", true},
        {"diskpart", true},
        {"shutdown /s", true},
        {"rm -rf /", true},
        {"rmdir /s /q D://data", true},
        {"del /f /s *.txt", true},
        {"reg delete HKCU\\Software\\Foo", true},
        {"git status", false},
        {"dir", false},
        {"echo hello", false},
        {"npm run build", false},
    };
    for (const Case &c : cases) {
        QString reason;
        const bool blocked = RunCommandTool::isBlocked(QString::fromLatin1(c.cmd), &reason);
        check(blocked == c.blocked,
              c.blocked ? "拦截危险命令" : "放行普通命令",
              QStringLiteral("%1 (blocked=%2)").arg(QString::fromLatin1(c.cmd)).arg(blocked));
    }

    // ---------------- 2. 未设工作区必须拒绝 ----------------
    qInfo().noquote() << "\n[2] 工作区约束";
    PathGuard::instance().setWorkspace(QString());
    RunCommandTool tool;
    {
        const auto r = tool.execute(QJsonObject{{QStringLiteral("command"), QStringLiteral("echo hi")}});
        check(!r.ok, "未设工作区时拒绝执行", r.content);
    }

    QTemporaryDir tmp;
    PathGuard::instance().setWorkspace(tmp.path());
    check(PathGuard::instance().enabled(), "设置工作区后启用");

    // ---------------- 3. 正常命令 ----------------
    qInfo().noquote() << "\n[3] 正常执行";
    {
        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"), QStringLiteral("echo egent-test-ok")}});
        check(r.ok, "echo 执行成功", r.content);
        check(r.content.contains(QStringLiteral("egent-test-ok")), "输出内容正确");
        check(r.content.contains(QStringLiteral("退出码 0")), "退出码为 0");
    }

    // ---------------- 4. 工作目录就是工作区 ----------------
    qInfo().noquote() << "\n[4] 执行目录";
    {
        QFile marker(tmp.path() + QStringLiteral("/marker.txt"));
        marker.open(QIODevice::WriteOnly);
        marker.write("x");
        marker.close();

        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"), QStringLiteral("dir /b")}});
        check(r.ok, "dir 执行成功", r.content);
        check(r.content.contains(QStringLiteral("marker.txt")),
              "命令确实在工作区目录下执行", r.content.left(100));
    }

    // ---------------- 5. 失败命令要带退出码 ----------------
    qInfo().noquote() << "\n[5] 非零退出码";
    {
        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"), QStringLiteral("exit 3")}});
        check(!r.ok, "非零退出码算失败", r.content);
        check(r.content.contains(QStringLiteral("退出码 3")), "退出码如实返回");
    }

    // ---------------- 6. 危险命令不能靠 execute 绕过 ----------------
    qInfo().noquote() << "\n[6] 绕过路径";
    {
        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"), QStringLiteral("format C:")}});
        check(!r.ok, "直接调 execute 也被黑名单拦住", r.content);
    }

    // ---------------- 7. 注册表按需加载 ----------------
    qInfo().noquote() << "\n[7] 工具注册";
    ToolRegistry::instance().registerDefaults(false);
    check(ToolRegistry::instance().find(QStringLiteral("run_command")) == nullptr,
          "默认不注册命令工具");
    ToolRegistry::instance().registerDefaults(true);
    check(ToolRegistry::instance().find(QStringLiteral("run_command")) != nullptr,
          "显式开启后注册成功");
    check(ToolRegistry::instance().names().size() == 4, "共 4 个工具",
          ToolRegistry::instance().names().join(QStringLiteral(",")));

    // ---------------- 8. 执行期间界面不能冻结 ----------------
    qInfo().noquote() << "\n[8] 执行期间事件循环仍在运行";
    {
        int ticks = 0;
        QTimer ticker;
        QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
        ticker.start(50);

        QElapsedTimer clock;
        clock.start();
        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"),
                         QStringLiteral("ping -n 3 127.0.0.1 > nul")}});
        const qint64 ms = clock.elapsed();
        ticker.stop();

        check(ms > 1000, "命令确实执行了一段时间",
              QStringLiteral("%1ms").arg(ms));
        // 关键断言：如果执行期间阻塞了 UI 线程，定时器一次都不会触发
        check(ticks >= 10, "期间定时器持续触发（事件循环未被阻塞）",
              QStringLiteral("ticks=%1，理论应有 %2 次左右").arg(ticks).arg(ms / 50));
        Q_UNUSED(r)
    }

    // ---------------- 9. 中止正在执行的命令 ----------------
    qInfo().noquote() << "\n[9] 中止";
    {
        QTimer::singleShot(400, [&tool]() { tool.abort(); });

        QElapsedTimer clock;
        clock.start();
        const auto r = tool.execute(
            QJsonObject{{QStringLiteral("command"),
                         QStringLiteral("ping -n 10 127.0.0.1 > nul")}});
        const qint64 ms = clock.elapsed();

        check(ms < 5000, "命令被提前中止（没跑满 10 秒）",
              QStringLiteral("%1ms").arg(ms));
        check(!r.ok, "中止算作失败", r.content);
        check(r.content.contains(QStringLiteral("中止")), "返回明确的『已中止』说明", r.content);
    }

    // ---------------- 10. 文件写入（L4）----------------
    qInfo().noquote() << "\n[10] 文件写入";
    {
        WriteFileTool wt;

        check(!wt.execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("a.txt")}}).ok,
              "缺少 content 被拒绝");

        check(!wt.execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("../evil.txt")},
                                      {QStringLiteral("content"), QStringLiteral("x")}}).ok,
              "越界写入被拒绝");

        {
            const auto r = wt.execute(
                QJsonObject{{QStringLiteral("path"), QStringLiteral("sub/new.txt")},
                            {QStringLiteral("content"), QStringLiteral("hello")}});
            check(r.ok, "创建文件（含多级目录）成功", r.content);
            check(QFile::exists(tmp.path() + QStringLiteral("/sub/new.txt")),
                  "文件确实落盘");
        }

        {
            const auto r = wt.execute(
                QJsonObject{{QStringLiteral("path"), QStringLiteral("sub/new.txt")},
                            {QStringLiteral("content"), QStringLiteral("updated")}});
            check(r.ok, "覆盖已有文件成功", r.content);
            check(r.content.contains(QStringLiteral("已覆盖")), "明确告知是覆盖操作");

            QFile f(tmp.path() + QStringLiteral("/sub/new.txt"));
            f.open(QIODevice::ReadOnly);
            check(QString::fromUtf8(f.readAll()) == QStringLiteral("updated"),
                  "文件内容被正确替换");
        }

        {
            const QByteArray big(WriteFileTool::kMaxWriteBytes + 16, 'x');
            const auto r = wt.execute(
                QJsonObject{{QStringLiteral("path"), QStringLiteral("big.txt")},
                            {QStringLiteral("content"), QString::fromLatin1(big)}});
            check(!r.ok, "超过 1MB 的内容被拒绝", r.content);
        }

        // 注册表：三个开关独立
        ToolRegistry::instance().registerDefaults(false, false);
        check(ToolRegistry::instance().find(QStringLiteral("write_file")) == nullptr,
              "默认不注册写文件工具");
        ToolRegistry::instance().registerDefaults(false, true);
        check(ToolRegistry::instance().find(QStringLiteral("write_file")) != nullptr,
              "单独开启写文件后注册成功");
    }

    qInfo().noquote() << QStringLiteral("\n===== 通过 %1 / 失败 %2 =====").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
