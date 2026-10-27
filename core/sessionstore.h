#pragma once

#include "core/models.h"

#include <QList>
#include <QString>

/**
 * 会话存储（SQLite）。
 *
 * 表结构：
 *   session(id, title, created_at, updated_at)
 *   message(id, session_id, role, content, created_at)
 *
 * 设计取舍：
 *  - 流式过程中不逐字写库（每 50ms 一次太浪费），改为「回答结束时整条落库」。
 *    代价是进程被强杀会丢当前这条回答，但换来的写放大几乎为零。
 *  - 会话标题取首条用户消息的前若干字，之后可手动重命名。
 */
class SessionStore
{
public:
    struct SessionInfo
    {
        int id = -1;
        QString title;
        qint64 updatedAt = 0;
        int messageCount = 0;
    };

    SessionStore() = default;
    /** 析构时务必释放连接，否则 Windows 上 SQLite 文件会被锁住删不掉 */
    ~SessionStore();
    Q_DISABLE_COPY(SessionStore)

    /** 打开数据库；path 为空时用 defaultDbPath()。首次打开自动建表。 */
    bool open(const QString &path = QString());
    void close();
    bool isOpen() const;

    QString lastError() const { return m_lastError; }
    QString databasePath() const { return m_path; }

    int createSession(const QString &title);
    QList<SessionInfo> sessions() const;
    bool renameSession(int id, const QString &title);
    bool deleteSession(int id);
    bool touchSession(int id);

    QList<Message> messages(int sessionId) const;
    bool appendMessage(int sessionId, Role role, const QString &content);
    /** 用完整文本覆盖最后一条 assistant 消息（流式结束后调用） */
    bool updateLastAssistant(int sessionId, const QString &content);
    /** 删除会话里最后一条消息（重新生成时用） */
    bool dropLastMessage(int sessionId);

    static QString defaultDbPath();

private:
    bool ensureSchema();

    QString m_path;
    QString m_lastError;
    bool m_open = false;
};
