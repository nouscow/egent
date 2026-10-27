#ifndef BAILIAN_MODELS_H
#define BAILIAN_MODELS_H

#include <QString>

enum class Role { System, User, Assistant };

struct Message {
    Role role = Role::User;
    QString content;
};

inline QString roleToStr(Role r)
{
    switch (r) {
    case Role::System:    return QStringLiteral("system");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::User:      return QStringLiteral("user");
    }
    return QStringLiteral("user");
}

inline Role roleFromStr(const QString &s)
{
    if (s == QLatin1String("system"))    return Role::System;
    if (s == QLatin1String("assistant")) return Role::Assistant;
    return Role::User;
}

#endif // BAILIAN_MODELS_H
