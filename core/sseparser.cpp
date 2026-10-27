#include "sseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void SSEParser::reset()
{
    m_buffer.clear();
    m_done = false;
}

QStringList SSEParser::ingest(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    m_buffer.replace("\r\n", "\n");   // 归一换行，统一按 "\n\n" 分帧

    QStringList deltas;
    int idx;
    while ((idx = m_buffer.indexOf("\n\n")) >= 0) {
        QByteArray event = m_buffer.left(idx);
        m_buffer.remove(0, idx + 2);

        // 收集该事件内所有 "data:" 行（SSE 允许多行 data 拼接）
        QStringList lines = QString::fromUtf8(event).split(QLatin1Char('\n'));
        QString data;
        for (QString line : lines) {
            line = line.trimmed();
            if (line.startsWith(QStringLiteral("data:")))
                data += line.mid(5).trimmed();
        }
        if (data.isEmpty())
            continue;
        if (data == QStringLiteral("[DONE]")) {
            m_done = true;
            continue;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &err);
        if (!doc.isObject())
            continue;
        QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("choices")))
            continue;
        QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;
        QJsonObject ch = choices.first().toObject();
        if (!ch.contains(QStringLiteral("delta")))
            continue;
        QJsonObject delta = ch.value(QStringLiteral("delta")).toObject();
        if (delta.contains(QStringLiteral("content")))
            deltas.append(delta.value(QStringLiteral("content")).toString());
    }
    return deltas;
}
