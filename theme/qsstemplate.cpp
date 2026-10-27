#include "qsstemplate.h"

#include <QSet>

namespace QssTemplate {

static bool isAsciiName(const QString &name)
{
    if (name.isEmpty())
        return false;
    for (const QChar ch : name) {
        const ushort u = ch.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                     || (u >= '0' && u <= '9') || u == '_' || u == '.' || u == '-';
        if (!ok)
            return false;
    }
    return true;
}

RenderResult render(const QString &tpl, const Lookup &lookup)
{
    // 修复: 完全绕开 QRegularExpression/PCRE2。MinGW + Qt 6.11.0 环境下,
    // globalMatch 在处理含中文 {{令牌}} 注释或大 QSS 后存在挂起(死循环)问题。
    // 占位符语法简单, 用 QString::indexOf 手工解析, 行为与 ASCII 正则等价。
    RenderResult result;
    QSet<QString> unknown;

    QString out;
    out.reserve(tpl.size());

    int pos = 0;
    while (true) {
        const int start = tpl.indexOf(QLatin1String("{{"), pos);
        if (start < 0) {
            out += tpl.mid(pos);
            break;
        }
        // 找结束符
        const int nameBegin = start + 2;
        const int end = tpl.indexOf(QLatin1String("}}"), nameBegin);
        if (end < 0) {
            // 未闭合: 原样保留并终止
            out += tpl.mid(pos);
            break;
        }
        const QString name = tpl.mid(nameBegin, end - nameBegin).trimmed();
        if (!isAsciiName(name)) {
            // 非法令牌(如中文示例 {{令牌}}): 不替换，原样搬走 "{{" 后继续向后找
            out += tpl.mid(pos, start - pos);   // 到 "{{" 之前为止
            out += QLatin1String("{{");
            pos = nameBegin;
            continue;
        }
        const QString value = lookup ? lookup(name) : QString();
        // 注意：nameBegin 已经越过 "{{"，这里只能取到 start，否则会把 "{{" 也输出
        out += tpl.mid(pos, start - pos);
        if (value.isEmpty()) {
            out += QLatin1String("{{") + name + QLatin1String("}}");
            unknown.insert(name);
        } else {
            out += value;
        }
        pos = end + 2;
    }

    result.qss = out;

    // 兜底残留检测
    if (out.indexOf(QLatin1String("{{")) >= 0)
        unknown.insert(QStringLiteral("(未识别占位符)"));

    result.unknownTokens = QStringList(unknown.constBegin(), unknown.constEnd());
    result.unknownTokens.sort();
    return result;
}

} // namespace QssTemplate
