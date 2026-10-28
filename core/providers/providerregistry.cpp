#include "providerregistry.h"

#include <QDebug>
#include <QSet>

OpenAiCompatProvider::Config ProviderRegistry::bailianConfig()
{
    return {
        QStringLiteral("bailian"),
        QStringLiteral("百炼"),
        QUrl(QStringLiteral(
            "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions")),
        QStringLiteral("DASHSCOPE_API_KEY"),
        QStringLiteral("dashscope_api_key"),
        {QStringLiteral("qwen3.8-flash"), QStringLiteral("qwen3.8-max"),
         QStringLiteral("qwen-plus"), QStringLiteral("qwen3-vl-plus")},
    };
}

OpenAiCompatProvider::Config ProviderRegistry::deepseekConfig()
{
    return {
        QStringLiteral("deepseek"),
        QStringLiteral("DeepSeek"),
        QUrl(QStringLiteral("https://api.deepseek.com/v1/chat/completions")),
        QStringLiteral("DEEPSEEK_API_KEY"),
        QStringLiteral("deepseek_api_key"),
        {QStringLiteral("deepseek-chat"), QStringLiteral("deepseek-reasoner")},
    };
}

OpenAiCompatProvider::Config ProviderRegistry::openaiConfig()
{
    return {
        QStringLiteral("openai"),
        QStringLiteral("OpenAI"),
        QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions")),
        QStringLiteral("OPENAI_API_KEY"),
        QStringLiteral("openai_api_key"),
        {QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini")},
    };
}

ProviderRegistry &ProviderRegistry::instance()
{
    static ProviderRegistry registry;
    return registry;
}

void ProviderRegistry::registerBuiltins()
{
    for (const auto &cfg : {bailianConfig(), deepseekConfig(), openaiConfig()}) {
        if (!m_configs.contains(cfg.id))
            m_configs.insert(cfg.id, cfg);
    }
}

QStringList ProviderRegistry::ids() const
{
    return m_configs.keys();
}

QString ProviderRegistry::displayName(const QString &id) const
{
    return m_configs.value(id).displayName;
}

QStringList ProviderRegistry::models(const QString &id) const
{
    return m_configs.value(id).models;
}

IModelProvider *ProviderRegistry::provider(const QString &id)
{
    if (m_instances.contains(id))
        return m_instances.value(id);

    if (!m_configs.contains(id)) {
        qWarning().noquote() << "[provider] 未知厂商:" << id;
        return nullptr;
    }

    auto *p = new OpenAiCompatProvider(m_configs.value(id));
    m_instances.insert(id, p);
    return p;
}

IModelProvider *ProviderRegistry::current()
{
    if (m_configs.isEmpty())
        registerBuiltins();
    return provider(m_currentId);
}

void ProviderRegistry::setCurrentId(const QString &id)
{
    if (m_configs.contains(id) || m_configs.isEmpty())
        m_currentId = id;
}

void ProviderRegistry::registerCustomProvider(
    const QString &id, const QString &displayName, const QStringList &models,
    const OpenAiCompatProvider::Config &config)
{
    OpenAiCompatProvider::Config cfg = config;
    cfg.id = id;
    cfg.displayName = displayName;
    cfg.models = models;
    m_configs.insert(id, cfg);
    // 已创建旧实例时强制重建，避免插件更新后仍持有过期配置
    if (m_instances.contains(id)) {
        delete m_instances.take(id);
    }
}

void ProviderRegistry::unregisterCustomProvider(const QString &id)
{
    static const QSet<QString> builtins = {QStringLiteral("bailian"),
                                           QStringLiteral("deepseek"),
                                           QStringLiteral("openai")};
    if (builtins.contains(id))
        return; // 内置厂商不允许插件注销
    if (m_currentId == id)
        m_currentId = QStringLiteral("bailian"); // 退回内置默认
    m_configs.remove(id);
    if (m_instances.contains(id))
        delete m_instances.take(id);
}
