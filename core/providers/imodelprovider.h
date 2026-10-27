#ifndef BAILIAN_IMODELPROVIDER_H
#define BAILIAN_IMODELPROVIDER_H

#include <QJsonArray>
#include <QObject>
#include <QVariant>
#include "core/models.h"

/**
 * 厂商无关的模型 provider 接口（见企划书 7.2）。
 * 所有 AI 厂商（百炼 / OpenAI / DeepSeek …）都实现这一接口，
 * UI 与插件只依赖 IModelProvider，不感知具体厂商与端点。
 */
class IModelProvider : public QObject
{
    Q_OBJECT
public:
    explicit IModelProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IModelProvider() = default;

    /** 厂商 / provider 唯一 id，如 "bailian" / "openai" */
    virtual QString id() const = 0;

    /** 发送一次对话（含历史），结果通过信号流式返回 */
    virtual void send(const QList<Message> &history, const QString &model) = 0;

    /**
     * 中止当前请求（可选实现）。
     * 约定：中止后仍要发 finished()，让上层能正常收尾；
     * 不要通过 errorOccurred() 报告「用户主动取消」。
     */
    virtual void cancel() {}

    /**
     * 生成图片（文生图）。默认实现直接报错，由支持生图的厂商覆写。
     * 结果通过 imageReady / imageError 信号返回（非流式）。
     * @param prompt       用户提示词
     * @param workspaceId  百炼 MaaS 推理工作空间 ID（生图端点需要）
     */
    virtual void generateImage(const QString &prompt, const QString &workspaceId)
    {
        Q_UNUSED(prompt)
        Q_UNUSED(workspaceId)
        emit imageError(QStringLiteral("当前厂商不支持生成图片。"));
    }

    /**
     * 厂商无关的调参入口（可选实现）。
     * 约定 key 用小写蛇形，如 "enable_thinking" / "temperature"；
     * 不认识的 key 必须静默忽略，不能报错——各厂商能力本来就不一样。
     */
    virtual void setOption(const QString &key, const QVariant &value)
    {
        Q_UNUSED(key)
        Q_UNUSED(value)
    }

signals:
    void tokenReceived(const QString &delta);

    /**
     * 模型决定调用工具（本轮流式到此为止）。
     * toolCalls 是 OpenAI 的 tool_calls 数组，调用方执行后把结果以
     * role=Tool 的消息追加进历史，再发起下一轮。
     * 发出本信号后**不会**再发 finished()。
     */
    void toolCallsReady(const QJsonArray &toolCalls);

    /** 命中限流，正在按退避策略重试（attempt 从 1 开始） */
    void retrying(int attempt, int delayMs);
    /** 图片生成成功（非流式，整张一次返回）。imageData 为图片原始字节（PNG/JPG） */
    void imageReady(const QByteArray &imageData, const QString &prompt);
    /** 图片生成失败 */
    void imageError(const QString &message);
    void finished();
    void errorOccurred(const QString &message);
};

#define IModelProvider_IID "com.egent.IModelProvider/1.0"
Q_DECLARE_INTERFACE(IModelProvider, IModelProvider_IID)

#endif // BAILIAN_IMODELPROVIDER_H
