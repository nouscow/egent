#include "settingsdialog.h"

#include "core/providers/providerregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
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

    // ---- API Key（企划书要求：MVP 至少要能在此填写）----
    auto *keyRow = new QWidget(this);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->setSpacing(8);

    m_apiKey = new QLineEdit(keyRow);
    m_apiKey->setEchoMode(QLineEdit::Password); // 不明文回显
    keyLayout->addWidget(m_apiKey, 1);

    auto *clearKeyBtn = new QPushButton(QStringLiteral("清除"), keyRow);
    clearKeyBtn->setFixedWidth(60);
    clearKeyBtn->setToolTip(QStringLiteral("删除本地保存的 Key"));
    connect(clearKeyBtn, &QPushButton::clicked, this, [this]() {
        QSettings st(QStringLiteral("egent"), QStringLiteral("egent"));
        st.remove(ProviderRegistry::instance()
                      .configFor(ProviderRegistry::instance().currentId())
                      .settingsKey);
        m_apiKey->clear();
        m_apiKey->setPlaceholderText(QStringLiteral("已清除，请重新填写"));
    });
    keyLayout->addWidget(clearKeyBtn);

    m_apiKeyLabel = new QLabel(QStringLiteral("API Key"), this);
    form->addRow(m_apiKeyLabel, keyRow);

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

    m_writeEnabled = new QCheckBox(QStringLiteral("允许模型写文件（每次确认）"), this);
    m_writeEnabled->setToolTip(
        QStringLiteral("开启后模型可在工作区内创建或覆盖文件。\n"
                       "每次写入都会展示完整内容让你确认，且不能删除文件。"));
    form->addRow(QStringLiteral("文件写入"), m_writeEnabled);

    m_fontSize = new QComboBox(this);
    m_fontSize->addItem(QStringLiteral("小"), 13);
    m_fontSize->addItem(QStringLiteral("标准"), 14);
    m_fontSize->addItem(QStringLiteral("大"), 16);
    m_fontSize->addItem(QStringLiteral("特大"), 18);
    m_fontSize->setToolTip(QStringLiteral("调整界面正文字号，标题与小字会同步跟随"));
    form->addRow(QStringLiteral("界面字号"), m_fontSize);

    root->addLayout(form);

    auto *hint = new QLabel(
        QStringLiteral("修改立即生效，作用于下一次发送。"), this);
    hint->setText(QStringLiteral("修改立即生效，作用于下一次发送。\n"
                                 "Agent 工具只能访问工作区目录内的文件。"));
    hint->setObjectName(QStringLiteral("hintLabel"));
    root->addWidget(hint);

    // 关于入口：LGPLv3 要求向用户显著声明使用了 Qt
    auto *about = new QLabel(this);
    about->setObjectName(QStringLiteral("aboutLabel"));
    about->setTextFormat(Qt::RichText);
    about->setText(QStringLiteral("<a href=\"about\">关于 Egent %1</a>")
                       .arg(QStringLiteral(EGENT_VERSION)));
    about->setTextInteractionFlags(Qt::TextBrowserInteraction);
    about->setOpenExternalLinks(false);
    connect(about, &QLabel::linkActivated, this, &SettingsDialog::showAbout);
    root->addWidget(about);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    // 默认是英文 OK/Cancel，与界面其余中文不统一
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
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
    // Key 永不回显：环境变量优先，其次本地保存，两者都没有才提示填写。
    // 键名与标签跟随当前厂商，切厂商后在设置里填各自的 Key。
    const auto cfg = ProviderRegistry::instance().configFor(
        ProviderRegistry::instance().currentId());
    m_apiKeyLabel->setText(cfg.displayName + QStringLiteral(" API Key"));

    const bool fromEnv = !cfg.envVar.isEmpty() && !qgetenv(cfg.envVar.toUtf8()).isEmpty();
    const bool hasSaved = !s.value(cfg.settingsKey).toString().isEmpty();
    m_apiKey->clear();
    if (fromEnv)
        m_apiKey->setPlaceholderText(
            QStringLiteral("使用环境变量 %1（优先级最高）").arg(cfg.envVar));
    else if (hasSaved)
        m_apiKey->setPlaceholderText(QStringLiteral("已保存，留空则保持不变"));
    else
        m_apiKey->setPlaceholderText(QStringLiteral("未设置，请填写"));

    m_writeEnabled->setChecked(s.value(QStringLiteral("agent/write_enabled"), false).toBool());

    const int fs = s.value(QStringLiteral("ui/font_size"), 14).toInt();
    const int fi = m_fontSize->findData(fs);
    m_fontSize->setCurrentIndex(fi >= 0 ? fi : 1);
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
    const auto cfg = ProviderRegistry::instance().configFor(
        ProviderRegistry::instance().currentId());
    const QString key = m_apiKey->text().trimmed();
    if (!key.isEmpty())
        s.setValue(cfg.settingsKey, key);

    s.setValue(QStringLiteral("params/temperature"), m_temperature->value());
    s.setValue(QStringLiteral("params/max_tokens"), m_maxTokens->value());
    s.setValue(QStringLiteral("params/timeout_sec"), m_timeoutSec->value());
    s.setValue(QStringLiteral("agent/enabled"), m_agentEnabled->isChecked());
    s.setValue(QStringLiteral("agent/workspace"), m_workspace->text().trimmed());
    s.setValue(QStringLiteral("agent/command_policy"), m_commandPolicy->currentData().toString());
    s.setValue(QStringLiteral("agent/write_enabled"), m_writeEnabled->isChecked());
    s.setValue(QStringLiteral("ui/font_size"), m_fontSize->currentData().toInt());
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

QString SettingsDialog::apiKey() const
{
    return m_apiKey->text().trimmed();
}

bool SettingsDialog::writeEnabled() const
{
    return m_writeEnabled->isChecked();
}

int SettingsDialog::fontSize() const
{
    return m_fontSize->currentData().toInt();
}

void SettingsDialog::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("关于 Egent"),
        QStringLiteral(
            "<h3>Egent %1</h3>"
            "<p>本地 AI 客户端 · 多厂商对话与工具调用</p>"
            "<p style='color:#8a8f99'>构建于 Qt %2</p>"
            "<hr>"
            "<p>本程序按 <b>LGPLv3</b> 使用 Qt：未修改 Qt 源码，"
            "采用动态链接并保留独立 DLL 以便替换。</p>"
            "<p>许可全文见程序目录下的 <code>LICENSE-Qt-LGPLv3.txt</code>。<br>"
            "Qt 源码获取：https://www.qt.io/download-open-source</p>")
            .arg(QStringLiteral(EGENT_VERSION), QString::fromLatin1(qVersion())));
}
