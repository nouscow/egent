#pragma once

#include <QStringList>

#include <memory>
#include <vector>

class IBailianPlugin;
class PluginContext;
class QPluginLoader;
class QWidget;

/**
 * 插件加载器（插件生态，P4.3）。
 *
 * 职责：
 *  - 扫描应用目录 plugins/ 下的插件 dll；
 *  - 用 QPluginLoader 逐个加载，单插件失败（缺 Qt DLL / ABI 不符 / IID
 *    不匹配 / install 失败）只记录错误，绝不中断主程序（失败隔离）；
 *  - 统一持有插件对象与加载器，退出时逆序 unload，先调 uninstall() 释放注册。
 *
 * 安全约束：
 *  - 只加载与主程序同构建环境的插件（MinGW 同版本约束在文档声明）；
 *  - 插件只能获得 PluginContext 注入的最小能力，无任意文件/命令权限。
 */
class PluginLoader
{
public:
    static PluginLoader &instance();

    ~PluginLoader();

    struct LoadResult
    {
        int loaded = 0;           // 成功加载并安装的插件数
        QStringList errors;      // 失败说明（含原因），空表示全成功
    };

    /**
     * 加载并安装 pluginsDir() 下全部插件。
     * @param parentWidget 供插件弹窗挂靠；可传 nullptr。
     */
    LoadResult loadAll(QWidget *parentWidget);

    /** 卸载全部插件（逆序 uninstall + unload），幂等 */
    void unloadAll();

    /** 插件目录：应用可执行文件同级 /plugins，不存在则创建 */
    QString pluginsDir() const;

    QList<IBailianPlugin *> plugins() const { return m_plugins; }

private:
    PluginLoader() = default;
    Q_DISABLE_COPY_MOVE(PluginLoader)

    bool installOne(const QString &filePath, QWidget *parentWidget,
                    QString *error);

    QList<IBailianPlugin *> m_plugins;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    // 上下文与插件同生命周期：install 期间创建、unloadAll 末尾释放，
    // 保证插件在 uninstall() 中仍可安全访问注入的注入口。
    std::vector<std::unique_ptr<PluginContext>> m_contexts;
};