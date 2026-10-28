#pragma once

#include "core/providers/imodelprovider.h"
#include "core/providers/openaicompatprovider.h"

#include <QMap>
#include <QStringList>

/**
 * 厂商注册表（单例）。
 *
 * 百炼 / DeepSeek / OpenAI 都走 OpenAI 兼容协议，差异只在端点和 Key，
 * 所以这里只存配置、按需创建实例。新增厂商 = 在 registerBuiltins 里加一条，
 * 界面与 AgentLoop 完全不用改（企划书决策 5）。
 */
class ProviderRegistry
{
public:
    static ProviderRegistry &instance();

    /** 注册三家内置厂商（幂等） */
    void registerBuiltins();

    QStringList ids() const;
    QString displayName(const QString &id) const;
    QStringList models(const QString &id) const;
    OpenAiCompatProvider::Config configFor(const QString &id) const
    {
        return m_configs.value(id);
    }

    /** 取（或懒创建）指定厂商的 provider 实例 */
    IModelProvider *provider(const QString &id);
    IModelProvider *current();
    QString currentId() const { return m_currentId; }
    void setCurrentId(const QString &id);

    /**
     * 注册自定义厂商（插件生态 P4.2 经 PluginContext 调用）。
     * id 冲突时覆盖旧配置；config.id 必须与 id 一致。
     */
    void registerCustomProvider(const QString &id, const QString &displayName,
                                const QStringList &models,
                                const OpenAiCompatProvider::Config &config);

    /** 注销自定义厂商（插件卸载时调用）；内置三家与正在使用的厂商不可注销 */
    void unregisterCustomProvider(const QString &id);

    static OpenAiCompatProvider::Config bailianConfig();
    static OpenAiCompatProvider::Config deepseekConfig();
    static OpenAiCompatProvider::Config openaiConfig();

private:
    ProviderRegistry() = default;

    QMap<QString, OpenAiCompatProvider::Config> m_configs;
    QMap<QString, IModelProvider *> m_instances;
    QString m_currentId = QStringLiteral("bailian");
};
