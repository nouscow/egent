#include "sessionstore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {
constexpr auto kConnection = "egent_main";

/** 幂等补列：老库缺 image_* 列时 ALTER TABLE 增量迁移，避免破坏旧数据 */
bool ensureColumn(QSqlDatabase &db, const QString &table, const QString &col,
                  const QString &ddl)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (q.next()) {
        if (q.value(1).toString() == col)
            return true; // 已有
    }
    QSqlQuery alt(db);
    return alt.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, ddl));
}
}

SessionStore::~SessionStore()
{
    close();
}

QString SessionStore::defaultDbPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.egent");
    return dir + QStringLiteral("/egent.db");
}

bool SessionStore::open(const QString &path)
{
    if (m_open)
        close();

    m_path = path.isEmpty() ? defaultDbPath() : path;

    const QFileInfo fi(m_path);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath())) {
        m_lastError = QStringLiteral("无法创建目录: %1").arg(fi.absolutePath());
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("SQLite 驱动不可用（缺少 qsqlite 插件）");
        qWarning().noquote() << "[db]" << m_lastError;
        return false;
    }

    {
        QSqlDatabase db = QSqlDatabase::contains(QLatin1String(kConnection))
                              ? QSqlDatabase::database(QLatin1String(kConnection))
                              : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          QLatin1String(kConnection));
        db.setDatabaseName(m_path);
        if (!db.open()) {
            m_lastError = db.lastError().text();
            qWarning().noquote() << "[db] 打开失败:" << m_lastError;
            return false;
        }
    }

    m_open = true;
    if (!ensureSchema()) {
        close();
        return false;
    }
    return true;
}

void SessionStore::close()
{
    if (QSqlDatabase::contains(QLatin1String(kConnection))) {
        {
            QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection), false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(QLatin1String(kConnection));
    }
    m_open = false;
}

bool SessionStore::isOpen() const
{
    return m_open;
}

bool SessionStore::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);

    const QStringList ddl = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS session ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  title TEXT NOT NULL,"
                       "  created_at INTEGER NOT NULL,"
                       "  updated_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS message ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  session_id INTEGER NOT NULL,"
                       "  role TEXT NOT NULL,"
                       "  content TEXT NOT NULL,"
                       "  image_path TEXT NOT NULL DEFAULT '',"
                       "  image_mime TEXT NOT NULL DEFAULT '',"
                       "  created_at INTEGER NOT NULL,"
                       "  FOREIGN KEY(session_id) REFERENCES session(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_message_session "
                       "ON message(session_id, id)"),
    };

    for (const QString &sql : ddl) {
        if (!q.exec(sql)) {
            // PRAGMA 失败不致命，建表失败才致命
            if (sql.startsWith(QStringLiteral("CREATE"))) {
                m_lastError = q.lastError().text();
                qWarning().noquote() << "[db] 建表失败:" << m_lastError;
                return false;
            }
        }
    }

    // 老库迁移：为已有 message 表补图片附件列（幂等）
    if (!ensureColumn(db, QStringLiteral("message"), QStringLiteral("image_path"),
                      QStringLiteral("image_path TEXT NOT NULL DEFAULT ''"))
        || !ensureColumn(db, QStringLiteral("message"), QStringLiteral("image_mime"),
                         QStringLiteral("image_mime TEXT NOT NULL DEFAULT ''"))) {
        m_lastError = q.lastError().text();
        qWarning().noquote() << "[db] 图片列迁移失败:" << m_lastError;
        return false;
    }
    return true;
}

int SessionStore::createSession(const QString &title)
{
    if (!m_open)
        return -1;

    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    q.prepare(QStringLiteral(
        "INSERT INTO session(title, created_at, updated_at) VALUES(?, ?, ?)"));
    q.addBindValue(title.isEmpty() ? QStringLiteral("新会话") : title);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

QList<SessionStore::SessionInfo> SessionStore::sessions() const
{
    QList<SessionInfo> out;
    if (!m_open)
        return out;

    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT s.id, s.title, s.updated_at, COUNT(m.id) "
        "FROM session s LEFT JOIN message m ON m.session_id = s.id "
        "GROUP BY s.id ORDER BY s.updated_at DESC"));

    while (q.next()) {
        SessionInfo info;
        info.id = q.value(0).toInt();
        info.title = q.value(1).toString();
        info.updatedAt = q.value(2).toLongLong();
        info.messageCount = q.value(3).toInt();
        out.append(info);
    }
    return out;
}

