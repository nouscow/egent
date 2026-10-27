#pragma once

#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * 一份主题的数据表示（三层令牌：primitive → semantic → component）。
 *
 * 设计要点：
 *  - 主题是「数据」不是「行为」，因此这里没有多态基类，只有一份结构体。
 *  - 令牌值可以是字面颜色（#rrggbb / rgb() / rgba() / transparent），
 *    也可以是对其它令牌名字的引用；load 后统一 resolve 成最终色值。
 *  - 解析期做三件事：多层展开、循环引用检测、缺失令牌记录。
 *    这些错误一律通过 missingTokens()/cyclicTokens() 暴露，由上层告警，绝不静默吞掉。
 */
class ThemeData
{
public:
    ThemeData() = default;

    bool loadFromJson(const QByteArray &json, QString *error = nullptr);
    bool loadFromFile(const QString &path, QString *error = nullptr);

    bool isValid() const { return m_valid; }
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    int version() const { return m_version; }

    /** 展开后的令牌字符串；未定义时返回空 QString。 */
    QString token(const QString &name) const { return m_resolved.value(name); }

    /** 展开后的颜色；未定义或无法解析时返回 fallback。 */
    QColor color(const QString &name, const QColor &fallback = QColor()) const;

    /** 被引用但未定义的令牌名（去重、已排序）。 */
    QStringList missingTokens() const;
    /** 参与循环引用的令牌名（去重、已排序）。 */
    QStringList cyclicTokens() const;

private:
    QString rawLookup(const QString &name) const;
    bool resolveAll();
    QString resolveOne(const QString &name, QSet<QString> &stack, bool &ok);

    QString m_id;
    QString m_name;
    int m_version = 1;

    QHash<QString, QString> m_primitive;
    QHash<QString, QString> m_semantic;
    QHash<QString, QString> m_component;

    QHash<QString, QString> m_resolved;
    QSet<QString> m_missing;
    QSet<QString> m_cyclic;

    bool m_valid = false;
};
