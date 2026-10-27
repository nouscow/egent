#ifndef EGENT_OPENAICOMPATPROVIDER_H
#define EGENT_OPENAICOMPATPROVIDER_H

#include "core/providers/imodelprovider.h"
#include "core/sseparser.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QTimer;

/**
 * OpenAI 兼容协议的通用实现。
 *
 * 百炼、DeepSeek、OpenAI 三家的 chat 端点格式一致（都是 OpenAI 兼容），
 * 差异只有「端点 + Key 来源 + 模型列表」，因此不必各写一份 provider，
 * 用配置区分即可。新增厂商 = 加一份 Config，不动调用方（企划书决策 5）。
 */
class OpenAiCompatProvider : public IModelProvider
{
    Q_OBJECT
public:
    struct Config
    {
        QString id;            // "bailian" / "deepseek" / "openai"
        QString displayName;   // 界面显示名
        QUrl endpoint;
        QString envVar;        // 环境变量名，优先级最高
        QString settingsKey;   // QSettings 键名
        QStringList models;    // 该厂商的模型列表
    };

    explicit OpenAiCompatProvider(const Config &config, QObject *parent = nullptr);

    const Config &config() const { return m_config; }
    QStringList models() const { return m_config.models; }

    QString id() const override { return m_config.id; }
    void send(const QList<Message> &history, const QString &model) override;
    void cancel() override;
    void setOption(const QString &key, const QVariant &value) override;
    void generateImage(const QString &prompt, const QString &workspaceId) override;

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError code);
    void onTimeout();
    void onImageReplyFinished();
    void onImageDownloadFinished();

private:
    Config m_config;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeoutTimer = nullptr;

    SSEParser m_parser;
    bool m_errored = false;

    // ---- 图片生成（百炼 MaaS，非 SSE）----
    QNetworkReply *m_imgReply = nullptr;   // 生图请求
    QNetworkReply *m_imgDl = nullptr;      // 图片下载请求
    QString m_imgPrompt;                   // 本次生图的提示词，随结果一起回传
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

    // ---- 限流退避重试 ----
    static constexpr int kMaxRetries = 3;
    QByteArray m_lastBody;   // 重试时原样重发
    int m_retryCount = 0;

    void postRaw(const QByteArray &body);
    /** 命中 429 时安排重试；返回 true 表示已在重试流程中，不要报错 */
    bool handleRateLimit();

    // ---- Agent 工具调用 ----
    bool m_enableTools = false;
    struct PendingToolCall
    {
        QString id;
        QString name;
        QString arguments; // 跨分片累加
    };
    QMap<int, PendingToolCall> m_pendingCalls;

    QString apiKey() const { return apiKeyFor(m_config); }

public:
    /** 读取指定厂商的 Key：环境变量优先，其次 QSettings */
    static QString apiKeyFor(const Config &config);

    /** 把 HTTP 状态码 / 传输层错误翻译成用户能看懂的句子 */
    QString describeError() const;
    /** 从响应体里尽力抠出服务端给的错误说明 */
    static QString serverMessage(const QByteArray &body);
};

#endif // EGENT_OPENAICOMPATPROVIDER_H
