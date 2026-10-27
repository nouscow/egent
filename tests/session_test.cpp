// SessionStore 自测：不依赖 GUI，用临时目录里的真实 SQLite 文件跑。
// 退出码 0 = 全部通过。

#include "core/sessionstore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char *what, const QString &detail = QString())
{
    if (cond) {
        ++g_pass;
        qInfo().noquote() << "  PASS " << what;
    } else {
        ++g_fail;
        qWarning().noquote() << "  FAIL " << what
                             << (detail.isEmpty() ? QString() : ("  -> " + detail));
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        qWarning() << "临时目录创建失败";
        return 1;
    }
    const QString dbPath = tmp.path() + QStringLiteral("/test.db");

    SessionStore store;

    qInfo().noquote() << "\n[1] 打开与建表";
    check(store.open(dbPath), "open() 成功", store.lastError());
    check(store.isOpen(), "isOpen() 为真");
    check(QFile::exists(dbPath), "数据库文件已创建");

    qInfo().noquote() << "\n[2] 会话创建";
    const int s1 = store.createSession(QStringLiteral("第一个会话"));
    const int s2 = store.createSession(QStringLiteral("第二个会话"));
    check(s1 > 0 && s2 > 0 && s1 != s2, "两次创建返回不同 id");
    check(store.sessions().size() == 2, "会话数为 2");

    qInfo().noquote() << "\n[3] 消息读写";
    check(store.appendMessage(s1, Role::User, QStringLiteral("你好")), "追加 user 消息");
    check(store.appendMessage(s1, Role::Assistant, QStringLiteral("你好，有什么可以帮你")),
          "追加 assistant 消息");

    const QList<Message> msgs = store.messages(s1);
    check(msgs.size() == 2, "读回 2 条消息");
    check(!msgs.isEmpty() && msgs.first().role == Role::User, "第一条是 user");
    check(!msgs.isEmpty() && msgs.first().content == QStringLiteral("你好"), "内容正确");
    check(store.messages(s2).isEmpty(), "另一个会话仍是空的");

    qInfo().noquote() << "\n[4] 覆盖最后一条回答";
    check(store.updateLastAssistant(s1, QStringLiteral("覆盖后的回答")), "updateLastAssistant 成功");
    const QList<Message> after = store.messages(s1);
    check(after.size() == 2, "消息数没变（是覆盖不是追加）");
    check(after.last().content == QStringLiteral("覆盖后的回答"), "内容已被替换");

    qInfo().noquote() << "\n[5] 重命名与排序";
    check(store.renameSession(s1, QStringLiteral("改名后")), "rename 成功");
    bool renamed = false;
    for (const auto &s : store.sessions()) {
        if (s.id == s1 && s.title == QStringLiteral("改名后"))
            renamed = true;
    }
    check(renamed, "列表里读到新标题");
    check(store.sessions().first().messageCount == 2, "messageCount 统计正确");

    qInfo().noquote() << "\n[6] 删除最后一条";
    check(store.dropLastMessage(s2), "dropLastMessage 不报错");
    store.appendMessage(s2, Role::User, QStringLiteral("a"));
    store.appendMessage(s2, Role::User, QStringLiteral("b"));
    check(store.dropLastMessage(s2), "drop 成功");
    check(store.messages(s2).size() == 1, "确实少了一条");

    qInfo().noquote() << "\n[7] 删除会话（含级联消息）";
    check(store.deleteSession(s1), "deleteSession 成功");
    check(store.sessions().size() == 1, "会话数降为 1");
    check(store.messages(s1).isEmpty(), "该会话的消息也没了");

    qInfo().noquote() << "\n[8] 关闭后再打开（持久化）";
    store.close();
    check(!store.isOpen(), "close() 后 isOpen 为假");

    SessionStore reopened;
    check(reopened.open(dbPath), "重新打开成功", reopened.lastError());
    check(reopened.sessions().size() == 1, "数据仍在");
    check(reopened.messages(s2).size() == 1, "消息仍可读");

    qInfo().noquote() << "\n[9] 图片附件读写（P1.1）";
    check(reopened.appendMessage(s2, Role::User, QStringLiteral("看图"),
                                 QStringLiteral("C:/shots/a.png"), QStringLiteral("image/png")),
          "携带图片附件追加成功");
    const QList<Message> withImg = reopened.messages(s2);
    bool imgOk = false;
    for (const auto &m : withImg) {
        if (m.role == Role::User && m.imagePath == QStringLiteral("C:/shots/a.png")
            && m.imageMime == QStringLiteral("image/png")) {
            imgOk = true;
        } else if (!m.imagePath.isEmpty()) {
            imgOk = false; // 其它旧消息不应被误带图片
        }
    }
    check(imgOk, "图片路径与 MIME 读回正确，且旧消息无脏字段");

    qInfo().noquote() << "\n[10] 旧库兼容（无 image 列的库升级）";
    QTemporaryDir tmp2;
    check(tmp2.isValid(), "临时目录2 创建成功");
    const QString legacyDb = tmp2.path() + QStringLiteral("/legacy.db");
    {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), QStringLiteral("legacy_conn"));
            db.setDatabaseName(legacyDb);
            check(db.open(), "旧库打开（手工建老 schema）");
            QSqlQuery q(db);
            q.exec(QStringLiteral("CREATE TABLE session ("
                                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  " title TEXT NOT NULL,"
                                  " created_at INTEGER NOT NULL,"
                                  " updated_at INTEGER NOT NULL)"));
            q.exec(QStringLiteral("CREATE TABLE message ("
                                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  " session_id INTEGER NOT NULL,"
                                  " role TEXT NOT NULL,"
                                  " content TEXT NOT NULL,"
                                  " created_at INTEGER NOT NULL,"
                                  " FOREIGN KEY(session_id) REFERENCES session(id)"
                                  " ON DELETE CASCADE)"));
            q.exec(QStringLiteral("INSERT INTO session(id,title,created_at,updated_at)"
                                  " VALUES(1,'旧会话',1,1)"));
            q.exec(QStringLiteral("INSERT INTO message(id,session_id,role,content,created_at)"
                                  " VALUES(1,1,'user','旧消息文本',1)"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("legacy_conn"));
    }

    SessionStore legacy;
    check(legacy.open(legacyDb), "旧库升级打开成功", legacy.lastError());
    const QList<Message> oldMsgs = legacy.messages(1);
    check(oldMsgs.size() == 1 && oldMsgs.first().content == QStringLiteral("旧消息文本"),
          "旧数据原样可读");
    check(oldMsgs.first().imagePath.isEmpty(), "旧消息图片字段为空（默认值）");
    check(legacy.appendMessage(1, Role::User, QStringLiteral("升级后的新消息"),
                               QStringLiteral("D:/x.png"), QStringLiteral("image/png")),
          "升级后追加带图消息成功");
    const QList<Message> upgraded = legacy.messages(1);
    check(upgraded.size() == 2 && upgraded.last().imagePath == QStringLiteral("D:/x.png"),
          "升级后图片消息可写可读");

    qInfo().noquote() << QStringLiteral("\n===== 通过 %1 / 失败 %2 =====").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
