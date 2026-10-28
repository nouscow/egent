#include "core/tools/auditlog.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

constexpr int kMaxField = 300; // 参数 / 结果摘要单字段最大长度
QString g_logPathOverride;     // 测试重定向

QString clip(const QString &s)
{
    if (s.size() <= kMaxField)
        return s;
    return s.left(kMaxField) + QStringLiteral("...(截断)");
}

} // namespace

AuditLog &AuditLog::instance()
{
    static AuditLog s;
    return s;
}

QString AuditLog::logFilePath()
{
    if (!g_logPathOverride.isEmpty())
        return g_logPathOverride;

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.egent");
    return dir + QStringLiteral("/logs/audit.log");
}

void AuditLog::setLogPathForTest(const QString &path)
{
    g_logPathOverride = path;
}

void AuditLog::append(const QString &tool, const QString &args,
                      const QString &result, Outcome outcome)
{
    const QString path = logFilePath();
    const QFileInfo fi(path);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath())) {
        qWarning().noquote() << "[audit] 无法创建日志目录:" << fi.absolutePath();
        return;
    }

    const QString line = QStringLiteral("%1 [tool=%2] [outcome=%3] [args=%4] [result=%5]\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                                  tool,
                                  outcome == Outcome::Ok      ? QStringLiteral("ok")
                                  : outcome == Outcome::Fail  ? QStringLiteral("fail")
                                                              : QStringLiteral("denied"),
                                  clip(args),
                                  clip(result));

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning().noquote() << "[audit] 无法打开日志文件:" << path << f.errorString();
        return;
    }
    f.write(line.toUtf8());
}