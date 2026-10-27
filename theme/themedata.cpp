#include "themedata.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QRegularExpression>

namespace {

/** 形如 #rgb / #rrggbb / #aarrggbb / rgb() / rgba() / hsl() / transparent 视为字面色值 */
bool looksLikeLiteralColor(const QString &v)
{
    const QString s = v.trimmed().toLower();
    if (s.isEmpty())
        return false;
    if (s.startsWith(QLatin1Char('#')))
        return true;
    if (s == QLatin1String("transparent"))
        return true;
    static const QRegularExpression fnRe(
        QStringLiteral("^(rgba?|hsla?)\\s*\\("));
    return fnRe.match(s).hasMatch();
}

void readLayer(const QJsonObject &layer, QHash<QString, QString> &out)
{
    for (auto it = layer.constBegin(); it != layer.constEnd(); ++it) {
        const QJsonValue v = it.value();
        if (v.isString())
            out.insert(it.key(), v.toString().trimmed());
    }
}

} // namespace

bool ThemeData::loadFromJson(const QByteArray &json, QString *error)
{
    m_primitive.clear();
    m_semantic.clear();
    m_component.clear();
    m_resolved.clear();
    m_missing.clear();
    m_cyclic.clear();
    m_valid = false;

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 解析失败: %1").arg(perr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();

    m_id = root.value(QStringLiteral("id")).toString();
    m_name = root.value(QStringLiteral("name")).toString();
    m_version = root.value(QStringLiteral("version")).toInt(1);

    if (m_id.isEmpty()) {
        if (error)
            *error = QStringLiteral("主题缺少 id 字段");
        return false;
    }
    if (m_name.isEmpty())
        m_name = m_id;

    readLayer(root.value(QStringLiteral("primitive")).toObject(), m_primitive);
    readLayer(root.value(QStringLiteral("semantic")).toObject(), m_semantic);
    readLayer(root.value(QStringLiteral("component")).toObject(), m_component);

    if (m_semantic.isEmpty() && m_primitive.isEmpty()) {
        if (error)
            *error = QStringLiteral("主题 %1 没有任何令牌").arg(m_id);
        return false;
    }

    m_valid = resolveAll();
    if (!m_valid) {
        if (!m_cyclic.isEmpty()) {
            if (error)
                *error = QStringLiteral("主题 %1 存在循环引用: %2")
                             .arg(m_id, cyclicTokens().join(QStringLiteral(", ")));
            return false;
        }
        // 缺失令牌不阻断加载（可能有可选令牌），由上层打印告警
        if (error)
            *error = QStringLiteral("主题 %1 缺少令牌: %2")
                         .arg(m_id, missingTokens().join(QStringLiteral(", ")));
    }

    return true;
}

bool ThemeData::loadFromFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开 %1").arg(path);
        return false;
    }
    QString err;
    const bool ok = loadFromJson(f.readAll(), &err);
    if (!ok && error)
        *error = QStringLiteral("%1: %2").arg(path, err);
    return ok;
}

QString ThemeData::rawLookup(const QString &name) const
{
    if (m_primitive.contains(name))
        return m_primitive.value(name);
    if (m_semantic.contains(name))
        return m_semantic.value(name);
    if (m_component.contains(name))
        return m_component.value(name);
    return QString();
}

QString ThemeData::resolveOne(const QString &name, QSet<QString> &stack, bool &ok)
{
    if (m_resolved.contains(name))
        return m_resolved.value(name);

    const QString raw = rawLookup(name);
    if (raw.isEmpty()) {
        m_missing.insert(name);
        ok = false;
        return QString();
    }

    if (looksLikeLiteralColor(raw))
        return raw;

    // 引用型：递归展开，递归栈用于检测环
    if (stack.contains(name)) {
        m_cyclic.insert(name);
        ok = false;
        return QString();
    }

    stack.insert(name);
    const QString r = resolveOne(raw, stack, ok);
    stack.remove(name);
    return r;
}

bool ThemeData::resolveAll()
{
    bool allOk = true;

    const auto resolveLayer = [this, &allOk](const QHash<QString, QString> &layer) {
        for (auto it = layer.constBegin(); it != layer.constEnd(); ++it) {
            QSet<QString> stack;
            bool ok = true;
            const QString v = resolveOne(it.key(), stack, ok);
            if (ok && !v.isEmpty())
                m_resolved.insert(it.key(), v);
            else
                allOk = false;
        }
    };

    // 先 primitive，再 semantic，最后 component，保证下层先就绪、命中缓存
    resolveLayer(m_primitive);
    resolveLayer(m_semantic);
    resolveLayer(m_component);

    // 关键令牌必须存在，否则视为主题损坏（QSS 里到处引用它们）
    for (const char *required : {"bg.window", "bg.surface", "text.primary"}) {
        if (!m_resolved.contains(QString::fromLatin1(required))) {
            m_missing.insert(QString::fromLatin1(required));
            allOk = false;
        }
    }
    return allOk;
}

QColor ThemeData::color(const QString &name, const QColor &fallback) const
{
    const QString v = m_resolved.value(name);
    if (v.isEmpty())
        return fallback;
    const QColor c(v);
    return c.isValid() ? c : fallback;
}

QStringList ThemeData::missingTokens() const
{
    QStringList list(m_missing.constBegin(), m_missing.constEnd());
    list.sort();
    return list;
}

QStringList ThemeData::cyclicTokens() const
{
    QStringList list(m_cyclic.constBegin(), m_cyclic.constEnd());
    list.sort();
    return list;
}
