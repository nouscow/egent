#pragma once

#include "core/models.h"
#include "core/providers/imodelprovider.h"
#include "core/tools/itool.h"

#include <QEventLoop>
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

    /**
     * 界面在收到 permissionRequired 后调用，回复是否允许执行。
     * 不回复的话，60 秒后按「拒绝」处理，避免整个对话卡死。
     */
    void respondPermission(bool granted);

    bool isRunning() const { return m_running; }

signals:
    /** 文本增量，界面用来逐字追加气泡 */
    void contentDelta(const QString &delta);
    /** 即将执行某个工具（argsJson 是模型给的原始 JSON 字符串） */
    void toolCallStarted(const QString &name, const QString &argsJson);
    /** 工具执行完毕 */
    void toolCallFinished(const QString &name, bool ok, const QString &result);
    /**
     * 某个工具需要用户许可才能执行。
     * 界面必须调用 respondPermission() 回复，否则会阻塞到超时。
     */
    void permissionRequired(const QString &toolName, const QString &detail);
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
    /** 阻塞等待用户许可；超时按拒绝处理 */
    bool askPermission(const QString &toolName, const QString &detail);

    IModelProvider *m_provider = nullptr;
    QList<Message> m_history;
    QString m_model = QStringLiteral("qwen3.8-flash");
    QString m_accumulated; // 本轮已收到的文本
    int m_maxRounds = 8;
    int m_round = 0;
    bool m_running = false;

    // 权限等待：用嵌套事件循环阻塞工具执行，直到界面给出答复
    QEventLoop *m_permissionLoop = nullptr;
    bool m_permissionGranted = false;

    // 正在执行的工具（取消时要把中止转给它，否则耗时的命令停不下来）
    ITool *m_currentTool = nullptr;
    bool m_cancelled = false;
};
