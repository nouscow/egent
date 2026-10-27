#include "messagebubble.h"

#include "avatarstore.h"
#include "avatarwidget.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenu>
#include <QTextDocument>

#include "theme/thememanager.h"
#include <QLabel>

namespace {
constexpr int kBubbleMaxWidth = 560;
} // namespace

MessageBubble::MessageBubble(Role role, const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_role(role)
{
    buildUi(text);
}

void MessageBubble::buildUi(const QString &text)
{
    const bool isUser = (m_role == Role::User);

    // 工具调用：没有头像，做成一条整宽的小卡片，和助手气泡左缘对齐
    if (m_role == Role::Tool) {
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(52, 3, 16, 3);

        m_bubble = new QLabel(this);
        m_bubble->setWordWrap(true);
        m_bubble->setTextFormat(Qt::PlainText);
        m_bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_bubble->setAttribute(Qt::WA_StyledBackground, true);
        m_bubble->setProperty("role", "toolBubble");
        m_bubble->setContextMenuPolicy(Qt::NoContextMenu);
        col->addWidget(m_bubble);

        setText(text);
        return;
    }

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 8, 0, 8);
    row->setSpacing(10);

    // 头像支持图片/文字两种形态，图片由 AvatarStore 统一管理（当前还没有设置入口，
    // 但调用 AvatarStore::instance().setImagePath("user", path) 即可全局生效）。
    m_avatar = new AvatarWidget(isUser ? AvatarWidget::Kind::User
                                       : AvatarWidget::Kind::Assistant,
                                this);
    const QString avatarRole = isUser ? QStringLiteral("user") : QStringLiteral("assistant");
    m_avatar->setFallbackText(isUser ? QStringLiteral("你") : QStringLiteral("E"));
    m_avatar->setImage(AvatarStore::instance().image(avatarRole));

    connect(&AvatarStore::instance(), &AvatarStore::avatarChanged,
            this, [this, avatarRole](const QString &role) {
                if (role == avatarRole)
                    m_avatar->setImage(AvatarStore::instance().image(avatarRole));
            });

    m_bubble = new QLabel(this);
    m_bubble->setWordWrap(true);
    m_bubble->setTextFormat(Qt::PlainText);
    m_bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bubble->setMaximumWidth(kBubbleMaxWidth);
    m_bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_bubble->setAttribute(Qt::WA_StyledBackground, true);
    m_bubble->setProperty("role", isUser ? "userBubble" : "assistantBubble");
    // 让右键事件冒泡到 MessageBubble，否则 QLabel 会吃掉它
    m_bubble->setContextMenuPolicy(Qt::NoContextMenu);
    m_avatar->setContextMenuPolicy(Qt::NoContextMenu);

    if (isUser) {
        row->addStretch(1);
        row->addWidget(m_bubble, 0, Qt::AlignTop);
        row->addWidget(m_avatar, 0, Qt::AlignTop);
    } else {
        row->addWidget(m_avatar, 0, Qt::AlignTop);
        row->addWidget(m_bubble, 0, Qt::AlignTop);
        row->addStretch(1);
    }

    setText(text);
}

void MessageBubble::setText(const QString &text)
{
    // 助手的回答按 Markdown 渲染；用户输入与工具卡片保持纯文本，
    // 避免把用户随手打的符号当成语法。
    if (m_role == Role::Assistant && !text.isEmpty()) {
        m_bubble->setTextFormat(Qt::RichText);
        m_bubble->setText(renderMarkdown(text));
    } else {
        m_bubble->setTextFormat(Qt::PlainText);
        m_bubble->setText(text);
    }

    // 工具卡片占满整行，不需要按内容算宽度
    if (m_role == Role::Tool)
        return;

    // QLabel 一旦 setWordWrap(true)，尺寸策略会倾向「最小宽度」，
    // 结果是气泡被压成很窄的一条。这里按最长一行显式算宽度，
    // 上限仍由 maximumWidth 兜住，超长文本自然折行。
    const QFontMetrics fm(m_bubble->font());
    int contentWidth = 0;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        contentWidth = qMax(contentWidth, fm.horizontalAdvance(line));

    // 36 ≈ 左右 padding（QSS 里的 16px×2）再留一点余量
    const int width = qBound(72, contentWidth + 36, kBubbleMaxWidth);
    if (m_bubble->minimumWidth() != width) {
        m_bubble->setMinimumWidth(width);
        m_bubble->updateGeometry();
    }
}

