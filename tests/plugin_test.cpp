// 插件生态端到端测试（P4.1–P4.4）
// 依赖构建产物：build-release/plugins/helloplugin.dll（由 egent 目标 POST_BUILD 复制）
#include "core/plugins/ibailianplugin.h"
#include "core/plugins/plugincontext.h"
#include "core/plugins/pluginloader.h"
#include "core/providers/providerregistry.h"
#include "core/skills/skillregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QtTest>

class PluginTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 构建期已把 helloplugin.dll 复制到 applicationDirPath()/plugins
    }

    void skillRegistryInjectable()
    {
        auto &reg = SkillRegistry::instance();
        reg.registerBuiltins();
        QVERIFY(reg.contains(QStringLiteral("help")));
        QVERIFY(reg.contains(QStringLiteral("time")));
        QVERIFY(reg.contains(QStringLiteral("calc")));
        QVERIFY(!reg.contains(QStringLiteral("hello")));
    }

    void customProviderRegistry()
    {
        auto &reg = ProviderRegistry::instance();
        reg.registerBuiltins();
        OpenAiCompatProvider::Config cfg{QStringLiteral("x"), QStringLiteral("X"),
                                        QUrl(QStringLiteral("https://example.com/v1")),
                                        QStringLiteral("X_KEY"),
                                        QStringLiteral("x_key"),
                                        {QStringLiteral("x-1")}};
        reg.registerCustomProvider(QStringLiteral("plugfake"), QStringLiteral("插件厂"),
                                   {QStringLiteral("plug-1")}, cfg);
        QVERIFY(reg.ids().contains(QStringLiteral("plugfake")));
        QCOMPARE(reg.displayName(QStringLiteral("plugfake")), QStringLiteral("插件厂"));
        reg.unregisterCustomProvider(QStringLiteral("plugfake"));
        QVERIFY(!reg.ids().contains(QStringLiteral("plugfake")));
        // 内置三家不可注销
        reg.unregisterCustomProvider(QStringLiteral("bailian"));
        QVERIFY(reg.ids().contains(QStringLiteral("bailian")));
    }

    void pluginDirExists()
    {
        const QString dir = PluginLoader::instance().pluginsDir();
        QVERIFY(QDir(dir).exists());
        QVERIFY(QFileInfo::exists(dir + QStringLiteral("/helloplugin.dll")));
    }

    void loadAllInstallsHelloSkill()
    {
        auto &skills = SkillRegistry::instance();
        skills.reloadBuiltins();
        QVERIFY(!skills.contains(QStringLiteral("hello")));

        const auto result = PluginLoader::instance().loadAll(nullptr);
        QCOMPARE(result.loaded, 1);
        QVERIFY(result.errors.isEmpty());

        // /hello 技能已由插件注册
        QVERIFY(skills.contains(QStringLiteral("hello")));
        auto *hello = skills.skillByName(QStringLiteral("hello"));
        QVERIFY(hello != nullptr);
        const QString out = hello->execute({QStringLiteral("Egent")});
        QVERIFY(out.contains(QStringLiteral("Egent")));
        QVERIFY(out.contains(QStringLiteral("Hello")));

        // 卸载后技能被回收（PluginContext 由 PluginLoader 持有，随卸载统一释放）
        PluginLoader::instance().unloadAll();
        QVERIFY(!skills.contains(QStringLiteral("hello")));
    }

    void failedPluginIsolated()
    {
        // 失败隔离：plugins 目录中混入"假插件"（真实 Qt 动态库改名，无
        // IBailianPlugin 元数据），loadAll 必须记录该插件失败但不崩溃，
        // 也不影响真插件（helloplugin）的加载与注册。
        const QString realPlugins = PluginLoader::instance().pluginsDir();

        const QString src = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/../bin/Qt6Core.dll");
        QString realSrc = src;
        if (!QFileInfo::exists(realSrc))
            realSrc = QCoreApplication::applicationDirPath() +
                      QStringLiteral("/Qt6Core.dll");

        bool fakeAdded = false;
        if (QFileInfo::exists(realSrc)) {
            fakeAdded = QFile::copy(realSrc, realPlugins + QStringLiteral("/fakep.dll"));
        }

        ProviderRegistry::instance().registerBuiltins();
        SkillRegistry::instance().reloadBuiltins();
        const auto result = PluginLoader::instance().loadAll(nullptr); // 应继续执行不崩溃

        if (fakeAdded) {
            // 假插件加载失败被记录（失败隔离），真插件不受影响并注册 /hello
            QVERIFY(!result.errors.isEmpty());
            QVERIFY(SkillRegistry::instance().contains(QStringLiteral("hello")));
        } else {
            QVERIFY(result.errors.isEmpty());
        }

        PluginLoader::instance().unloadAll();
        QVERIFY(!SkillRegistry::instance().contains(QStringLiteral("hello")));
        if (fakeAdded)
            QFile::remove(realPlugins + QStringLiteral("/fakep.dll"));
    }
};

QTEST_GUILESS_MAIN(PluginTest)
#include "plugin_test.moc"