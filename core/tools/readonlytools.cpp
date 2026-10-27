#include "readonlytools.h"

#include "core/tools/pathguard.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>

namespace {

/** 搜索时跳过的目录：版本库、构建产物、依赖缓存，扫它们纯属浪费 */
bool shouldSkipDir(const QString &absolutePath)
{
    static const QStringList skipped = {
        QStringLiteral("/.git/"),       QStringLiteral("/.svn/"),
        QStringLiteral("/build/"),      QStringLiteral("/build-"),
        QStringLiteral("/node_modules/"), QStringLiteral("/.vs/"),
        QStringLiteral("/__pycache__/"),
    };
    const QString p = QDir::fromNativeSeparators(absolutePath) + QLatin1Char('/');
    for (const QString &s : skipped) {
        if (p.contains(s, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QJsonObject stringProp(const QString &desc)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), desc}};
}

} // namespace

// ============================ list_dir ============================

QString ListDirTool::description() const
{
    return QStringLiteral("列出工作区内某个目录的内容。返回每项的类型（目录/文件）与大小。");
}

QJsonObject ListDirTool::parametersSchema() const
{
    QJsonObject pathProp =
        stringProp(QStringLiteral("相对工作区的目录路径，省略或留空表示工作区根目录"));
    pathProp.insert(QStringLiteral("default"), QStringLiteral("."));

    const QJsonObject props{{QStringLiteral("path"), pathProp}};
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), props}};
}

ToolResult ListDirTool::execute(const QJsonObject &args)
{
    // 模型经常把可选参数传成空字符串而不是省略，这里兜一手：
    // 空路径按工作区根处理，比直接报「路径为空」更有用
    QString rel = args.value(QStringLiteral("path")).toString().trimmed();
    if (rel.isEmpty())
        rel = QStringLiteral(".");

    QString err;
    const QString dir = PathGuard::instance().resolve(rel, &err);
    if (dir.isEmpty())
        return ToolResult::failure(err);

    const QDir d(dir);
    if (!d.exists())
        return ToolResult::failure(QStringLiteral("目录不存在: %1").arg(rel));

    const QFileInfoList entries = d.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name);

    if (entries.isEmpty())
        return ToolResult::success(QStringLiteral("（空目录）"));

    QStringList lines;
    int shown = 0;
    for (const QFileInfo &fi : entries) {
        if (shown >= ToolLimits::kMaxDirEntries) {
            lines << QStringLiteral("… 还有 %1 项未显示").arg(entries.size() - shown);
            break;
        }
        if (fi.isDir())
            lines << QStringLiteral("[DIR ] %1/").arg(fi.fileName());
        else
            lines << QStringLiteral("[FILE] %1  (%2 字节)").arg(fi.fileName()).arg(fi.size());
        ++shown;
    }
    return ToolResult::success(lines.join(QLatin1Char('\n')));
}

// ============================ read_file ============================

QString ReadFileTool::description() const
{
    return QStringLiteral("读取工作区内一个文本文件的内容。二进制文件会被拒绝；"
                          "超大文件只返回前 256KB。");
}

QJsonObject ReadFileTool::parametersSchema() const
{
    QJsonObject props{{QStringLiteral("path"),
                       stringProp(QStringLiteral("相对工作区的文件路径"))}};
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), props},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}};
}

ToolResult ReadFileTool::execute(const QJsonObject &args)
{
    const QString rel = args.value(QStringLiteral("path")).toString();
    if (rel.trimmed().isEmpty())
        return ToolResult::failure(QStringLiteral("缺少 path 参数"));

    QString err;
    const QString file = PathGuard::instance().resolve(rel, &err);
    if (file.isEmpty())
        return ToolResult::failure(err);

    const QFileInfo fi(file);
    if (!fi.isFile())
        return ToolResult::failure(QStringLiteral("不是文件: %1").arg(rel));

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return ToolResult::failure(QStringLiteral("打不开: %1").arg(f.errorString()));

    const QByteArray data = f.read(ToolLimits::kMaxReadBytes);

    // 文本文件里出现 NUL 基本可以断定是二进制
    if (data.contains('\0'))
        return ToolResult::failure(QStringLiteral("这是二进制文件，不予读取: %1").arg(rel));

    QString text = QString::fromUtf8(data);
    if (fi.size() > data.size()) {
        text += QStringLiteral("\n\n…（文件共 %1 字节，仅读取前 %2 字节）")
                    .arg(fi.size())
                    .arg(data.size());
    }
    return ToolResult::success(text);
}

// ============================ search_text ============================

QString SearchTextTool::description() const
{
    return QStringLiteral("在工作区内递归搜索包含指定文本的行，返回「文件:行号: 内容」。"
                          "自动跳过 .git、build、node_modules 等目录。");
}

QJsonObject SearchTextTool::parametersSchema() const
{
    QJsonObject props{
        {QStringLiteral("query"), stringProp(QStringLiteral("要搜索的文本（区分大小写）"))},
        {QStringLiteral("path"), stringProp(QStringLiteral("搜索起点目录，省略则为工作区根"))},
    };
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), props},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}}};
}

ToolResult SearchTextTool::execute(const QJsonObject &args)
{
    const QString query = args.value(QStringLiteral("query")).toString();
    if (query.isEmpty())
        return ToolResult::failure(QStringLiteral("缺少 query 参数"));

    QString rel = args.value(QStringLiteral("path")).toString().trimmed();
    if (rel.isEmpty())
        rel = QStringLiteral(".");

    QString err;
    const QString root = PathGuard::instance().resolve(rel, &err);
    if (root.isEmpty())
        return ToolResult::failure(err);

    QStringList hits;
    int scanned = 0;

    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (shouldSkipDir(file))
            continue;

        const QFileInfo fi(file);
        if (fi.size() > ToolLimits::kMaxSearchFileBytes)
            continue;

        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        if (data.contains('\0'))
            continue;

        ++scanned;
        const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            if (!lines.at(i).contains(query))
                continue;
            const QString relPath = QDir(root).relativeFilePath(file);
            hits << QStringLiteral("%1:%2: %3")
                        .arg(relPath)
                        .arg(i + 1)
                        .arg(lines.at(i).trimmed().left(200));
            if (hits.size() >= ToolLimits::kMaxSearchResults)
                break;
        }
        if (hits.size() >= ToolLimits::kMaxSearchResults)
            break;
    }

    if (hits.isEmpty())
        return ToolResult::success(
            QStringLiteral("没有找到包含「%1」的内容（已扫描 %2 个文本文件）")
                .arg(query)
                .arg(scanned));

    QString out = hits.join(QLatin1Char('\n'));
    if (hits.size() >= ToolLimits::kMaxSearchResults)
        out += QStringLiteral("\n… 已达上限 %1 条，请缩小搜索范围").arg(ToolLimits::kMaxSearchResults);
    return ToolResult::success(out);
}
