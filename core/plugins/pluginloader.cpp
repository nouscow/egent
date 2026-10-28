#include "core/plugins/pluginloader.h"

#include "core/plugins/ibailianplugin.h"
#include "core/plugins/plugincontext.h"
#include "core/providers/providerregistry.h"
#include "core/skills/skillregistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>
#include <QSet>

namespace {

void pluginLog(const QString &level, const QString &msg)
{
    QString tag = level == QStringLiteral("error")
                      ? QStringLiteral("[plugin:error]")
                      : QStringLiteral("[plugin]");
    if (level == QLatin1String("error"))
        qWarning().noquote() << tag << msg;
    else
        qInfo().noquote() << tag << msg;
}

} // namespace

PluginLoader &PluginLoader::instance()
{
    static PluginLoader loader;
    return loader;
}

PluginLoader::~PluginLoader()
{
    unloadAll();
}

QString PluginLoader::pluginsDir() const
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir d(dir + QStringLiteral("/plugins"));
    d.mkpath(QStringLiteral("."));
    return d.absolutePath();
}

PluginLoader::LoadResult PluginLoader::loadAll(QWidget *parentWidget)
{
    LoadResult result;

    const QDir dir(pluginsDir());
    const QStringList entries = dir.entryList(QStringList{QStringLiteral("*.dll"), QStringLiteral("*.so"), QStringLiteral("*.dylib")},
                                              QDir::Files | QDir::Readable, QDir::Name);
    if (entries.isEmpty()) {
        qInfo().noquote() << "[plugin] plugins 目录无插件，跳过加载：" << dir.absolutePath();
        return result;
    }

    for (const QString &entry : entries) {
        QString err;
        if (installOne(dir.filePath(entry), parentWidget, &err)) {
            ++result.loaded;
        } else {
            result.errors << err;
            qWarning().noquote() << "[plugin] 加载失败:" << err;
        }
    }
    return result;
}

bool PluginLoader::installOne(const QString &filePath, QWidget *parentWidget, QString *error)
{
    auto loader = std::make_unique<QPluginLoader>(filePath);
    QObject *instance = loader->instance(); // 触发实际加载；ABI/IID 不符返回 null
    if (!instance) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(QFileInfo(filePath).fileName(),
                                                  loader->errorString());
        return false;
    }

    auto *plugin = qobject_cast<IBailianPlugin *>(instance);
    if (!plugin) {
        if (error)
            *error = QStringLiteral("%1: 不是 Egent 插件（IBailianPlugin IID 不匹配）")
                         .arg(QFileInfo(filePath).fileName());
        return false;
    }

    // 插件 id 唯一性校验
    for (auto *p : m_plugins) {
        if (p->id() == plugin->id()) {
            if (error)
                *error = QStringLiteral("%1: 插件 id 冲突（%2）")
                             .arg(QFileInfo(filePath).fileName(), plugin->id());
            return false;
        }
    }

    // 上下文与插件同生命周期（由 m_contexts 持有），插件在 uninstall 时
    // 仍需经 PluginContext 访问注入口，不能在 install 返回后随栈销毁。
    auto ctx = std::make_unique<PluginContext>();
    ctx->setSkillRegistry(&SkillRegistry::instance());
    ctx->setProviderRegistry(&ProviderRegistry::instance());
    ctx->setPluginId(plugin->id());
    ctx->setParentWidget(parentWidget);
    ctx->setLogFn(pluginLog);

    const QString installErr = plugin->install(ctx.get());
    if (!installErr.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1: 安装失败 - %2")
                         .arg(QFileInfo(filePath).fileName(), installErr);
        return false;
    }

    m_plugins.append(plugin);
    m_loaders.push_back(std::move(loader));
    m_contexts.push_back(std::move(ctx));
    qInfo().noquote() << QStringLiteral("[plugin] 已加载 %1 v%2（%3）")
                             .arg(plugin->name(), plugin->version(), plugin->id());
    return true;
}

void PluginLoader::unloadAll()
{
    for (auto it = m_plugins.crbegin(); it != m_plugins.crend(); ++it) {
        (*it)->uninstall();
        qInfo().noquote() << QStringLiteral("[plugin] 已卸载 %1").arg((*it)->id());
    }
    m_plugins.clear();

    for (auto &loader : m_loaders) {
        if (loader && loader->isLoaded())
            loader->unload();
    }
    m_loaders.clear();
    m_contexts.clear();
}