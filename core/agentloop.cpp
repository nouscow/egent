#include "agentloop.h"

#include "core/tools/toolregistry.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

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
    m_cancelled = false;
    m_currentTool = nullptr;
    m_running = true;
    request();
}

void AgentLoop::cancel()
{
    if (!m_running)
        return;

    m_cancelled = true;
    // 先中止正在跑的工具（可能是一条耗时命令），再停网络请求
    if (m_currentTool)
        m_currentTool->abort();
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

bool AgentLoop::askPermission(const QString &toolName, const QString &detail)
{
    m_permissionGranted = false;

    QEventLoop loop;
    m_permissionLoop = &loop;

    // 必须延迟到事件循环真正跑起来之后再通知界面。
    // 若在这里同步 emit，界面会立刻调用 respondPermission()，
    // 而那时 loop 还没 exec，quit() 会被吞掉 —— 结果是每次都要干等 60 秒超时。
    QTimer::singleShot(0, this, [this, toolName, detail]() {
        emit permissionRequired(toolName, detail);
    });

    // 界面若忘了回复，不能把整个对话卡死
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();

    m_permissionLoop = nullptr;
    return m_permissionGranted;
}

void AgentLoop::respondPermission(bool granted)
{
    m_permissionGranted = granted;
    if (m_permissionLoop)
        m_permissionLoop->quit();
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

        ToolResult result;
        ITool *tool = ToolRegistry::instance().find(name);

        // 需要许可的工具：先问人，再做。用户不答就按拒绝处理。
        if (tool && tool->requiresConfirmation() && !askPermission(name, argsJson)) {
            result = ToolResult::failure(
                QStringLiteral("用户拒绝执行这条命令，请换一种做法或询问用户。"));
            emit toolCallStarted(name, argsJson);
            emit toolCallFinished(name, false, result.content);
            Message denied;
            denied.role = Role::Tool;
            denied.content = result.content;
            denied.toolCallId = callId;
            denied.name = name;
            m_history.append(denied);
            continue;
        }

        emit toolCallStarted(name, argsJson);

        m_currentTool = tool;
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

        m_currentTool = nullptr;

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

    // 用户点了停止：不再继续下一轮，也不再执行剩余的工具调用
    if (m_cancelled) {
        m_running = false;
        emit finished(m_history);
        return;
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
