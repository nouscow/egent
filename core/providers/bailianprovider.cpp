#include "bailianprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>

#include "core/tools/toolregistry.h"

BailianProvider::BailianProvider(QObject *parent)
    : IModelProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_timeoutTimer(new QTimer(this))
{
    // 关闭系统代理自动探测。
    // Windows 上 QNetworkAccessManager 默认会做 WPAD 查询，首个请求常因此多等 2~6 秒，
    // 表现为「首字延迟忽高忽低」。这里直连，需要代理的用户可通过下方环境变量显式指定。
    const QByteArray httpProxy = qgetenv("HTTP_PROXY");
    const QByteArray httpsProxy = qgetenv("HTTPS_PROXY");
    if (httpProxy.isEmpty() && httpsProxy.isEmpty()) {
        m_nam->setProxy(QNetworkProxy::NoProxy);
    }

    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &BailianProvider::onTimeout);
}

QUrl BailianProvider::endpoint()
{
    return QUrl(QStringLiteral(
        "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"));
}

QString BailianProvider::apiKey() const
{
    QByteArray env = qgetenv("DASHSCOPE_API_KEY");
    if (!env.isEmpty())
        return QString::fromUtf8(env);
    QSettings s;
    return s.value(QStringLiteral("dashscope_api_key")).toString();
}

void BailianProvider::send(const QList<Message> &history, const QString &model)
{
    m_parser.reset();
    m_errored = false;
    m_cancelled = false;
    m_timedOut = false;
    m_errorBody.clear();
    m_pendingCalls.clear();
    m_timeoutTimer->stop();

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QJsonArray messages;
    for (const Message &m : history) {
        QJsonObject o;
        if (m.role == Role::Tool) {
            // 工具结果回灌：必须带 tool_call_id，模型靠它对齐是哪次调用
            o.insert(QStringLiteral("role"), QStringLiteral("tool"));
            o.insert(QStringLiteral("tool_call_id"), m.toolCallId);
            o.insert(QStringLiteral("content"), m.content);
        } else {
            o.insert(QStringLiteral("role"), roleToStr(m.role));
            o.insert(QStringLiteral("content"), m.content);
            if (!m.toolCalls.isEmpty())
                o.insert(QStringLiteral("tool_calls"), m.toolCalls);
        }
        messages.append(o);
    }
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);
    QJsonObject opt;
    opt.insert(QStringLiteral("include_usage"), true);
    body.insert(QStringLiteral("stream_options"), opt);
    body.insert(QStringLiteral("enable_thinking"), m_enableThinking);
    body.insert(QStringLiteral("temperature"), m_temperature);
    body.insert(QStringLiteral("max_tokens"), m_maxTokens);

    if (m_enableTools && !ToolRegistry::instance().isEmpty())
        body.insert(QStringLiteral("tools"), ToolRegistry::instance().toolsForModel());

    QNetworkRequest req(endpoint());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QString key = apiKey();
    if (key.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "未找到 DASHSCOPE_API_KEY：请设置环境变量后再试。"));
        return;
    }
    req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    QJsonDocument doc(body);
    m_reply = m_nam->post(req, doc.toJson());
    connect(m_reply, &QNetworkReply::readyRead, this, &BailianProvider::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &BailianProvider::onFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &BailianProvider::onError);

    if (m_timeoutMs > 0)
        m_timeoutTimer->start(m_timeoutMs);
}

void BailianProvider::onReadyRead()
{
    if (!m_reply)
        return;

    const QByteArray data = m_reply->readAll();

    // 服务端拒绝请求时回的是普通 JSON（不是 SSE），先攒起来供错误提示使用
    const int status =
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        m_errorBody.append(data);
        return;
    }

    for (const SSEParser::Event &ev : m_parser.ingest(data)) {
        if (!ev.content.isEmpty())
            emit tokenReceived(ev.content);

        // tool_calls 的 arguments 是跨帧逐字拼的，按 index 累加
        for (const auto &d : ev.toolCalls) {
            PendingToolCall &slot = m_pendingCalls[d.index];
            if (!d.id.isEmpty())
                slot.id = d.id;
            if (!d.name.isEmpty())
                slot.name = d.name;
            slot.arguments += d.arguments;
        }
    }
}

void BailianProvider::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_timeoutTimer->stop();
    m_reply->abort(); // 会触发 errorOccurred(OperationCanceled)，由 onError 吞掉
}

void BailianProvider::onTimeout()
{
    if (!m_reply || m_reply->isFinished())
        return;
    m_timedOut = true;
    m_reply->abort(); // 同样落到 onError，靠 m_timedOut 区分文案
}

