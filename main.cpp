#include "mainwindow.h"
#include "theme/thememanager.h"
#include "ui/avatarstore.h"
#include "ui/messagebubble.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QDebug>
#include <QLocale>
#include <QContextMenuEvent>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("egent"));
    QApplication::setOrganizationName(QStringLiteral("egent"));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "egent_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    // 调试用：--theme <id> 指定初始主题；--shot <png> 截图后退出（自动化视觉检查）
    const QStringList args = QCoreApplication::arguments();
    QString initialTheme = QStringLiteral("dark");
    const int themeIdx = args.indexOf(QStringLiteral("--theme"));
    if (themeIdx > 0 && themeIdx + 1 < args.size())
        initialTheme = args.at(themeIdx + 1);

    // 主题必须在任何窗口创建之前就位，否则会看到一闪而过的默认样式
    ThemeManager::instance().initialize(initialTheme, /*followSystem=*/false);
    for (const QString &line : ThemeManager::instance().diagnostics())
        qWarning().noquote() << "[theme]" << line;

    // 调试用：--avatar-user <png> / --avatar-assistant <png> 临时换图片头像
    const int avUserIdx = args.indexOf(QStringLiteral("--avatar-user"));
    if (avUserIdx > 0 && avUserIdx + 1 < args.size())
        AvatarStore::instance().setImagePath(QStringLiteral("user"), args.at(avUserIdx + 1));
    const int avAiIdx = args.indexOf(QStringLiteral("--avatar-assistant"));
    if (avAiIdx > 0 && avAiIdx + 1 < args.size())
        AvatarStore::instance().setImagePath(QStringLiteral("assistant"), args.at(avAiIdx + 1));

    // 调试用：--workspace <dir> 临时启用 Agent 并指定工作区（会写入设置）
    const int wsIdx = args.indexOf(QStringLiteral("--workspace"));
    if (wsIdx > 0 && wsIdx + 1 < args.size()) {
        QSettings st(QStringLiteral("egent"), QStringLiteral("egent"));
        const QString ws = args.at(wsIdx + 1);
        st.setValue(QStringLiteral("agent/workspace"), ws);
        st.setValue(QStringLiteral("agent/enabled"), !ws.isEmpty());
    }

    MainWindow w;
    w.show();

    // 调试用：--autosend "<文本>" 自动填入输入框并发起一次真实对话（验证流式追加）
    const int autoSendIdx = args.indexOf(QStringLiteral("--autosend"));
    const bool hasAutoSend = (autoSendIdx > 0 && autoSendIdx + 1 < args.size());
    if (hasAutoSend) {
        const QString text = args.at(autoSendIdx + 1);
        QTimer::singleShot(300, [&w, text]() {
            if (auto *box = w.findChild<QPlainTextEdit *>()) {
                box->setPlainText(text);
                QMetaObject::invokeMethod(&w, "onSendClicked", Qt::DirectConnection);
            }
        });
    }

    // 调试用：--export <md> 跳过文件对话框，直接把当前会话导出（验证格式）
    const int exportIdx = args.indexOf(QStringLiteral("--export"));
    if (exportIdx > 0 && exportIdx + 1 < args.size()) {
        const QString path = args.at(exportIdx + 1);
        QString err;
        if (w.exportSessionToFile(-1, path, &err))
            qInfo().noquote() << "[export] 已导出:" << path;
        else
            qWarning().noquote() << "[export] 失败:" << err;
        return 0;
    }

    // 调试用：--shot-menu <png> 模拟右键一条助手消息，把弹出的菜单截下来
    const int menuShotIdx = args.indexOf(QStringLiteral("--shot-menu"));
    if (menuShotIdx > 0 && menuShotIdx + 1 < args.size()) {
        const QString path = args.at(menuShotIdx + 1);
        QTimer::singleShot(400, [&w]() {
            const auto bubbles = w.findChildren<MessageBubble *>();
            for (MessageBubble *b : bubbles) {
                if (b->role() != MessageBubble::Role::Assistant)
                    continue;
                const QPoint local = b->rect().center();
                QContextMenuEvent ev(QContextMenuEvent::Mouse, local,
                                     b->mapToGlobal(local));
                QCoreApplication::sendEvent(b, &ev);
                break;
            }
        });
        // menu.exec() 会阻塞在这里，但嵌套事件循环仍会跑定时器
        QTimer::singleShot(1300, [&w, path]() {
            if (QScreen *scr = QGuiApplication::primaryScreen()) {
                const bool ok = scr->grabWindow(0).save(path);
                qInfo().noquote() << (ok ? "[shot] 菜单已保存:" : "[shot] 保存失败:") << path;
            }
            QCoreApplication::quit();
        });
        return QCoreApplication::exec();
    }

    // 调试用：--shot-settings <png> 单独截设置对话框
    const int dlgShotIdx = args.indexOf(QStringLiteral("--shot-settings"));
    if (dlgShotIdx > 0 && dlgShotIdx + 1 < args.size()) {
        const QString path = args.at(dlgShotIdx + 1);
        auto *dlg = new SettingsDialog(&w);
        dlg->show();
        QTimer::singleShot(800, [dlg, path]() {
            const bool ok = dlg->grab().save(path);
            qInfo().noquote() << (ok ? "[shot] 对话框已保存:" : "[shot] 保存失败:") << path;
            QCoreApplication::quit();
        });
        return QCoreApplication::exec();
    }

    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    if (shotIdx > 0 && shotIdx + 1 < args.size()) {
        const QString path = args.at(shotIdx + 1);
        // 自动发消息时留足流式时间，否则 1.2 秒就够画静态界面
        const int delay = hasAutoSend ? 18000 : 1200;
        QTimer::singleShot(delay, [&w, path]() {
            const bool ok = w.grab().save(path);
            qInfo().noquote() << (ok ? "[shot] 已保存:" : "[shot] 保存失败:") << path;
            QCoreApplication::quit();
        });
    }

    return QCoreApplication::exec();
}
