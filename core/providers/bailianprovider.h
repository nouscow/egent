#ifndef BAILIAN_BAILIANPROVIDER_H
#define BAILIAN_BAILIANPROVIDER_H

#include "core/providers/imodelprovider.h"
#include "core/sseparser.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QTimer;

/**
 * 百炼 provider：实现 IModelProvider，调用百炼 OpenAI 兼容端点，
 * chat 走 SSE 流式（见企划书 8 / 8.1）。
 */
class BailianProvider : public IModelProvider
{
    Q_OBJECT
public:
    explicit BailianProvider(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("bailian"); }
    void send(const QList<Message> &history, const QString &model) override;
    void cancel() override;
    void setOption(const QString &key, const QVariant &value) override;

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError code);
    void onTimeout();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeoutTimer = nullptr;

    SSEParser m_parser;
    bool m_errored = false;
    bool m_cancelled = false;
    bool m_timedOut = false;

    // 错误响应的原始 body（HTTP >= 400 时 readyRead 收下的数据），
    // 用来从中提取服务端给的 error.message
    QByteArray m_errorBody;

    // 深度思考默认关闭：实测同一问题 qwen3.8-flash
    //   开思考首字 7.46s / 关思考首字 0.61s，差 12 倍。
    bool m_enableThinking = false;

    // 整条请求的上限时间（毫秒）。流式请求收到首包后不再重新计时，
    // 因为长回答本身可能需要很久；这里防的是「一个字节都不来」。
    int m_timeoutMs = 60000;

    // 采样参数（可通过 setOption 调整，见设置面板）
    double m_temperature = 0.7;
    int m_maxTokens = 4096;

    // ---- Agent 工具调用 ----
    bool m_enableTools = false;
    struct PendingToolCall
    {
        QString id;
        QString name;
        QString arguments; // 跨分片累加
    };
    QMap<int, PendingToolCall> m_pendingCalls;

    QString apiKey() const;
    static QUrl endpoint();

    /** 把 HTTP 状态码 / 传输层错误翻译成用户能看懂的句子 */
    QString describeError() const;
    /** 从响应体里尽力抠出服务端给的错误说明 */
    static QString serverMessage(const QByteArray &body);
};

#endif // BAILIAN_BAILIANPROVIDER_H
