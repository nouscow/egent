#ifndef BAILIAN_SSEPARSER_H
#define BAILIAN_SSEPARSER_H

#include <QByteArray>
#include <QStringList>

/**
 * SSE 流式解析器（见企划书第八章「解析要点」）。
 * 维护累积 buffer，按 "\n\n" 分帧，提取 chat 响应的 choices[].delta.content。
 * 调用方必须按字节流喂入，本类保证 UTF-8 多字节不被截断（只在完整事件上解析）。
 */
class SSEParser
{
public:
    /** 喂入新收到的字节，返回本次从完整事件中解析出的内容增量列表 */
    QStringList ingest(const QByteArray &chunk);
    bool isDone() const { return m_done; }
    void reset();

private:
    QByteArray m_buffer;
    bool m_done = false;
};

#endif // BAILIAN_SSEPARSER_H
