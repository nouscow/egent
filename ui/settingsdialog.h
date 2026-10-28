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
    /** 是否允许模型写文件（L4，默认关闭） */
    bool writeEnabled() const;
    /** 正文字号（px），0 表示跟随主题默认 */
    int fontSize() const;
    /** 百炼 MaaS 推理工作空间 ID（生图端点需要） */
    QString imageWorkspaceId() const;

    /** 月度额度上限（元），0 表示不设限 */
    double monthlyQuotaLimit() const;
    /** 本月已用额度（元），手动记录，跨月自动清零 */
    double quotaUsedThisMonth() const;

private:
    void buildUi();
    void showAbout();

    QDoubleSpinBox *m_temperature = nullptr;
    QSpinBox *m_maxTokens = nullptr;
    QSpinBox *m_timeoutSec = nullptr;
    QCheckBox *m_agentEnabled = nullptr;
    QLineEdit *m_workspace = nullptr;
    QComboBox *m_commandPolicy = nullptr;
    QLineEdit *m_apiKey = nullptr;
    class QLabel *m_apiKeyLabel = nullptr;
    QCheckBox *m_writeEnabled = nullptr;
    QComboBox *m_fontSize = nullptr;
    QLineEdit *m_imageWsId = nullptr;
    QDoubleSpinBox *m_quotaMonthly = nullptr;
    QDoubleSpinBox *m_quotaUsed = nullptr;
};
