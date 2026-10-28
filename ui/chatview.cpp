#include "chatview.h"

#include "messagebubble.h"

#include <QLabel>
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

    // 空状态：没有消息时给出引导，避免一片空白
    m_emptyHint = new QLabel(
        QStringLiteral("在下面输入问题开始对话\nCtrl+Enter 发送"), m_container);
    m_emptyHint->setObjectName(QStringLiteral("emptyHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_emptyHint);
    m_layout->addStretch(1);

    setWidget(m_container);
    updateEmptyHint();
}

void ChatView::rebuild(const QList<QPair<QString, QString>> &messages,
                       const QList<QString> &images)
{
    for (MessageBubble *b : m_bubbles) {
        m_layout->removeWidget(b);
        b->deleteLater();
    }
    m_bubbles.clear();

    for (int i = 0; i < messages.size(); ++i)
        appendMessage(messages.at(i).first, messages.at(i).second,
                      i < images.size() ? images.at(i) : QString());

    updateEmptyHint();
    scrollToBottom();
}

MessageBubble *ChatView::appendMessage(const QString &role, const QString &text,
                                       const QString &imagePath)
{
    return insertMessage(m_bubbles.size(), role, text, imagePath);
}

MessageBubble *ChatView::insertMessage(int index, const QString &role, const QString &text,
                                       const QString &imagePath)
{
    index = qBound(0, index, m_bubbles.size());

    MessageBubble::Role r = MessageBubble::Role::Assistant;
    if (role == QLatin1String("user"))
        r = MessageBubble::Role::User;
    else if (role == QLatin1String("tool"))
        r = MessageBubble::Role::Tool;

    auto *bubble = new MessageBubble(r, text, m_container, imagePath);

    // 布局里 0..n-1 是气泡，最后一项是弹簧，插到这个区间即可
    m_layout->insertWidget(index, bubble);
    m_bubbles.insert(index, bubble);

    connect(bubble, &MessageBubble::copyRequested, this, &ChatView::copyRequested);
    // 用指针反查索引：插入会让后面所有气泡的序号变化，捕获固定索引必然错位
    connect(bubble, &MessageBubble::regenerateRequested, this, [this, bubble]() {
        emit regenerateRequested(m_bubbles.indexOf(bubble));
    });
    connect(bubble, &MessageBubble::editRequested, this, [this, bubble]() {
        emit editRequested(m_bubbles.indexOf(bubble));
    });

    updateEmptyHint();
    scrollToBottom();
    return bubble;
}

void ChatView::updateEmptyHint()
{
    if (m_emptyHint)
        m_emptyHint->setVisible(m_bubbles.isEmpty());
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
