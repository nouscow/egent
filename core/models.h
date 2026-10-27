#ifndef BAILIAN_MODELS_H
#define BAILIAN_MODELS_H

#include <QJsonArray>
#include <QString>

enum class Role { System, User, Assistant, Tool };

struct Message
{
    Role role = Role::User;
    QString content;

    // ---- 图片附件（视觉问答 / 生图入会话共用）----
    /** 本地图片路径；无图片时为空串 */
    QString imagePath;
    /** 图片 MIME，如 image/png；用于请求体与气泡渲染 */
    QString imageMime;

    // ---- 以下三个字段只在 Agent 工具调用里用到，普通对话留空 ----
    /** role == Assistant 且模型决定调用工具时：OpenAI 的 tool_calls 数组 */
    QJsonArray toolCalls;
    /** role == Tool 时：回复的是哪一次调用（对应 tool_call 的 id） */
    QString toolCallId;
    /** role == Tool 时：工具名，便于日志与调试 */
    QString name;
};

inline QString roleToStr(Role r)
{
    switch (r) {
    case Role::System:    return QStringLiteral("system");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::User:      return QStringLiteral("user");
    case Role::Tool:      return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

inline Role roleFromStr(const QString &s)
{
    if (s == QLatin1String("system"))    return Role::System;
    if (s == QLatin1String("assistant")) return Role::Assistant;
    if (s == QLatin1String("tool"))      return Role::Tool;
    return Role::User;
}

#endif // BAILIAN_MODELS_H
