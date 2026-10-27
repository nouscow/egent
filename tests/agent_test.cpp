// Agent 端到端验收：真实模型 → 要求它调工具 → 本地执行 → 回灌 → 出最终回答。
// 需要 DASHSCOPE_API_KEY。退出码 0 = 通过。

#include "core/agentloop.h"
#include "core/providers/openaicompatprovider.h"
#include "core/providers/providerregistry.h"
#include "core/tools/pathguard.h"
#include "core/tools/toolregistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
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

static void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (qgetenv("DASHSCOPE_API_KEY").isEmpty()) {
        qWarning() << "FAIL 环境变量 DASHSCOPE_API_KEY 为空";
        return 1;
    }

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    writeFile(tmp.path() + QStringLiteral("/hello.txt"),
              QStringLiteral("这是一个测试文件").toUtf8());
    writeFile(tmp.path() + QStringLiteral("/notes.md"), QByteArray("# 笔记\n内容"));

    PathGuard::instance().setWorkspace(tmp.path());
    ToolRegistry::instance().registerDefaults();

    OpenAiCompatProvider provider(ProviderRegistry::bailianConfig());
    provider.setOption(QStringLiteral("tools_enabled"), true);
    provider.setOption(QStringLiteral("enable_thinking"), false);

    AgentLoop loop(&provider);
    loop.setModel(QStringLiteral("qwen3.8-flash"));

    QStringList calledTools;
    QString answer;
    QString error;

    QObject::connect(&loop, &AgentLoop::toolCallStarted,
                     [&](const QString &name, const QString &args) {
                         calledTools << name;
                         qInfo().noquote() << QStringLiteral("  -> 调用 %1 %2").arg(name, args);
                     });
    QObject::connect(&loop, &AgentLoop::toolCallFinished,
                     [&](const QString &name, bool ok, const QString &result) {
                         qInfo().noquote() << QStringLiteral("  <- %1 %2：%3")
                                                  .arg(name, ok ? "成功" : "失败",
                                                       result.left(80).replace('\n', ' '));
                     });
    QObject::connect(&loop, &AgentLoop::contentDelta,
                     [&](const QString &d) { answer += d; });
    QObject::connect(&loop, &AgentLoop::errorOccurred,
                     [&](const QString &m) { error = m; });
    QObject::connect(&loop, &AgentLoop::finished, &app, &QCoreApplication::quit);

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &app, &QCoreApplication::quit);
    guard.start(120000);

    qInfo().noquote() << "提问：工作区根目录下有哪些文件？请先调用 list_dir 查看，再告诉我结果。";
    loop.start({{Role::User,
                 QStringLiteral("工作区根目录下有哪些文件？请先调用 list_dir 查看，"
                                "再告诉我结果。")}});

    app.exec();

    qInfo().noquote() << "\n---------- 结果 ----------";
    qInfo().noquote() << "调用工具 :" << (calledTools.isEmpty() ? QStringLiteral("(无)")
                                                               : calledTools.join(QStringLiteral(", ")));
    qInfo().noquote() << "最终回答 :" << answer.trimmed().left(200);
    if (!error.isEmpty())
        qInfo().noquote() << "错误     :" << error;

    qInfo().noquote() << "\n---------- 判定 ----------";
    check(error.isEmpty(), "无错误", error);
    check(!calledTools.isEmpty(), "模型确实发起了工具调用");
    check(calledTools.contains(QStringLiteral("list_dir")), "调用的是 list_dir");
    check(!answer.trimmed().isEmpty(), "拿到了最终回答");
    check(answer.contains(QStringLiteral("hello")), "回答里提到了真实文件名 hello.txt",
          answer.left(120));

    qInfo().noquote() << QStringLiteral("\n===== 通过 %1 / 失败 %2 =====").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
