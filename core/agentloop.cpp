#include "agentloop.h"

#include "core/tools/toolregistry.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

AgentLoop::AgentLoop(IModelProvider *provider, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
{
    if (!m_provider)
        return;

    connect(m_provider, &IModelProvider::tokenReceived,
            this, &AgentLoop::onToken);
    connect(m_provider, &IModelProvider::toolCallsReady,
            this, &AgentLoop::onToolCalls);
    connect(m_provider, &IModelProvider::finished,
            this, &AgentLoop::onProviderFinished);
    connect(m_provider, &IModelProvider::errorOccurred,
            this, &AgentLoop::onProviderError);
}

void AgentLoop::start(const QList<Message> &history)
{
    m_history = history;
    m_round = 0;
    m_accumulated.clear();
    m_running = true;
    request();
}

void AgentLoop::cancel()
{
    if (!m_running)
        return;
    if (m_provider)
        m_provider->cancel();
}

void AgentLoop::request()
{
    if (!m_provider) {
        m_running = false;
        emit errorOccurred(QStringLiteral("没有可用的模型接口"));
        return;
    }
    m_accumulated.clear();
    m_provider->send(m_history, m_model);
}

void AgentLoop::onToken(const QString &delta)
{
    m_accumulated += delta;
    emit contentDelta(delta);
}

void AgentLoop::onProviderFinished()
{
    if (!m_running)
        return;

    // 模型没有要求工具 → 这就是最终回答
    m_history.append({Role::Assistant, m_accumulated});
    m_running = false;
    emit finished(m_history);
}

void AgentLoop::onProviderError(const QString &message)
{
    if (!m_running)
        return;
    m_running = false;
    emit errorOccurred(message);
    emit finished(m_history);
}

void AgentLoop::onToolCalls(const QJsonArray &calls)
{
    if (!m_running)
        return;

    // 1) 先记下「模型这次说了什么 + 要调哪些工具」
    Message assistantMsg;
    assistantMsg.role = Role::Assistant;
    assistantMsg.content = m_accumulated;
    assistantMsg.toolCalls = calls;
    m_history.append(assistantMsg);

    // 2) 逐个执行，结果原样追加为 tool 消息
    for (const QJsonValue &value : calls) {
        const QJsonObject call = value.toObject();
        const QString callId = call.value(QStringLiteral("id")).toString();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        const QString argsJson = fn.value(QStringLiteral("arguments")).toString();

        emit toolCallStarted(name, argsJson);

        ToolResult result;
        ITool *tool = ToolRegistry::instance().find(name);
        if (!tool) {
            result = ToolResult::failure(
                QStringLiteral("没有名为 %1 的工具").arg(name));
        } else {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(argsJson.toUtf8(), &err);
            if (!doc.isObject()) {
                result = ToolResult::failure(
                    QStringLiteral("参数不是合法 JSON：%1").arg(err.errorString()));
            } else {
                result = tool->execute(doc.object());
            }
        }

        emit toolCallFinished(name, result.ok, result.content);
        qInfo().noquote() << QStringLiteral("[agent] %1 -> %2")
                                 .arg(name, result.ok ? QStringLiteral("ok")
                                                      : QStringLiteral("fail"));

        Message toolMsg;
        toolMsg.role = Role::Tool;
        toolMsg.content = result.content;
        toolMsg.toolCallId = callId;
        toolMsg.name = name;
        m_history.append(toolMsg);
    }

    // 3) 轮次上限：防模型反复读同一个文件停不下来
    ++m_round;
    if (m_round >= m_maxRounds) {
        m_running = false;
        const QString msg =
            QStringLiteral("连续调用了 %1 轮工具仍未给出回答，已中止。").arg(m_round);
        emit errorOccurred(msg);
        emit finished(m_history);
        return;
    }

    // 4) 带着工具结果继续问
    request();
}
