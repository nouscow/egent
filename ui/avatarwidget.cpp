#include "avatarwidget.h"

#include "theme/thememanager.h"

#include <QPainter>
#include <QPainterPath>

AvatarWidget::AvatarWidget(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_fallbackText(kind == Kind::User ? QStringLiteral("你") : QStringLiteral("E"))
{
    setFixedSize(kSize, kSize);
    setAttribute(Qt::WA_TranslucentBackground, false);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](const QString &) { update(); });
}

void AvatarWidget::setImage(const QPixmap &pixmap)
{
    m_pixmap = pixmap;
    update();
}

void AvatarWidget::setImagePath(const QString &path)
{
    if (path.isEmpty()) {
        setImage(QPixmap());
        return;
    }
    QPixmap pm(path);
    setImage(pm); // 加载失败时 pm 为空，自动退回文字头像
}

void AvatarWidget::setFallbackText(const QString &text)
{
    m_fallbackText = text;
    update();
}

void AvatarWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF circle = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QPainterPath clip;
    clip.addEllipse(circle);
    p.setClipPath(clip);

    const ThemeManager &tm = ThemeManager::instance();
    const bool isUser = (m_kind == Kind::User);

    if (!m_pixmap.isNull()) {
        // 等比放大铺满，再居中裁切，保证脸在圆里而不是被压扁
        const QPixmap scaled = m_pixmap.scaled(
            size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const qreal dx = (width() - scaled.width() / devicePixelRatioF()) / 2.0;
        const qreal dy = (height() - scaled.height() / devicePixelRatioF()) / 2.0;
        p.drawPixmap(QPointF(dx, dy), scaled);
    } else {
        const QColor bg = isUser ? tm.color(QStringLiteral("avatar.user.bg"),
                                            QColor(QStringLiteral("#2563eb")))
                                 : tm.color(QStringLiteral("avatar.assistant.bg"),
                                            QColor(QStringLiteral("#e5e8ef")));
        const QColor fg = isUser ? tm.color(QStringLiteral("avatar.user.text"), Qt::white)
                                 : tm.color(QStringLiteral("avatar.assistant.text"), Qt::black);

        p.fillPath(clip, bg);
        p.setPen(fg);

        QFont f = font();
        f.setPixelSize(15);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_fallbackText);
    }

    // 助手头像描一圈细边，浅色主题下才不会糊在背景里
    if (!isUser) {
        p.setClipping(false);
        const QColor border = tm.color(QStringLiteral("avatar.border"),
                                       QColor(QStringLiteral("#d3d8e2")));
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circle);
    }
}
