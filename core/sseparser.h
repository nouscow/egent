#ifndef BAILIAN_SSEPARSER_H
#define BAILIAN_SSEPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

/**
 * SSE 流式解析器（OpenAI 兼容格式）。
 *
 * 相比初版只吐文本，这里要能识别 tool_calls —— 关键差异在于
 * **工具调用的 arguments 是跨多个分片逐字拼出来的**：
 *
 *   data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"pa"}}]}}]}
 *   data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"th\":\"RE"}}]}}]}
 *
 * 所以解析器只负责「把每帧拆成增量」，**拼接由调用方按 index 做**。
 * 解析器除未凑满一帧的字节缓冲外不持有状态。
 *
 * 另：只在完整帧上做 fromUtf8，保证 UTF-8 多字节字符不会被腰斩。
 */
class SSEParser
{
public:
    struct ToolCallDelta
    {
        int index = -1;
        QString id;        // 通常只在首个分片出现
        QString name;      // 同上
        QString arguments; // 分片，调用方需要累加
    };

    struct Event
    {
        QString content;                // 文本增量（可能为空）
        QList<ToolCallDelta> toolCalls; // 工具调用增量
        QString finishReason;           // "stop" / "tool_calls" / "length"
        bool done = false;              // 收到 [DONE]
    };

    /** 喂入新收到的字节，返回本轮从完整帧中解析出的事件 */
    QList<Event> ingest(const QByteArray &chunk);

    bool isDone() const { return m_done; }
    void reset();

private:
    QByteArray m_buffer;
    bool m_done = false;
};

#endif // BAILIAN_SSEPARSER_H
