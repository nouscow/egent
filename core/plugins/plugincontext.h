#pragma once

#include <QString>

#include <functional>

class SkillRegistry;
class ProviderRegistry;
class QWidget;

/**
 * 插件上下文（插件生态，P4.2）。
 *
 * 主程序向插件开放的最小能力集合：
 *  - 技能注册表（SkillRegistry）：插件可注册 / 注销 / 命令技能；
 *  - 厂商注册表（ProviderRegistry）：插件可注册自定义模型厂商；
 *  - 专属数据目录（dataDir）：AppData/egent/plugins/<pluginId>，插件只允许
 *    读写自己的目录，防止越权访问主程序文件（权限最小化）。
 *  - 日志回调（log）：插件日志统一走主程序日志通道，便于审计。
 *  - 父窗口（parentWidget）：允许插件弹窗时挂靠主窗口（无 UI 插件传 null）。
 */
class PluginContext
{
public:
    PluginContext() = default;

    SkillRegistry *skillRegistry() const { return m_skills; }
    void setSkillRegistry(SkillRegistry *r) { m_skills = r; }

    ProviderRegistry *providerRegistry() const { return m_providers; }
    void setProviderRegistry(ProviderRegistry *r) { m_providers = r; }

    void setPluginId(const QString &id) { m_pluginId = id; }
    QString pluginId() const { return m_pluginId; }

    QWidget *parentWidget() const { return m_parentWidget; }
    void setParentWidget(QWidget *w) { m_parentWidget = w; }

    /** 插件专属数据目录（首次调用自动创建），为空时落在系统临时目录 */
    QString dataDir() const;

    using LogFn = std::function<void(const QString &level, const QString &msg)>;
    void setLogFn(LogFn fn) { m_log = std::move(fn); }

    /** 写一条日志（level: debug/info/warn/error） */
    void log(const QString &level, const QString &msg) const;

private:
    SkillRegistry *m_skills = nullptr;
    ProviderRegistry *m_providers = nullptr;
    QString m_pluginId;
    QWidget *m_parentWidget = nullptr;
    LogFn m_log;
};