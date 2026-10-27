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
        // 目标不存在：至少要把父目录解实，再拼回文件名，
        // 否则 `工作区/../外面/新文件` 这类路径会绕过校验
        const QFileInfo parent(target.absolutePath());
        const QString parentCanon = parent.canonicalFilePath();
        if (parentCanon.isEmpty())
            return fail(QStringLiteral("路径无法解析: %1").arg(userPath));
        canon = QDir::cleanPath(parentCanon + QLatin1Char('/') + target.fileName());
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
