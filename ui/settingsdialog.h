#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;

/**
 * 参数设置面板。
 *
 * 只管三件事：temperature / max_tokens / 请求超时。
 * 值存在 QSettings 里，改动即时生效于「下一次发送」。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    /** 从 QSettings 读取并填充控件 */
    void loadFromSettings();
    /** 把控件值写回 QSettings */
    void saveToSettings();

    double temperature() const;
    int maxTokens() const;
    int timeoutSeconds() const;

private:
    void buildUi();

    QDoubleSpinBox *m_temperature = nullptr;
    QSpinBox *m_maxTokens = nullptr;
    QSpinBox *m_timeoutSec = nullptr;
};
