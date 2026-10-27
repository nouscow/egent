#pragma once

#include <QWidget>

class AvatarWidget;
class QContextMenuEvent;
class QLabel;

/**
 * 一条消息 = 头像 + 气泡。
 *
 * 全部颜色/圆角/内边距都由 app.qss 通过动态属性 role 命中，
 * 这里只负责结构与内容，不出现任何色值。
 *
 * 右键菜单自己处理：子控件设为 NoContextMenu 让事件冒泡上来。
 *   - 任意消息：复制内容
 *   - 仅助手消息：重新生成
 */
class MessageBubble : public QWidget
{
    Q_OBJECT

public:
    enum class Role { User, Assistant };

    MessageBubble(Role role, const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    QString text() const;

    Role role() const { return m_role; }

signals:
    void copyRequested(const QString &text);
    void regenerateRequested();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void buildUi(const QString &text);
    void showContextMenu(const QPoint &globalPos);

    Role m_role = Role::Assistant;
    AvatarWidget *m_avatar = nullptr;
    QLabel *m_bubble = nullptr;
};
