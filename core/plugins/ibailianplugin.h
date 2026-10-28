#pragma once

#include <QObject>
#include <QString>

class PluginContext;

/**
 * 插件主接口（插件生态，P4.1）。
 *
 * 契约对齐既有 `IModelProvider` 的 Q_DECLARE_INTERFACE 范式（见
 * core/providers/imodelprovider.h）：插件是带 Q_OBJECT 元数据的动态库，
 * 主程序用 QPluginLoader 按 IID 加载后 qobject_cast 获取本接口。
 *
 * 生命周期约定：
 *  1. 主程序加载插件 dll → 取到 IBailianPlugin*；
 *  2. 调用 install(ctx) 让插件把能力注册进 PluginContext 暴露的注入口
 *     （SkillRegistry / ProviderRegistry 等），返回空串表示成功；
 *  3. 主程序退出前（或插件被卸载时）调用 uninstall() 释放注册；
 *  4. 插件对象与 dll 由 PluginLoader 统一持有与释放，插件自身不得自行 delete。
 */
class IBailianPlugin : public QObject
{
    Q_OBJECT
public:
    explicit IBailianPlugin(QObject *parent = nullptr) : QObject(parent) {}
    ~IBailianPlugin() override = default;

    /** 插件唯一 id（全小写，如 "hello"），用于数据目录与日志归属 */
    virtual QString id() const = 0;

    /** 插件显示名 */
    virtual QString name() const = 0;

    /** 插件版本（建议 semver，如 "1.0.0"） */
    virtual QString version() const = 0;

    /** 插件用途说明（展示 / 日志用） */
    virtual QString description() const = 0;

    /**
     * 插件安装钩子：在这里把能力注册进 PluginContext 注入口。
     * @param ctx 注入了 SkillRegistry / ProviderRegistry 的上下文，
     *            插件可通过 ctx->dataDir() 获取专属数据目录。
     * @return 空串表示安装成功；非空返回失败原因，主程序会据此卸载并记录错误。
     */
    virtual QString install(PluginContext *ctx)
    {
        Q_UNUSED(ctx)
        return {};
    }

    /** 插件卸载钩子：释放 install 中注册的能力（如 unregisterSkill） */
    virtual void uninstall() {}
};

#define IBailianPlugin_IID "com.egent.IBailianPlugin/1.0"
Q_DECLARE_INTERFACE(IBailianPlugin, IBailianPlugin_IID)