bool SessionStore::renameSession(int id, const QString &title)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE session SET title = ? WHERE id = ?"));
    q.addBindValue(title);
    q.addBindValue(id);
    return q.exec();
}

bool SessionStore::deleteSession(int id)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));

    // 外键级联依赖 PRAGMA，这里显式删一遍更稳
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM message WHERE session_id = ?"));
    del.addBindValue(id);
    del.exec();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM session WHERE id = ?"));
    q.addBindValue(id);
    const bool ok = q.exec();
    if (!ok)
        m_lastError = q.lastError().text();
    return ok;
}

bool SessionStore::touchSession(int id)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE session SET updated_at = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);
    return q.exec();
}

QList<Message> SessionStore::messages(int sessionId) const
{
    QList<Message> out;
    if (!m_open)
        return out;

    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT role, content, image_path, image_mime FROM message WHERE session_id = ? ORDER BY id ASC"));
    q.addBindValue(sessionId);
    if (!q.exec())
        return out;

    while (q.next()) {
        Message m;
        m.role = roleFromStr(q.value(0).toString());
        m.content = q.value(1).toString();
        m.imagePath = q.value(2).toString();
        m.imageMime = q.value(3).toString();
        out.append(m);
    }
    return out;
}

bool SessionStore::appendMessage(int sessionId, Role role, const QString &content,
                                 const QString &imagePath, const QString &imageMime)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO message(session_id, role, content, image_path, image_mime, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(roleToStr(role));
    q.addBindValue(content);
    // 默认参数为 null QString，直接绑定会被 QtSql 当成 NULL 而违反 NOT NULL，
    // 统一转为显式空串再绑定
    q.addBindValue(imagePath.isNull() ? QStringLiteral("") : imagePath);
    q.addBindValue(imageMime.isNull() ? QStringLiteral("") : imageMime);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    const bool ok = q.exec();
    if (!ok)
        m_lastError = q.lastError().text();
    else
        touchSession(sessionId);
    return ok;
}

bool SessionStore::updateLastAssistant(int sessionId, const QString &content)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE message SET content = ? WHERE id = ("
        "  SELECT id FROM message WHERE session_id = ? AND role = 'assistant' "
        "  ORDER BY id DESC LIMIT 1)"));
    q.addBindValue(content);
    q.addBindValue(sessionId);
    const bool ok = q.exec();
    if (ok)
        touchSession(sessionId);
    return ok;
}

bool SessionStore::dropLastMessage(int sessionId)
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM message WHERE id = ("
        "  SELECT id FROM message WHERE session_id = ? ORDER BY id DESC LIMIT 1)"));
    q.addBindValue(sessionId);
    return q.exec();
}

bool SessionStore::truncateAfter(int sessionId, int keepCount)
{
    if (!m_open)
        return false;
    if (keepCount < 0)
        keepCount = 0;

    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM message WHERE session_id = ? AND id NOT IN ("
        "  SELECT id FROM message WHERE session_id = ? ORDER BY id LIMIT ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(sessionId);
    q.addBindValue(keepCount);
    const bool ok = q.exec();
    if (ok)
        touchSession(sessionId);
    return ok;
}

bool SessionStore::updateMessageAt(int sessionId, int index, const QString &content)
{
    if (!m_open || index < 0)
        return false;

    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE message SET content = ? WHERE id = ("
        "  SELECT id FROM message WHERE session_id = ? ORDER BY id LIMIT 1 OFFSET ?)"));
    q.addBindValue(content);
    q.addBindValue(sessionId);
    q.addBindValue(index);
    const bool ok = q.exec();
    if (ok)
        touchSession(sessionId);
    return ok;
}
