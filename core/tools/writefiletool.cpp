#include "writefiletool.h"

#include "core/tools/pathguard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

QString WriteFileTool::description() const
{
    return QStringLiteral(
        "在工作区内创建或覆盖一个文本文件。写入前会把最终内容展示给用户确认。"
        "不能删除文件，也不允许写到工作区之外。");
}

QJsonObject WriteFileTool::parametersSchema() const
{
    const QJsonObject path{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"), QStringLiteral("相对工作区的文件路径")},
    };
    const QJsonObject content{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"), QStringLiteral("写入的完整文件内容")},
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("content"), content}}},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("path"), QStringLiteral("content")}},
    };
}

ToolResult WriteFileTool::execute(const QJsonObject &args)
{
    const QString rel = args.value(QStringLiteral("path")).toString().trimmed();
    if (rel.isEmpty())
        return ToolResult::failure(QStringLiteral("缺少 path 参数"));

    if (!args.contains(QStringLiteral("content")))
        return ToolResult::failure(QStringLiteral("缺少 content 参数"));

    const QString content = args.value(QStringLiteral("content")).toString();

    QString err;
    const QString file = PathGuard::instance().resolve(rel, &err);
    if (file.isEmpty())
        return ToolResult::failure(err);

    const QFileInfo info(file);
    if (info.isDir())
        return ToolResult::failure(QStringLiteral("目标是目录，不是文件: %1").arg(rel));

    if (content.toUtf8().size() > kMaxWriteBytes) {
        return ToolResult::failure(
            QStringLiteral("内容超过 %1KB 上限，拒绝写入。").arg(kMaxWriteBytes / 1024));
    }

    // 允许在工作区内创建多级目录
    const QString parent = info.absolutePath();
    if (!QDir().exists(parent) && !QDir().mkpath(parent))
        return ToolResult::failure(QStringLiteral("无法创建目录: %1").arg(parent));

    const bool existed = info.exists();
    const qint64 oldSize = existed ? info.size() : 0;

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return ToolResult::failure(QStringLiteral("写入失败: %1").arg(f.errorString()));

    const qint64 written = f.write(content.toUtf8());
    f.close();

    if (written < 0)
        return ToolResult::failure(QStringLiteral("写入过程中出错"));

    return ToolResult::success(
        existed
            ? QStringLiteral("已覆盖 %1（原 %2 字节 → 现 %3 字节）")
                  .arg(rel).arg(oldSize).arg(written)
            : QStringLiteral("已创建 %1（%2 字节）").arg(rel).arg(written));
}
