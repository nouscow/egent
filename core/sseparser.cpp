#include "sseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

void SSEParser::reset()
{
    m_buffer.clear();
    m_done = false;
}

QList<SSEParser::Event> SSEParser::ingest(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    m_buffer.replace("\r\n", "\n"); // 归一换行，统一按 "\n\n" 分帧

    QList<Event> events;
    int idx;
    while ((idx = m_buffer.indexOf("\n\n")) >= 0) {
        const QByteArray frame = m_buffer.left(idx);
        m_buffer.remove(0, idx + 2);

        // 一帧里可能有多行 data:（SSE 规范允许），先拼起来
        QString data;
        const QStringList lines = QString::fromUtf8(frame).split(QLatin1Char('\n'));
        for (QString line : lines) {
            line = line.trimmed();
            if (line.startsWith(QStringLiteral("data:")))
                data += line.mid(5).trimmed();
        }
        if (data.isEmpty())
            continue;

        Event ev;
        if (data == QStringLiteral("[DONE]")) {
            m_done = true;
            ev.done = true;
            events.append(ev);
            continue;
        }

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &err);
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            // 只带 usage 的收尾帧，无 choices —— 忽略即可
            continue;
        }

        const QJsonObject choice = choices.first().toObject();
        ev.finishReason = choice.value(QStringLiteral("finish_reason")).toString();

        const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
        if (delta.contains(QStringLiteral("content")))
            ev.content = delta.value(QStringLiteral("content")).toString();

        const QJsonArray toolCalls = delta.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &v : toolCalls) {
            const QJsonObject tc = v.toObject();
            ToolCallDelta d;
            d.index = tc.value(QStringLiteral("index")).toInt(0);
            d.id = tc.value(QStringLiteral("id")).toString();
            const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
            d.name = fn.value(QStringLiteral("name")).toString();
            d.arguments = fn.value(QStringLiteral("arguments")).toString();
            ev.toolCalls.append(d);
        }

        if (!ev.content.isEmpty() || !ev.toolCalls.isEmpty()
            || !ev.finishReason.isEmpty())
            events.append(ev);
    }
    return events;
}
