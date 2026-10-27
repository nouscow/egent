#include "themerepository.h"

#include <QDebug>
#include <QDir>
#include <QStandardPaths>

QString ThemeRepository::userThemeDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + QStringLiteral("/themes");
}

void ThemeRepository::reload()
{
    m_themes.clear();
    m_order.clear();
    m_diagnostics.clear();

    // 1) 内置主题
    loadFromDir(QStringLiteral(":/themes"), true);
    // 2) 用户主题，同名覆盖内置
    loadFromDir(userThemeDir(), false);

    if (m_order.isEmpty())
        m_diagnostics << QStringLiteral("没有任何主题被加载，界面将使用 Qt 默认样式");
}

void ThemeRepository::loadFromDir(const QString &dirPath, bool isResource)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return;

    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

    for (const QString &fileName : files) {
        const QString fullPath = dir.filePath(fileName);
        ThemeData data;
        QString error;
        if (!data.loadFromFile(fullPath, &error)) {
            m_diagnostics << QStringLiteral("跳过主题 %1（%2）").arg(fileName, error);
            continue;
        }

        const QString id = data.id();
        const bool shadowing = m_themes.contains(id);
        if (shadowing) {
            m_diagnostics << QStringLiteral("主题 %1 被用户目录版本覆盖").arg(id);
        }

        m_themes.insert(id, data);
        if (!m_order.contains(id))
            m_order.append(id);

        if (!isResource || !shadowing) {
            qInfo().noquote() << QStringLiteral("[theme] 载入 %1 <- %2").arg(id, fullPath);
        }
    }
}

QString ThemeRepository::displayName(const QString &id) const
{
    const ThemeData *d = find(id);
    return d ? d->name() : id;
}

const ThemeData *ThemeRepository::find(const QString &id) const
{
    auto it = m_themes.constFind(id);
    return it == m_themes.constEnd() ? nullptr : &it.value();
}

QString ThemeRepository::fallbackId() const
{
    return m_order.isEmpty() ? QString() : m_order.first();
}
