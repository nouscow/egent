// Agent 只读工具自测：重点验证 PathGuard 的越界拦截。
// 退出码 0 = 全部通过。

#include "core/tools/pathguard.h"
#include "core/tools/readonlytools.h"
#include "core/tools/toolregistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

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

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        qWarning() << "临时目录创建失败";
        return 1;
    }

    const QString ws = tmp.path() + QStringLiteral("/workspace");
    QDir().mkpath(ws + QStringLiteral("/src"));
    QDir().mkpath(ws + QStringLiteral("/.git"));
    writeFile(ws + QStringLiteral("/README.md"),
              QByteArray("# Demo\nhello world\nsecond line\n"));
    writeFile(ws + QStringLiteral("/src/main.cpp"),
              QByteArray("int main() { return 0; }\n// hello from source\n"));
    writeFile(ws + QStringLiteral("/.git/config"), QByteArray("[core]\n"));
    // 工作区外面放一个「机密」文件，用来测试逃逸
    writeFile(tmp.path() + QStringLiteral("/secret.txt"), QByteArray("TOP SECRET"));

    // ---------------- 1. PathGuard ----------------
    qInfo().noquote() << "\n[1] PathGuard 边界";

    PathGuard &guard = PathGuard::instance();
    check(!guard.enabled(), "初始状态未启用");
    {
        QString err;
        check(guard.resolve(QStringLiteral("README.md"), &err).isEmpty(),
              "未设置工作区时拒绝一切访问", err);
    }

    guard.setWorkspace(ws);
    check(guard.enabled(), "设置工作区后启用");

    {
        QString err;
        check(!guard.resolve(QStringLiteral("README.md"), &err).isEmpty(),
              "工作区内相对路径放行");
        check(!guard.resolve(QStringLiteral("src/main.cpp"), &err).isEmpty(),
              "子目录路径放行");
        check(guard.resolve(ws + QStringLiteral("/README.md"), &err).isEmpty() == false,
              "工作区内绝对路径放行");
    }

    {
        QString err;
        const QString escaped = guard.resolve(QStringLiteral("../secret.txt"), &err);
        check(escaped.isEmpty(), "`../` 逃逸被拦截", err);
    }
    {
        QString err;
        const QString escaped = guard.resolve(QStringLiteral("src/../../../secret.txt"), &err);
        check(escaped.isEmpty(), "多层 `../` 逃逸被拦截", err);
    }
    {
        QString err;
        const QString outside = guard.resolve(tmp.path() + QStringLiteral("/secret.txt"), &err);
        check(outside.isEmpty(), "工作区外的绝对路径被拦截", err);
    }
    {
        QString err;
        // 不存在的文件：仍然要判断它落在哪
        const QString ghost = guard.resolve(QStringLiteral("../ghost.txt"), &err);
        check(ghost.isEmpty(), "不存在的越界路径也被拦截", err);
    }
    {
        QString err;
        const QString ok = guard.resolve(QStringLiteral("src/../README.md"), &err);
        check(!ok.isEmpty(), "合法范围内的 `..` 仍可解析（归一化后不越界）");
    }

    // ---------------- 2. list_dir ----------------
    qInfo().noquote() << "\n[2] list_dir";
    ListDirTool listTool;
    {
        const auto r = listTool.execute(QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}});
        check(r.ok, "列根目录成功", r.content);
        check(r.content.contains(QStringLiteral("README.md")), "包含 README.md");
        check(r.content.contains(QStringLiteral("[DIR ] src")), "包含 src 目录");
        check(r.content.contains(QStringLiteral(".git")), "包含隐藏目录 .git");
    }
    {
        const auto r = listTool.execute(QJsonObject{{QStringLiteral("path"), QStringLiteral("../")}});
        check(!r.ok, "越界列目录被拒绝", r.content);
    }

    // ---------------- 3. read_file ----------------
    qInfo().noquote() << "\n[3] read_file";
    ReadFileTool readTool;
    {
        const auto r = readTool.execute(
            QJsonObject{{QStringLiteral("path"), QStringLiteral("README.md")}});
        check(r.ok, "读取文本文件成功", r.content);
        check(r.content.contains(QStringLiteral("hello world")), "内容正确");
    }
    {
        const auto r = readTool.execute(QJsonObject{{QStringLiteral("path"), QString()}});
        check(!r.ok, "缺少 path 参数被拒绝");
    }
    {
        const auto r = readTool.execute(
            QJsonObject{{QStringLiteral("path"), QStringLiteral("../secret.txt")}});
        check(!r.ok, "越界读文件被拒绝", r.content);
        check(!r.content.contains(QStringLiteral("TOP SECRET")), "没有泄露工作区外内容");
    }
    writeFile(ws + QStringLiteral("/blob.bin"), QByteArray("abc\0def", 7));
    {
        const auto r = readTool.execute(
            QJsonObject{{QStringLiteral("path"), QStringLiteral("blob.bin")}});
        check(!r.ok, "二进制文件被拒绝", r.content);
    }
    {
        // 大文件应被截断而不是整个读进来
        QByteArray big(ToolLimits::kMaxReadBytes + 4096, 'x');
        writeFile(ws + QStringLiteral("/big.txt"), big);
        const auto r = readTool.execute(
            QJsonObject{{QStringLiteral("path"), QStringLiteral("big.txt")}});
        check(r.ok, "超大文件仍可读");
        check(r.content.size() < big.size(), "超大文件被截断");
        check(r.content.contains(QStringLiteral("仅读取前")), "给出了截断说明");
    }

    // ---------------- 4. search_text ----------------
    qInfo().noquote() << "\n[4] search_text";
    SearchTextTool searchTool;
    {
        const auto r = searchTool.execute(
            QJsonObject{{QStringLiteral("query"), QStringLiteral("hello")}});
        check(r.ok, "搜索成功", r.content);
        check(r.content.contains(QStringLiteral("README.md:2")), "命中 README 第 2 行");
        check(r.content.contains(QStringLiteral("main.cpp")), "命中 main.cpp");
    }
    {
        const auto r = searchTool.execute(
            QJsonObject{{QStringLiteral("query"), QStringLiteral("core")}});
        check(!r.content.contains(QStringLiteral(".git")), "跳过 .git 目录");
    }
    {
        const auto r = searchTool.execute(
            QJsonObject{{QStringLiteral("query"), QStringLiteral("不存在的字符串zzz")}});
        check(r.ok, "无结果时也算成功");
        check(r.content.contains(QStringLiteral("没有找到")), "给出无结果说明");
    }

    // ---------------- 5. 注册表 ----------------
    qInfo().noquote() << "\n[5] ToolRegistry";
    ToolRegistry &reg = ToolRegistry::instance();
    reg.registerDefaults();
    check(reg.names().size() == 3, "注册了 3 个只读工具", reg.names().join(QStringLiteral(",")));
    check(reg.find(QStringLiteral("read_file")) != nullptr, "能按名字找到 read_file");
    check(reg.find(QStringLiteral("run_command")) == nullptr, "M5 没有执行类工具");

    const QJsonArray tools = reg.toolsForModel();
    check(tools.size() == 3, "toolsForModel 返回 3 项");
    const QJsonObject first = tools.first().toObject();
    check(first.value(QStringLiteral("type")).toString() == QStringLiteral("function"),
          "格式为 OpenAI function 类型");
    const QJsonObject fn = first.value(QStringLiteral("function")).toObject();
    check(!fn.value(QStringLiteral("name")).toString().isEmpty(), "function 带 name");
    check(fn.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("type"))
              .toString() == QStringLiteral("object"),
          "parameters 是 JSON Schema object");

    qInfo().noquote() << QStringLiteral("\n===== 通过 %1 / 失败 %2 =====").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
