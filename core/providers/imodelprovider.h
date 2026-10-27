#ifndef BAILIAN_IMODELPROVIDER_H
#define BAILIAN_IMODELPROVIDER_H

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
    void finished();
    void errorOccurred(const QString &message);
};

#define IModelProvider_IID "com.egent.IModelProvider/1.0"
Q_DECLARE_INTERFACE(IModelProvider, IModelProvider_IID)

#endif // BAILIAN_IMODELPROVIDER_H