void BailianProvider::setOption(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("enable_thinking"))
        m_enableThinking = value.toBool();
    else if (key == QLatin1String("timeout_ms"))
        m_timeoutMs = value.toInt();
    else if (key == QLatin1String("temperature"))
        m_temperature = qBound(0.0, value.toDouble(), 2.0);
    else if (key == QLatin1String("max_tokens"))
        m_maxTokens = qBound(1, value.toInt(), 32768);
    else if (key == QLatin1String("tools_enabled"))
        m_enableTools = value.toBool();
    // 其余 key 静默忽略
}

QString BailianProvider::serverMessage(const QByteArray &body)
{
    if (body.isEmpty())
        return QString();

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(body, &err);
    if (!doc.isObject())
        return QString();

    const QJsonObject root = doc.object();
    const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
    if (!errObj.isEmpty()) {
        const QString msg = errObj.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty())
            return msg;
    }
    // 有些错误结构是 {"message": "..."} 平铺的
    return root.value(QStringLiteral("message")).toString();
}

QString BailianProvider::describeError() const
{
    QNetworkReply *reply = m_reply;

    if (m_timedOut) {
        return QStringLiteral("请求超时：%1 秒内没有任何响应。"
                              "请检查网络，或稍后重试。")
            .arg(m_timeoutMs / 1000);
    }

    const int status =
        reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    const QString detail = serverMessage(m_errorBody);

    switch (status) {
    case 400:
        return detail.isEmpty() ? QStringLiteral("请求参数有误，请检查模型名与参数。")
                                : QStringLiteral("请求参数有误：%1").arg(detail);
    case 401:
        return QStringLiteral("API Key 无效或已过期，请重新设置 DASHSCOPE_API_KEY。");
    case 403:
        return QStringLiteral("当前账号无权访问该模型，请换一个模型试试。");
    case 404:
        return QStringLiteral("模型不存在，请检查模型名称是否正确。");
    case 429:
        return QStringLiteral("调用太频繁被限流了，请等几秒再试。");
    default:
        break;
    }
    if (status >= 500)
        return QStringLiteral("服务端开小差了（HTTP %1），请稍后重试。").arg(status);
    if (status > 0) {
        return detail.isEmpty() ? QStringLiteral("请求失败（HTTP %1）。").arg(status)
                                : detail;
    }

    // 没有状态码 = 还没连上就失败了，属于传输层问题
    if (reply) {
        switch (reply->error()) {
        case QNetworkReply::TimeoutError:
            return QStringLiteral("连接超时，请检查网络后重试。");
        case QNetworkReply::HostNotFoundError:
            return QStringLiteral("找不到服务器，请检查网络连接。");
        case QNetworkReply::SslHandshakeFailedError:
            return QStringLiteral("HTTPS 握手失败，可能是系统时间或证书异常。");
        case QNetworkReply::RemoteHostClosedError:
            return QStringLiteral("连接被服务器断开，请重试。");
        default:
            return reply->errorString();
        }
    }
    return QStringLiteral("网络请求失败。");
}

void BailianProvider::onError(QNetworkReply::NetworkError /*code*/)
{
    if (m_cancelled)
        return; // 用户主动中止，不算错误
    if (m_errored)
        return;
    m_errored = true;
    m_timeoutTimer->stop();
    emit errorOccurred(describeError());
}

void BailianProvider::onFinished()
{
    if (!m_reply)
        return;

    m_timeoutTimer->stop();

    if (m_cancelled) {
        // 取消后仍要通知上层收尾，否则界面会卡在「生成中」状态
        m_reply->deleteLater();
        m_reply = nullptr;
        emit finished();
        return;
    }

    if (m_errored) {
        m_reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    // 模型要求调用工具：本轮到此为止，交给上层执行后再发起下一轮
    if (!m_pendingCalls.isEmpty()) {
        QJsonArray calls;
        for (auto it = m_pendingCalls.cbegin(); it != m_pendingCalls.cend(); ++it) {
            calls.append(QJsonObject{
                {QStringLiteral("id"), it->id},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{{QStringLiteral("name"), it->name},
                             {QStringLiteral("arguments"), it->arguments}}},
            });
        }
        m_pendingCalls.clear();
        m_reply->deleteLater();
        m_reply = nullptr;
        emit toolCallsReady(calls);
        return;
    }

    // 有响应体但没走到 onError 的情况（例如 4xx 但不触发 errorOccurred）
    const int status =
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_reply->error() != QNetworkReply::NoError || status >= 400) {
        emit errorOccurred(describeError());
    } else {
        emit finished();
    }

    m_reply->deleteLater();
    m_reply = nullptr;
}
