#include "core/plugins/plugincontext.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

QString PluginContext::dataDir() const
{
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    base.cdUp();
    base.cd(QStringLiteral("egent"));
    base.cd(QStringLiteral("plugins"));

    QString dir = m_pluginId.isEmpty() ? base.absolutePath()
                                       : base.filePath(m_pluginId);
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

void PluginContext::log(const QString &level, const QString &msg) const
{
    if (m_log)
        m_log(level, msg);
}