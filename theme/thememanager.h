#pragma once

#include "themerepository.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

class QApplication;

/**
 * 主题门面（Facade + Singleton）。
 *
 * 对外只暴露四类操作：
 *   - 查询：availableThemes / currentThemeId / color / token
 *   - 应用：applyTheme
 *   - 策略：setFollowSystem
 *   - 通知：themeChanged 信号
 *
 * 内部把活儿分给 Repository（加载）、QssTemplate（渲染）、自己（应用 + 发信号），
 * 门面本身很薄。所有控件都不允许自己去读 JSON 或拼样式。
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager &instance();

    /** 应用启动时调用一次。id 为空时按「跟随系统」或第一个可用主题决定。 */
    void initialize(const QString &initialId = QString(), bool followSystem = true);

    QStringList availableThemes() const;
    QString themeDisplayName(const QString &id) const;
    QString currentThemeId() const { return m_currentId; }

    /** 主动切换主题；失败（id 不存在）时保持原样并返回 false。 */
    bool applyTheme(const QString &id);

    /**
     * 设置正文字号（px）；传 0 表示跟随主题默认。
     * 标题与辅助文字会按 ±1 自动跟随，保持原有层级关系。
     */
    void setFontSize(int px);
    int fontSize() const { return m_fontSize; }

    void setFollowSystem(bool on);
    bool followSystem() const { return m_followSystem; }

    /** 自绘用：取展开后的颜色。 */
    QColor color(const QString &tokenName, const QColor &fallback = QColor()) const;
    /** 模板用：取展开后的字符串，未定义返回空。 */
    QString token(const QString &tokenName) const;

    /** 启动期的诊断信息（缺令牌、被覆盖的主题等），供日志输出。 */
    QStringList diagnostics() const;

signals:
    void themeChanged(const QString &id);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(ThemeManager)

    void reloadTemplate();
    void applyResolvedTheme();
    QString pickIdForSystem() const;
    void onSystemColorSchemeChanged();

    ThemeRepository m_repo;
    QString m_template;
    QString m_currentId;
    int m_fontSize = 0;
    bool m_followSystem = true;
    bool m_initialized = false;
};
