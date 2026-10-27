#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSettings>
#include <QString>

/**
 * 头像资源库（单例）。
 *
 * 现在 UI 上还没有「更换头像」的入口，但能力先留好：
 *     AvatarStore::instance().setImagePath("user", "D:/me.png");
 *     AvatarStore::instance().clear("assistant");
 * 调用后所有已存在的头像会自动刷新（avatarChanged 信号），
 * 路径写进 QSettings，下次启动仍然生效。
 *
 * role 取值： "user" / "assistant"
 */
class AvatarStore : public QObject
{
    Q_OBJECT

public:
    static AvatarStore &instance();

    /** 设置头像图片；path 为空等价于 clear() */
    void setImagePath(const QString &role, const QString &path);

    /** 恢复成文字头像 */
    void clear(const QString &role);

    QString imagePath(const QString &role) const;

    /** 取头像图片；没配置或加载失败返回空 QPixmap（调用方应退回文字头像） */
    QPixmap image(const QString &role) const;

signals:
    /** 某个角色的头像变了，界面据此刷新 */
    void avatarChanged(const QString &role);

private:
    explicit AvatarStore(QObject *parent = nullptr);

    static bool isValidRole(const QString &role);

    QSettings m_settings;
    QHash<QString, QString> m_paths;
    mutable QHash<QString, QPixmap> m_cache;
};