QString MessageBubble::text() const
{
    return m_bubble->text();
}

void MessageBubble::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenu(event->globalPos());
    event->accept();
}

void MessageBubble::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *copyAct = menu.addAction(QStringLiteral("复制内容"));
    connect(copyAct, &QAction::triggered, this, [this]() {
        emit copyRequested(m_bubble->text());
    });

    if (m_role == Role::Assistant) {
        menu.addSeparator();
        QAction *regenAct = menu.addAction(QStringLiteral("重新生成"));
        connect(regenAct, &QAction::triggered, this, [this]() {
            emit regenerateRequested();
        });
    } else if (m_role == Role::User) {
        menu.addSeparator();
        QAction *editAct = menu.addAction(QStringLiteral("编辑后重发"));
        connect(editAct, &QAction::triggered, this, [this]() {
            emit editRequested();
        });
    }

    menu.exec(globalPos);
}

QString MessageBubble::markdownCss()
{
    // Qt 生成的 HTML 自带一套浅色默认样式，在深色主题下会黑字黑底。
    // 这里追加一段主题化样式覆盖它（后定义的规则优先级更高）。
    const ThemeManager &tm = ThemeManager::instance();
    const QString text = tm.token(QStringLiteral("chat.assistantBubble.text"));
    const QString border = tm.token(QStringLiteral("border.default"));
    const QString codeBg = tm.token(QStringLiteral("bg.window"));
    const QString accent = tm.token(QStringLiteral("accent"));

    return QStringLiteral(
        "<style>"
        "body { color: %1; }"
        "td, th { border: 1px solid %2; padding: 4px 8px; }"
        "table { border-collapse: collapse; margin: 6px 0; }"
        "th { background: %3; }"
        "pre { background: %4; padding: 8px; border-radius: 6px; }"
        "code { background: %4; }"
        "a { color: %5; }"
        "hr { border: none; border-top: 1px solid %2; }"
        "</style>")
        .arg(text, border, codeBg, codeBg, accent);
}

QString MessageBubble::markdownBody(const QString &markdown)
{
    // Qt 6 自带 Markdown 解析（底层 md4c，默认 GitHub 方言），
    // 表格/代码块/加粗都支持，因此不需要引入第三方库。
    QTextDocument doc;
    doc.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    QString html = doc.toHtml();

    // 只取 <body> 内部内容，便于嵌进导出的独立 HTML 文档。
    const int bodyStart = html.indexOf(QStringLiteral("<body"));
    if (bodyStart >= 0) {
        const int gt = html.indexOf(QLatin1Char('>'), bodyStart);
        if (gt >= 0) {
            const int end = html.indexOf(QStringLiteral("</body>"), gt);
            if (end >= 0)
                return html.mid(gt + 1, end - gt - 1);
        }
    }
    return html;
}

QString MessageBubble::messageFragment(const QString &role, const QString &content)
{
    if (role == QStringLiteral("assistant") && !content.isEmpty())
        return QStringLiteral("<div class='msg assistant'>%1</div>").arg(markdownBody(content));
    // 用户/工具消息按纯文本处理，避免把随手打的符号当成语法（与界面一致）。
    return QStringLiteral("<div class='msg user'>%1</div>").arg(content.toHtmlEscaped());
}

QString MessageBubble::renderMarkdown(const QString &markdown)
{
    QTextDocument doc;
    doc.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    QString html = doc.toHtml();

    const int headEnd = html.indexOf(QStringLiteral("</head>"));
    if (headEnd >= 0)
        html.insert(headEnd, markdownCss());
    else
        html.prepend(markdownCss());

    return html;
}
