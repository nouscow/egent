#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
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

    /** Agent 只读工具是否启用 */
    bool agentEnabled() const;
    /** 工作区根目录（Agent 只能访问其下） */
    QString workspace() const;
    /** 命令执行策略：off / always / session / never */
    QString commandPolicy() const;
    /** 新填的 API Key；空表示不修改 */
    QString apiKey() const;

private:
    void buildUi();

    QDoubleSpinBox *m_temperature = nullptr;
    QSpinBox *m_maxTokens = nullptr;
    QSpinBox *m_timeoutSec = nullptr;
    QCheckBox *m_agentEnabled = nullptr;
    QLineEdit *m_workspace = nullptr;
    QComboBox *m_commandPolicy = nullptr;
    QLineEdit *m_apiKey = nullptr;
};
