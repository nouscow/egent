#include "pathguard.h"

#include <QDir>
#include <QFileInfo>

PathGuard &PathGuard::instance()
{
    static PathGuard guard;
    return guard;
}

void PathGuard::setWorkspace(const QString &root)
{
    if (root.trimmed().isEmpty()) {
        m_root.clear();
        return;
    }

    const QFileInfo fi(root);
    if (!fi.isDir()) {
        // 不存在或不是目录：拒绝启用，而不是记下一个无效路径
        m_root.clear();
        return;
    }

    m_root = QDir::fromNativeSeparators(fi.canonicalFilePath());
}

QString PathGuard::resolve(const QString &userPath, QString *error) const
{
    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return QString();
    };

    if (m_root.isEmpty())
        return fail(QStringLiteral("未设置工作区目录，Agent 不能访问任何文件"));

    const QString trimmed = userPath.trimmed();
    if (trimmed.isEmpty())
        return fail(QStringLiteral("路径为空"));

    // 相对路径按工作区解析
    QFileInfo requested(trimmed);
    QString abs = requested.isAbsolute() ? trimmed : m_root + QLatin1Char('/') + trimmed;
    abs = QDir::cleanPath(abs);

    // 目标存在 → 取真实路径（顺带解开符号链接）
    const QFileInfo target(abs);
    QString canon = target.canonicalFilePath();

    if (canon.isEmpty()) {
        // 目标不存在。必须先把「最接近的已存在祖先目录」解成真实路径，
        // 再把缺失的各级名字拼回去——否则 `工作区/../外面/新文件` 这类
        // 路径会绕过校验。
        //
        // 注意不能只解析直接父目录：写文件时父目录本身可能也不存在
        // （要新建多级目录），那样会误判成「路径无法解析」。
        QFileInfo probe(target.absolutePath());
        QStringList missing;
        while (!probe.exists() && !probe.isRoot()) {
            missing.prepend(probe.fileName());
            probe.setFile(probe.absolutePath());
        }
        const QString baseCanon = probe.canonicalFilePath();
        if (baseCanon.isEmpty())
            return fail(QStringLiteral("路径无法解析: %1").arg(userPath));

        QString rebuilt = baseCanon;
        for (const QString &part : missing)
            rebuilt += QLatin1Char('/') + part;
        rebuilt += QLatin1Char('/') + target.fileName();
        canon = QDir::cleanPath(rebuilt);
    }

    canon = QDir::fromNativeSeparators(canon);
    const QString root = QDir::fromNativeSeparators(m_root);

    // Windows 文件系统不区分大小写，比较也必须一致
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    const bool inside = (QString::compare(canon, root, cs) == 0)
                        || canon.startsWith(root + QLatin1Char('/'), cs);

    if (!inside)
        return fail(QStringLiteral("路径超出工作区范围: %1").arg(userPath));

    if (error)
        error->clear();
    return canon;
}
