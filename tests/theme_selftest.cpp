// 主题模块自测：不依赖 GUI，纯 QtCore/QtGui。
// 跑完打印 PASS/FAIL 计数，返回码非 0 表示有失败项。

#include "theme/qsstemplate.h"
#include "theme/themedata.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStringList>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const QString &what, const QString &detail = QString())
{
    if (cond) {
        ++g_pass;
        qInfo().noquote() << "  PASS " << what;
    } else {
        ++g_fail;
        qWarning().noquote() << "  FAIL " << what << (detail.isEmpty() ? QString() : ("-> " + detail));
    }
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

static const char *kRequiredTokens[] = {
    "bg.window",
    "bg.surface",
    "bg.elevated",
    "bg.input",
    "bg.hover",
    "text.primary",
    "text.secondary",
    "text.placeholder",
    "text.onAccent",
    "border.default",
    "border.focus",
    "accent",
    "accent.hover",
    "danger",
    "selection.bg",
    "scrollbar.handle",
    "chat.userBubble.bg",
    "chat.userBubble.text",
    "chat.assistantBubble.bg",
    "chat.assistantBubble.text",
    "toolbar.bg",
    "input.bg",
    "input.border",
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString root = QStringLiteral(PROJECT_DIR);
    const QString themeDir = root + QStringLiteral("/resources/themes");
    const QString qssPath = root + QStringLiteral("/resources/qss/app.qss");

    // ---------- 1. 内置主题可加载且令牌自洽 ----------
    qInfo().noquote() << "\n[1] 内置主题加载";
    for (const QString &id : {QStringLiteral("dark"), QStringLiteral("light")}) {
        const QString path = themeDir + "/" + id + ".json";
        ThemeData t;
        QString err;
        const bool ok = t.loadFromFile(path, &err);
        check(ok, QStringLiteral("%1.json 可加载").arg(id), err);

        if (!ok)
            continue;

        check(t.missingTokens().isEmpty(),
              QStringLiteral("%1 无缺失令牌").arg(id),
              t.missingTokens().join(QStringLiteral(", ")));
        check(t.cyclicTokens().isEmpty(),
              QStringLiteral("%1 无循环引用").arg(id),
              t.cyclicTokens().join(QStringLiteral(", ")));

        int missing = 0;
        QStringList bad;
        for (const char *name : kRequiredTokens) {
            const QString key = QString::fromLatin1(name);
            const QColor c = t.color(key);
            if (!c.isValid()) {
                ++missing;
                bad << key;
            }
        }
        check(missing == 0, QStringLiteral("%1 必需令牌全部有合法色值").arg(id),
              bad.join(QStringLiteral(", ")));
    }

    // ---------- 2. QSS 模板渲染无残留 ----------
    qInfo().noquote() << "\n[2] QSS 模板渲染";
    const QByteArray qssBytes = readFile(qssPath);
    check(!qssBytes.isEmpty(), QStringLiteral("app.qss 可读取"), qssPath);

    const QString tpl = QString::fromUtf8(qssBytes);
    for (const QString &id : {QStringLiteral("dark"), QStringLiteral("light")}) {
        ThemeData t;
        QString err;
        if (!t.loadFromFile(themeDir + "/" + id + ".json", &err))
            continue;

        const auto r = QssTemplate::render(
            tpl, [&t](const QString &n) { return t.token(n); });

        check(!r.hasUnknown(),
              QStringLiteral("%1 渲染无未知令牌").arg(id),
              r.unknownTokens.join(QStringLiteral(", ")));
        check(!r.qss.contains(QStringLiteral("{{")),
              QStringLiteral("%1 渲染结果无 {{ 残留").arg(id));
        const int residuePos = r.qss.indexOf(QStringLiteral("{{"));
        if (residuePos >= 0) {
            const QString ctx = r.qss.mid(qMax(0, residuePos - 70), 150).replace(QLatin1Char('\n'), QLatin1Char(' '));
            qWarning().noquote() << "       残留上下文:" << ctx;
        }
        check(r.qss.size() > tpl.size() / 2,
              QStringLiteral("%1 渲染结果长度合理").arg(id),
              QStringLiteral("%1 bytes").arg(r.qss.size()));
    }

    // ---------- 3. 循环引用必须被拦住 ----------
    qInfo().noquote() << "\n[3] 循环引用检测";
    {
        const QByteArray bad = R"({
            "version": 1, "id": "bad-cycle", "name": "循环",
            "primitive": {},
            "semantic": {
                "bg.window": "bg.surface",
                "bg.surface": "bg.window",
                "text.primary": "#ffffff"
            },
            "component": {}
        })";
        ThemeData t;
        QString err;
        t.loadFromJson(bad, &err);
        check(!t.cyclicTokens().isEmpty(),
              QStringLiteral("循环引用被识别"), t.cyclicTokens().join(QStringLiteral(", ")));
    }

    // ---------- 4. 缺失令牌必须被记录 ----------
    qInfo().noquote() << "\n[4] 缺失令牌检测";
    {
        const QByteArray bad = R"({
            "version": 1, "id": "bad-missing", "name": "缺令牌",
            "primitive": { "a": "#112233" },
            "semantic": {
                "bg.window": "a",
                "bg.surface": "not-defined-anywhere",
                "text.primary": "a"
            },
            "component": {}
        })";
        ThemeData t;
        QString err;
        t.loadFromJson(bad, &err);
        check(t.missingTokens().contains(QStringLiteral("not-defined-anywhere")),
              QStringLiteral("缺失令牌被记录"),
              t.missingTokens().join(QStringLiteral(", ")));
    }

    // ---------- 5. 占位符渲染时缺失要保留并上报 ----------
    qInfo().noquote() << "\n[5] 渲染期缺失保护";
    {
        const auto r = QssTemplate::render(
            QStringLiteral("QWidget { color: {{known}}; background: {{nope}}; }"),
            [](const QString &n) { return n == QLatin1String("known") ? QStringLiteral("#123456")
                                                                     : QString(); });
        check(r.unknownTokens.contains(QStringLiteral("nope")),
              QStringLiteral("未知令牌被上报"), r.unknownTokens.join(QStringLiteral(", ")));
        check(r.qss.contains(QStringLiteral("{{nope}}")),
              QStringLiteral("未知令牌原样保留便于排查"));
        check(r.qss.contains(QStringLiteral("#123456")),
              QStringLiteral("已知令牌已替换"));
    }

    // ---------- 汇总 ----------
    qInfo().noquote() << QStringLiteral("\n===== 通过 %1 / 失败 %2 =====").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
