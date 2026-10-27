#pragma once

#include "core/models.h"
#include "core/providers/imodelprovider.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>

/**
 * Agent 执行循环。
 *
 * 普通对话是「一问一答」；开了工具之后变成：
 *
 *   用户提问 → 模型可能要求调工具 → 本地执行 → 结果回灌 → 模型继续
 *            → 直到模型不再要求工具，输出最终回答
 *
 * 这个类负责中间那段循环，界面只管把信号画出来。
 *
 * 安全：单轮最多 m_maxRounds 次工具调用，超限立即中止并报错，
 * 防止模型陷入「反复读同一个文件」的死循环。
 */
class AgentLoop : public QObject
{
    Q_OBJECT

public:
    explicit AgentLoop(IModelProvider *provider, QObject *parent = nullptr);

    void setModel(const QString &model) { m_model = model; }
    void setMaxRounds(int rounds) { m_maxRounds = qMax(1, rounds); }

    /** 开始一轮完整对话；history 里应当已包含本次用户提问 */
    void start(const QList<Message> &history);
    void cancel();

    bool isRunning() const { return m_running; }

signals:
    /** 文本增量，界面用来逐字追加气泡 */
    void contentDelta(const QString &delta);
    /** 即将执行某个工具（argsJson 是模型给的原始 JSON 字符串） */
    void toolCallStarted(const QString &name, const QString &argsJson);
    /** 工具执行完毕 */
    void toolCallFinished(const QString &name, bool ok, const QString &result);
    /** 整个循环结束（含最终回答） */
    void finished(const QList<Message> &finalHistory);
    void errorOccurred(const QString &message);

private slots:
    void onToken(const QString &delta);
    void onToolCalls(const QJsonArray &calls);
    void onProviderFinished();
    void onProviderError(const QString &message);

private:
    void request();

    IModelProvider *m_provider = nullptr;
    QList<Message> m_history;
    QString m_model = QStringLiteral("qwen3.8-flash");
    QString m_accumulated; // 本轮已收到的文本
    int m_maxRounds = 8;
    int m_round = 0;
    bool m_running = false;
};
