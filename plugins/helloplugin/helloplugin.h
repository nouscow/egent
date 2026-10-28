#pragma once

#include "core/plugins/ibailianplugin.h"

/**
 * 示例插件（插件生态，P4.4）。
 *
 * 演示三件事：
 *  1. 定义符合 Q_DECLARE_INTERFACE 契约的插件类（Q_OBJECT + Q_PLUGIN_METADATA）；
 *  2. install 时经 PluginContext 向 SkillRegistry 注册一个 `/hello` 技能；
 *  3. 使用 PluginContext::dataDir() 与 log 通道，展示插件专属数据目录隔离。
 *
 * 构建：cmake 目标 helloplugin（qt_add_plugin），产出的 helloplugin.dll
 * 部署到主程序同目录 plugins/ 下即可被 PluginLoader 自动发现加载。
 */
class HelloPlugin : public IBailianPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IBailianPlugin_IID)
    Q_INTERFACES(IBailianPlugin)
public:
    QString id() const override { return QStringLiteral("hello"); }
    QString name() const override { return QStringLiteral("Hello 演示插件"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString description() const override
    {
        return QStringLiteral("示例插件：注册 /hello 技能，演示插件注册表注入口与数据目录隔离。");
    }

    QString install(PluginContext *ctx) override;
    void uninstall() override;

private:
    PluginContext *m_ctx = nullptr;
    bool m_installed = false;
};