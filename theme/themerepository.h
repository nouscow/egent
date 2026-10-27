#pragma once

#include "themedata.h"

#include <QHash>
#include <QString>
#include <QStringList>

/**
 * 主题仓储：负责「主题从哪来」。
 *
 * 来源优先级（后者覆盖前者，按 id 同名）：
 *   1. 内置资源   :/themes/*.json
 *   2. 用户目录   <AppConfigLocation>/themes/*.json
 *
 * 调用方只关心 id 列表与取数据，不关心来源。将来要加远程主题，
 * 在这里多扫一处即可，ThemeManager 与 UI 都不用改。
 */
class ThemeRepository
{
public:
    void reload();

    QStringList ids() const { return m_order; }
    bool contains(const QString &id) const { return m_themes.contains(id); }

    QString displayName(const QString &id) const;
    const ThemeData *find(const QString &id) const;

    /** 没有可用主题时返回空；否则返回第一个。 */
    QString fallbackId() const;

    /** 初始化时的诊断信息，供启动日志输出。 */
    QStringList diagnostics() const { return m_diagnostics; }

    /** 用户主题目录（不存在时返回路径本身，调用方可按需创建）。 */
    static QString userThemeDir();

private:
    void loadFromDir(const QString &dirPath, bool isResource);

    QHash<QString, ThemeData> m_themes;
    QStringList m_order;
    QStringList m_diagnostics;
};
