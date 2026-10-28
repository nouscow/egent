#pragma once

#include <QList>
#include <QPair>
#include <QScrollArea>

class MessageBubble;
class QLabel;
class QVBoxLayout;

/**
 * 对话区：可滚动的气泡列表。
 *
 * 相比之前用 QTextBrowser 拼 HTML，换成真实控件后：
 *  - 头像/圆角/内边距由 QSS 精确控制，不再受富文本对 CSS 支持不全的限制
 *  - 流式追加只更新最后一个气泡，不重排整段 HTML
 */
class ChatView : public QScrollArea
{
    Q_OBJECT

public:
    explicit ChatView(QWidget *parent = nullptr);

    /** 按消息列表整体重建（切换主题时用，保证旧消息也换色） */
    void rebuild(const QList<QPair<QString, QString>> &messages,
                 const QList<QString> &images = QList<QString>());

    /** 追加一条消息，返回其指针，便于随后流式更新 */
    MessageBubble *appendMessage(const QString &role, const QString &text,
                                 const QString &imagePath = QString());

    /** 在指定位置插入（工具卡片要插在正在生成的回答之前） */
    MessageBubble *insertMessage(int index, const QString &role, const QString &text,
                                 const QString &imagePath = QString());

    /** 更新最后一条消息的内容（流式用） */
    void updateLastText(const QString &text);
    /** 按索引更新（气泡顺序与消息列表一一对应，比「最后一条」更可靠） */
    void updateTextAt(int index, const QString &text);

    int count() const { return m_bubbles.size(); }

signals:
    /** 某条消息被要求复制 */
    void copyRequested(const QString &text);
    /** 第 index 条（助手）消息被要求重新生成 */
    void regenerateRequested(int index);
    /** 第 index 条（用户）消息被要求编辑后重发 */
    void editRequested(int index);

private:
    void scrollToBottom();
    void updateEmptyHint();

    QLabel *m_emptyHint = nullptr;

    QWidget *m_container = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QList<MessageBubble *> m_bubbles;
};
