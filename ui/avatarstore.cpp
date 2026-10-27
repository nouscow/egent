#include "avatarstore.h"

#include <QDebug>

namespace {
constexpr int kDefaultAvatarEdge = 128; // 载入时先缩到这个尺寸，省内存也够清晰
}

AvatarStore &AvatarStore::instance()
{
    static AvatarStore store;
    return store;
}

AvatarStore::AvatarStore(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("egent"), QStringLiteral("egent"))
{
    // 启动时把已保存的路径读进来（不预加载图片，用到才读盘）
    for (const char *role : {"user", "assistant"}) {
        const QString key = QStringLiteral("avatar/%1").arg(QString::fromLatin1(role));
        const QString path = m_settings.value(key).toString();
        if (!path.isEmpty())
            m_paths.insert(QString::fromLatin1(role), path);
    }
}

bool AvatarStore::isValidRole(const QString &role)
{
    return role == QLatin1String("user") || role == QLatin1String("assistant");
}

void AvatarStore::setImagePath(const QString &role, const QString &path)
{
    if (!isValidRole(role)) {
        qWarning().noquote() << "[avatar] 未知角色:" << role;
        return;
    }

    if (path.isEmpty()) {
        clear(role);
        return;
    }

    m_paths.insert(role, path);
    m_cache.remove(role);
    m_settings.setValue(QStringLiteral("avatar/%1").arg(role), path);
    emit avatarChanged(role);
}

void AvatarStore::clear(const QString &role)
{
    if (!isValidRole(role))
        return;

    m_paths.remove(role);
    m_cache.remove(role);
    m_settings.remove(QStringLiteral("avatar/%1").arg(role));
    emit avatarChanged(role);
}

QString AvatarStore::imagePath(const QString &role) const
{
    return m_paths.value(role);
}

QPixmap AvatarStore::image(const QString &role) const
{
    if (m_cache.contains(role))
        return m_cache.value(role);

    const QString path = m_paths.value(role);
    if (path.isEmpty())
        return QPixmap();

    QPixmap pm(path);
    if (pm.isNull()) {
        qWarning().noquote() << "[avatar] 图片加载失败:" << path;
        return QPixmap();
    }

    if (pm.width() > kDefaultAvatarEdge || pm.height() > kDefaultAvatarEdge)
        pm = pm.scaled(kDefaultAvatarEdge, kDefaultAvatarEdge,
                       Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    m_cache.insert(role, pm);
    return pm;
}
