#include "chatview.h"

#include "messagebubble.h"

#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

ChatView::ChatView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("chatView"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);

    m_container = new QWidget(this);
    m_container->setObjectName(QStringLiteral("chatContainer"));
    m_container->setAttribute(Qt::WA_StyledBackground, true);

    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(16, 12, 16, 12);
    m_layout->setSpacing(4);
    m_layout->addStretch(1); // 末尾留一个弹簧，让少量消息时贴顶显示

    setWidget(m_container);
}

void ChatView::rebuild(const QList<QPair<QString, QString>> &messages)
{
    for (MessageBubble *b : m_bubbles) {
        m_layout->removeWidget(b);
        b->deleteLater();
    }
    m_bubbles.clear();

    for (const auto &m : messages)
        appendMessage(m.first, m.second);

    scrollToBottom();
}

MessageBubble *ChatView::appendMessage(const QString &role, const QString &text)
{
    return insertMessage(m_bubbles.size(), role, text);
}

MessageBubble *ChatView::insertMessage(int index, const QString &role, const QString &text)
{
    index = qBound(0, index, m_bubbles.size());

    MessageBubble::Role r = MessageBubble::Role::Assistant;
    if (role == QLatin1String("user"))
        r = MessageBubble::Role::User;
    else if (role == QLatin1String("tool"))
        r = MessageBubble::Role::Tool;

    auto *bubble = new MessageBubble(r, text, m_container);

    // 布局里 0..n-1 是气泡，最后一项是弹簧，插到这个区间即可
    m_layout->insertWidget(index, bubble);
    m_bubbles.insert(index, bubble);

    connect(bubble, &MessageBubble::copyRequested, this, &ChatView::copyRequested);
    // 用指针反查索引：插入会让后面所有气泡的序号变化，捕获固定索引必然错位
    connect(bubble, &MessageBubble::regenerateRequested, this, [this, bubble]() {
        emit regenerateRequested(m_bubbles.indexOf(bubble));
    });

    scrollToBottom();
    return bubble;
}

void ChatView::updateLastText(const QString &text)
{
    updateTextAt(m_bubbles.size() - 1, text);
}

void ChatView::updateTextAt(int index, const QString &text)
{
    if (index < 0 || index >= m_bubbles.size())
        return;
    m_bubbles.at(index)->setText(text);
    scrollToBottom();
}

void ChatView::scrollToBottom()
{
    // 等布局算完再滚，否则拿到的 maximum 还是旧值
    QTimer::singleShot(0, this, [this]() {
        if (auto *bar = verticalScrollBar())
            bar->setValue(bar->maximum());
    });
}
