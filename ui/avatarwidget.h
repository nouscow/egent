#pragma once

#include <QString>
#include <QWidget>

/**
 * 圆形头像控件。
 *
 * 两种呈现方式，共用同一块画布：
 *   - 有图片：按中心裁剪画成圆（QSS 的 border-radius 裁不了 QPixmap，只能自绘）
 *   - 无图片：画底色圆 + 居中文字（"你" / "百"）
 *
 * 颜色不写死，全部通过 ThemeManager 的 avatar.* 令牌取，
 * 并订阅 themeChanged 自动重绘。
 */
class AvatarWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { User, Assistant };

    explicit AvatarWidget(Kind kind, QWidget *parent = nullptr);

    /** 换成图片头像；传空 pixmap 则退回文字头像 */
    void setImage(const QPixmap &pixmap);
    /** 便捷入口：直接给文件路径，加载失败自动退回文字 */
    void setImagePath(const QString &path);

    /** 文字头像显示的内容（默认「你」/「百」） */
    void setFallbackText(const QString &text);
    QString fallbackText() const { return m_fallbackText; }

    bool hasImage() const { return !m_pixmap.isNull(); }

    QSize sizeHint() const override { return {kSize, kSize}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refreshColors();

    static constexpr int kSize = 36;

    Kind m_kind = Kind::Assistant;
    QString m_fallbackText;
    QPixmap m_pixmap;
};
