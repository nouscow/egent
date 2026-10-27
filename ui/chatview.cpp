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
    const auto r = (role == QLatin1String("user")) ? MessageBubble::Role::User
                                                   : MessageBubble::Role::Assistant;
    auto *bubble = new MessageBubble(r, text, m_container);

    // 插到末尾弹簧之前，保证消息从顶部开始排
    m_layout->insertWidget(m_layout->count() - 1, bubble);

    // 记录索引后把气泡的右键动作转发出层
    const int index = m_bubbles.size();
    connect(bubble, &MessageBubble::copyRequested, this, &ChatView::copyRequested);
    connect(bubble, &MessageBubble::regenerateRequested, this, [this, index]() {
        emit regenerateRequested(index);
    });

    m_bubbles.append(bubble);

    scrollToBottom();
    return bubble;
}

void ChatView::updateLastText(const QString &text)
{
    if (m_bubbles.isEmpty())
        return;
    m_bubbles.last()->setText(text);
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
