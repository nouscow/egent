#include "thememanager.h"

#include "qsstemplate.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

ThemeManager &ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

void ThemeManager::initialize(const QString &initialId, bool followSystem)
{
    if (m_initialized)
        return;
    m_initialized = true;

    reloadTemplate();
    m_repo.reload();

    for (const QString &line : m_repo.diagnostics())
        qWarning().noquote() << QStringLiteral("[theme]") << line;

    m_followSystem = followSystem;

    // 跟随系统时监听系统配色变化（Qt 6.5+ 的官方封装，不碰注册表）
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this,
                &ThemeManager::onSystemColorSchemeChanged);
    }

    QString target = initialId;
    if (target.isEmpty()) {
        target = m_followSystem ? pickIdForSystem() : m_repo.fallbackId();
    }
    if (!applyTheme(target)) {
        // 兜底：任何可用主题都行，绝不让界面停在无样式状态
        const QString fb = m_repo.fallbackId();
        if (!fb.isEmpty())
            applyTheme(fb);
        else
            qWarning().noquote() << "[theme] 没有可用主题，保持 Qt 默认样式";
    }
}

void ThemeManager::reloadTemplate()
{
    QFile f(QStringLiteral(":/qss/app.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[theme] 无法打开 :/qss/app.qss";
        m_template.clear();
        return;
    }
    m_template = QString::fromUtf8(f.readAll());
}

QStringList ThemeManager::availableThemes() const
{
    return m_repo.ids();
}

QString ThemeManager::themeDisplayName(const QString &id) const
{
    return m_repo.displayName(id);
}

QString ThemeManager::pickIdForSystem() const
{
    QString want;
    if (auto *hints = QGuiApplication::styleHints()) {
        want = (hints->colorScheme() == Qt::ColorScheme::Dark)
                   ? QStringLiteral("dark")
                   : QStringLiteral("light");
    } else {
        want = QStringLiteral("light");
    }
    if (m_repo.contains(want))
        return want;
    // 系统配色对应的内置主题不存在时，退回第一个可用主题
    return m_repo.fallbackId();
}

void ThemeManager::onSystemColorSchemeChanged()
{
    if (!m_followSystem)
        return;
    const QString id = pickIdForSystem();
    if (!id.isEmpty() && id != m_currentId)
        applyTheme(id);
}

void ThemeManager::setFontSize(int px)
{
    if (m_fontSize == px)
        return;
    m_fontSize = qBound(0, px, 24);
    applyResolvedTheme();
    emit themeChanged(m_currentId);
}

bool ThemeManager::applyTheme(const QString &id)
{
    const ThemeData *data = m_repo.find(id);
    if (!data) {
        qWarning().noquote() << QStringLiteral("[theme] 主题不存在: %1").arg(id);
        return false;
    }

    m_currentId = id;
    applyResolvedTheme();
    emit themeChanged(id);
    return true;
}

void ThemeManager::applyResolvedTheme()
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;

    const ThemeData *data = m_repo.find(m_currentId);
    if (!data)
        return;

    // ---- 1. 先同步 QPalette ----
    // QSS 覆盖不到的角落（placeholder、选区、部分原生绘制）只认 Palette，
    // 因此必须两边都给，且先设 Palette 再设样式表，避免被覆盖。
    const QColor window = data->color(QStringLiteral("bg.window"), Qt::white);
    const QColor surface = data->color(QStringLiteral("bg.surface"), window);
    const QColor text = data->color(QStringLiteral("text.primary"), Qt::black);
    const QColor placeholder =
        data->color(QStringLiteral("text.placeholder"), text);
    const QColor accent = data->color(QStringLiteral("accent"), Qt::blue);
    const QColor onAccent =
        data->color(QStringLiteral("text.onAccent"), Qt::white);

    QPalette pal = app->palette();
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, surface);
    pal.setColor(QPalette::AlternateBase, data->color(QStringLiteral("bg.elevated"), surface));
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, data->color(QStringLiteral("bg.elevated"), surface));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::ToolTipBase, surface);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, onAccent);
    pal.setColor(QPalette::PlaceholderText, placeholder);
    pal.setColor(QPalette::Disabled, QPalette::Text,
                 data->color(QStringLiteral("text.placeholder"), text));
    app->setPalette(pal);

    // ---- 2. 渲染并应用 QSS ----
    if (m_template.isEmpty())
        return;

    const auto result = QssTemplate::render(
        m_template, [this, data](const QString &name) {
            // 字号覆盖：用户选了字号就压过主题默认值，标题/小字按 ±1 跟随，
            // 避免调大后标题反而比正文小
            if (m_fontSize > 0) {
                if (name == QLatin1String("font.size"))
                    return QString::number(m_fontSize);
                if (name == QLatin1String("font.size.title"))
                    return QString::number(m_fontSize + 1);
                if (name == QLatin1String("font.size.small"))
                    return QString::number(m_fontSize - 1);
            }
            return data->token(name);
        });

    if (result.hasUnknown()) {
        qWarning().noquote() << QStringLiteral("[theme] 主题 %1 有未定义令牌: %2")
                                    .arg(m_currentId, result.unknownTokens.join(QStringLiteral(", ")));
    }

    app->setStyleSheet(result.qss);
    qInfo().noquote() << QStringLiteral("[theme] 已应用 %1（%2）").arg(m_currentId, data->name());
}

void ThemeManager::setFollowSystem(bool on)
{
    m_followSystem = on;
    if (on) {
        const QString id = pickIdForSystem();
        if (!id.isEmpty() && id != m_currentId)
            applyTheme(id);
    }
}

QColor ThemeManager::color(const QString &tokenName, const QColor &fallback) const
{
    if (const ThemeData *d = m_repo.find(m_currentId))
        return d->color(tokenName, fallback);
    return fallback;
}

QString ThemeManager::token(const QString &tokenName) const
{
    if (const ThemeData *d = m_repo.find(m_currentId))
        return d->token(tokenName);
    return QString();
}

QStringList ThemeManager::diagnostics() const
{
    QStringList all = m_repo.diagnostics();
    if (const ThemeData *d = m_repo.find(m_currentId)) {
        for (const QString &t : d->missingTokens())
            all << QStringLiteral("缺失令牌: %1").arg(t);
        for (const QString &t : d->cyclicTokens())
            all << QStringLiteral("循环引用: %1").arg(t);
    }
    return all;
}
