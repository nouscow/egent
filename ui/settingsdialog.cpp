#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// 与 BailianProvider 的默认值保持一致
constexpr double kDefaultTemperature = 0.7;
constexpr int kDefaultMaxTokens = 4096;
constexpr int kDefaultTimeoutSec = 60;
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("参数设置"));
    setModal(true);
    buildUi();
    loadFromSettings();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    m_temperature = new QDoubleSpinBox(this);
    m_temperature->setRange(0.0, 2.0);
    m_temperature->setSingleStep(0.1);
    m_temperature->setDecimals(1);
    m_temperature->setToolTip(QStringLiteral("越低越稳定保守，越高越发散。日常问答 0.5~0.8 足够。"));
    form->addRow(QStringLiteral("Temperature"), m_temperature);

    m_maxTokens = new QSpinBox(this);
    m_maxTokens->setRange(64, 32768);
    m_maxTokens->setSingleStep(256);
    m_maxTokens->setToolTip(QStringLiteral("单次回答的最大长度，太小会被截断。"));
    form->addRow(QStringLiteral("Max tokens"), m_maxTokens);

    m_timeoutSec = new QSpinBox(this);
    m_timeoutSec->setRange(5, 300);
    m_timeoutSec->setSingleStep(5);
    m_timeoutSec->setSuffix(QStringLiteral(" 秒"));
    m_timeoutSec->setToolTip(QStringLiteral("多久没有任何响应就判定为超时并中断。"));
    form->addRow(QStringLiteral("请求超时"), m_timeoutSec);

    // ---- Agent 只读能力 ----
    m_agentEnabled = new QCheckBox(QStringLiteral("启用 Agent 只读工具"), this);
    m_agentEnabled->setToolTip(
        QStringLiteral("开启后，模型可以读取工作区内的文件、搜索内容。\n"
                       "默认关闭。当前版本只能读，不能修改或执行命令。"));
    form->addRow(QStringLiteral("Agent 能力"), m_agentEnabled);

    auto *wsRow = new QWidget(this);
    auto *wsLayout = new QHBoxLayout(wsRow);
    wsLayout->setContentsMargins(0, 0, 0, 0);
    wsLayout->setSpacing(8);
    m_workspace = new QLineEdit(wsRow);
    m_workspace->setPlaceholderText(QStringLiteral("留空则不启用文件访问"));
    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"), wsRow);
    browseBtn->setFixedWidth(72);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择工作区目录"), m_workspace->text());
        if (!dir.isEmpty())
            m_workspace->setText(dir);
    });
    wsLayout->addWidget(m_workspace, 1);
    wsLayout->addWidget(browseBtn);
    form->addRow(QStringLiteral("工作区目录"), wsRow);

    m_commandPolicy = new QComboBox(this);
    m_commandPolicy->addItem(QStringLiteral("关闭（不提供命令执行）"), QStringLiteral("off"));
    m_commandPolicy->addItem(QStringLiteral("每次执行都问我"), QStringLiteral("always"));
    m_commandPolicy->addItem(QStringLiteral("本次会话问一次"), QStringLiteral("session"));
    m_commandPolicy->addItem(QStringLiteral("不再询问（危险）"), QStringLiteral("never"));
    m_commandPolicy->setToolTip(
        QStringLiteral("格式化磁盘、递归删除等命令属于硬性禁止，"
                       "即使选「不再询问」也不会执行。"));
    form->addRow(QStringLiteral("命令执行"), m_commandPolicy);

    root->addLayout(form);

    auto *hint = new QLabel(
        QStringLiteral("修改立即生效，作用于下一次发送。"), this);
    hint->setText(QStringLiteral("修改立即生效，作用于下一次发送。\n"
                                 "Agent 工具只能访问工作区目录内的文件。"));
    hint->setObjectName(QStringLiteral("hintLabel"));
    root->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveToSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void SettingsDialog::loadFromSettings()
{
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    m_temperature->setValue(
        s.value(QStringLiteral("params/temperature"), kDefaultTemperature).toDouble());
    m_maxTokens->setValue(
        s.value(QStringLiteral("params/max_tokens"), kDefaultMaxTokens).toInt());
    m_timeoutSec->setValue(
        s.value(QStringLiteral("params/timeout_sec"), kDefaultTimeoutSec).toInt());
    m_agentEnabled->setChecked(s.value(QStringLiteral("agent/enabled"), false).toBool());
    m_workspace->setText(s.value(QStringLiteral("agent/workspace")).toString());
    const QString policy = s.value(QStringLiteral("agent/command_policy"),
                                   QStringLiteral("off")).toString();
    const int pi = m_commandPolicy->findData(policy);
    m_commandPolicy->setCurrentIndex(pi >= 0 ? pi : 0);
}

void SettingsDialog::saveToSettings()
{
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    s.setValue(QStringLiteral("params/temperature"), m_temperature->value());
    s.setValue(QStringLiteral("params/max_tokens"), m_maxTokens->value());
    s.setValue(QStringLiteral("params/timeout_sec"), m_timeoutSec->value());
    s.setValue(QStringLiteral("agent/enabled"), m_agentEnabled->isChecked());
    s.setValue(QStringLiteral("agent/workspace"), m_workspace->text().trimmed());
    s.setValue(QStringLiteral("agent/command_policy"), m_commandPolicy->currentData().toString());
}

double SettingsDialog::temperature() const
{
    return m_temperature->value();
}

int SettingsDialog::maxTokens() const
{
    return m_maxTokens->value();
}

int SettingsDialog::timeoutSeconds() const
{
    return m_timeoutSec->value();
}

bool SettingsDialog::agentEnabled() const
{
    return m_agentEnabled->isChecked();
}

QString SettingsDialog::workspace() const
{
    return m_workspace->text().trimmed();
}

QString SettingsDialog::commandPolicy() const
{
    return m_commandPolicy->currentData().toString();
}
