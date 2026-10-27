#include "settingsdialog.h"

#include <QDialogButtonBox>
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

    root->addLayout(form);

    auto *hint = new QLabel(
        QStringLiteral("修改立即生效，作用于下一次发送。"), this);
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
}

void SettingsDialog::saveToSettings()
{
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    s.setValue(QStringLiteral("params/temperature"), m_temperature->value());
    s.setValue(QStringLiteral("params/max_tokens"), m_maxTokens->value());
    s.setValue(QStringLiteral("params/timeout_sec"), m_timeoutSec->value());
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
