#include "core/skills/builtinskills.h"

#include "core/skills/skillregistry.h"
#include "core/skills/iskill.h"

#include <QDateTime>
#include <QLocale>
#include <QStringList>

#include <cmath>

/* ------------------------------------------------------------------ */
/* HelpSkill                                                            */
/* ------------------------------------------------------------------ */

QString HelpSkill::execute(const QStringList &args)
{
    const QList<ISkill *> skills = SkillRegistry::instance().skills();
    if (skills.isEmpty())
        return QStringLiteral("当前没有已注册的技能。");

    // /help <命令名>：查看单个技能详细说明
    if (!args.isEmpty()) {
        for (const ISkill *s : skills) {
            if (s->command().compare(args.first(), Qt::CaseInsensitive) == 0) {
                return QStringLiteral("/%1 — %2\n%3")
                    .arg(s->command(), s->summary(), s->description());
            }
        }
        return QStringLiteral("没有名为 /%1 的技能。").arg(args.first());
    }

    QStringList lines;
    lines << QStringLiteral("可用技能（%1 个）：").arg(skills.size());
    for (const ISkill *s : skills) {
        lines << QStringLiteral("  %1%2%3")
                     .arg(QLatin1Char('/'), s->command(),
                          s->summary().isEmpty()
                              ? QString()
                              : QStringLiteral(" — ") + s->summary());
    }
    lines << QStringLiteral("输入 /help <命令名> 查看单个技能说明。");
    return lines.join(QLatin1Char('\n'));
}

/* ------------------------------------------------------------------ */
/* TimeSkill                                                           */
/* ------------------------------------------------------------------ */

QString TimeSkill::execute(const QStringList &args)
{
    Q_UNUSED(args);
    const QDateTime now = QDateTime::currentDateTime();
    const QString week =
        QLocale(QLocale::Chinese).dayName(now.date().dayOfWeek());
    return QStringLiteral("%1 %2")
        .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), week);
}

/* ------------------------------------------------------------------ */
/* CalcSkill —— 递归下降求值器（安全：纯算术、无副作用、限长）           */
/* ------------------------------------------------------------------ */

namespace {

class CalcParser
{
public:
    explicit CalcParser(const QString &src) : m_src(src) {}

    bool parse(double *out, QString *err)
    {
        m_pos = 0;
        skipSpaces();
        const double value = expr(err);
        skipSpaces();
        if (!err->isEmpty())
            return false;
        if (m_pos != m_src.size()) {
            *err = QStringLiteral("无法识别的字符：%1").arg(m_src.at(m_pos));
            return false;
        }
        *out = value;
        return true;
    }

private:
    void skipSpaces()
    {
        while (m_pos < m_src.size() && m_src.at(m_pos).isSpace())
            ++m_pos;
    }

    double expr(QString *err)
    {
        double v = term(err);
        if (!err->isEmpty())
            return 0.0;
        for (;;) {
            skipSpaces();
            if (m_pos >= m_src.size())
                break;
            const QChar c = m_src.at(m_pos);
            if (c == QLatin1Char('+')) {
                ++m_pos;
                v += term(err);
            } else if (c == QLatin1Char('-')) {
                ++m_pos;
                v -= term(err);
            } else {
                break;
            }
            if (!err->isEmpty())
                return 0.0;
        }
        return v;
    }

    double term(QString *err)
    {
        double v = factor(err);
        if (!err->isEmpty())
            return 0.0;
        for (;;) {
            skipSpaces();
            if (m_pos >= m_src.size())
                break;
            const QChar c = m_src.at(m_pos);
            if (c == QLatin1Char('*')) {
                ++m_pos;
                v *= factor(err);
            } else if (c == QLatin1Char('/')) {
                ++m_pos;
                const double d = factor(err);
                if (d == 0.0) {
                    *err = QStringLiteral("除数为 0");
                    return 0.0;
                }
                v /= d;
            } else if (c == QLatin1Char('%')) {
                ++m_pos;
                const double d = factor(err);
                if (d == 0.0) {
                    *err = QStringLiteral("取模除数为 0");
                    return 0.0;
                }
                v = std::fmod(v, d);
            } else {
                break;
            }
            if (!err->isEmpty())
                return 0.0;
        }
        return v;
    }

    double factor(QString *err)
    {
        skipSpaces();
        if (m_pos >= m_src.size()) {
            *err = QStringLiteral("表达式不完整");
            return 0.0;
        }
        const QChar c = m_src.at(m_pos);
        if (c == QLatin1Char('-')) { // 一元负号
            ++m_pos;
            return -factor(err);
        }
        if (c == QLatin1Char('(')) {
            ++m_pos;
            const double v = expr(err);
            if (!err->isEmpty())
                return 0.0;
            skipSpaces();
            if (m_pos >= m_src.size() || m_src.at(m_pos) != QLatin1Char(')')) {
                *err = QStringLiteral("缺少右括号");
                return 0.0;
            }
            ++m_pos;
            return v;
        }
        if (c.isDigit() || c == QLatin1Char('.')) {
            int end = m_pos;
            bool dot = false;
            while (end < m_src.size()) {
                const QChar d = m_src.at(end);
                if (d.isDigit())
                    ++end;
                else if (d == QLatin1Char('.') && !dot) {
                    dot = true;
                    ++end;
                } else {
                    break;
                }
            }
            bool ok = false;
            const double v = m_src.mid(m_pos, end - m_pos).toDouble(&ok);
            if (!ok) {
                *err = QStringLiteral("无效的数字");
                return 0.0;
            }
            m_pos = end;
            return v;
        }
        *err = QStringLiteral("意外的字符：%1").arg(c);
        return 0.0;
    }

    QString m_src;
    int m_pos = 0;
};

} // namespace

QString CalcSkill::execute(const QStringList &args)
{
    const QString exprText = args.join(QLatin1Char(' ')).trimmed();
    if (exprText.isEmpty())
        return QStringLiteral("用法：/calc <表达式>，例如 /calc (1+2)*3");
    if (exprText.size() > 200)
        return QStringLiteral("错误：表达式过长（上限 200 字符）");

    CalcParser parser(exprText);
    double result = 0.0;
    QString err;
    if (!parser.parse(&result, &err))
        return QStringLiteral("错误：%1").arg(err);

    // 结果展示：整数不带小数位，其余保留最多 10 位有效数字
    if (std::abs(result - std::round(result)) < 1e-9)
        return QString::number(static_cast<qlonglong>(result));
    return QString::number(result, 'g', 10);
}