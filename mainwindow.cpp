#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "theme/thememanager.h"
#include "core/providers/providerregistry.h"
#include "core/models.h"

#include <QTimer>
#include <utility>
#include <functional>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include "core/agentloop.h"
#include "core/providers/providerregistry.h"
#include "core/tools/pathguard.h"
#include "core/tools/toolregistry.h"
#include "core/skills/skillregistry.h"
#include "ui/chatview.h"
#include "ui/settingsdialog.h"
#include "ui/messagebubble.h"
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QDate>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QToolButton>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTextStream>
#include <QDialog>
#include <QPixmap>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 必须先注册厂商：buildUi() 里的厂商/模型下拉要读注册表，
    // 放在 buildUi 之后会拿到空列表，界面出现两个空白下拉框。
    ProviderRegistry::instance().registerBuiltins();

    setWindowTitle(QStringLiteral("Egent · 百炼"));
    resize(980, 760);

    buildUi();

    setupShortcuts();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);

    connect(m_chatView, &ChatView::copyRequested, this, &MainWindow::onCopyMessage);
    connect(m_chatView, &ChatView::regenerateRequested,
            this, &MainWindow::onRegenerateMessage);
    connect(m_chatView, &ChatView::editRequested,
            this, &MainWindow::onEditMessage);

    // ---- 真实流式接口 + Agent 循环 ----
    // 界面不再直连 provider：所有请求都经过 AgentLoop，
    // 由它决定「这一轮是最终回答，还是要先调工具」。
    m_provider = ProviderRegistry::instance().current();
    m_agentLoop = new AgentLoop(m_provider, this);
    connect(m_agentLoop, &AgentLoop::contentDelta, this, &MainWindow::onProviderToken);
    connect(m_agentLoop, &AgentLoop::finished, this,
            [this](const QList<Message> &) { onProviderFinished(); });
    connect(m_agentLoop, &AgentLoop::errorOccurred,
            this, &MainWindow::onProviderError);
    connect(m_agentLoop, &AgentLoop::toolCallStarted,
            this, &MainWindow::onToolStarted);
    connect(m_agentLoop, &AgentLoop::toolCallFinished,
            this, &MainWindow::onToolFinished);
    connect(m_agentLoop, &AgentLoop::permissionRequired,
            this, &MainWindow::onPermissionRequired);

    // 图片生成结果（与对话流式走不同信号，互不影响）
    connect(m_provider, &IModelProvider::imageReady, this, &MainWindow::onImageReady);
    connect(m_provider, &IModelProvider::imageError, this, &MainWindow::onImageError);

    // 限流重试在状态栏提示，避免用户以为卡住
    connect(m_provider, &IModelProvider::retrying, this,
            [this](int attempt, int delayMs) {
                statusBar()->showMessage(
                    QStringLiteral("请求被限流，%1 秒后自动重试（第 %2/%3 次）")
                        .arg(delayMs / 1000)
                        .arg(attempt)
                        .arg(3),
                    delayMs);
            });

    m_streamTimer = new QTimer(this);
    m_streamTimer->setInterval(50);
    connect(m_streamTimer, &QTimer::timeout, this, &MainWindow::flushStream);

    applyParamsFromSettings();

    // 启动时应用上次保存的字号
    {
        QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
        ThemeManager::instance().setFontSize(
            s.value(QStringLiteral("ui/font_size"), 0).toInt());
    }

    // ---- M3：打开本地会话库，载入最近一次会话（没有就新建） ----
    if (!m_store.open()) {
        qWarning().noquote() << "[db] 无法打开会话库:" << m_store.lastError();
    }
    reloadSessionList();
    const auto existing = m_store.sessions();
    if (!existing.isEmpty())
        loadSession(existing.first().id);
    else
        loadSession(ensureSession());

    renderChat();
    refreshQuotaLabel();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *outer = new QHBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---------- 左侧：会话列表 ----------
    auto *sidebar = new QWidget(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(220);

    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 12, 10, 12);
    sideLayout->setSpacing(8);

    auto *newSessionBtn = new QPushButton(QStringLiteral("＋ 新会话"), sidebar);
    newSessionBtn->setObjectName(QStringLiteral("newSessionButton"));
    connect(newSessionBtn, &QPushButton::clicked, this, &MainWindow::onNewSession);
    sideLayout->addWidget(newSessionBtn);

    m_sessionList = new QListWidget(sidebar);
    m_sessionList->setObjectName(QStringLiteral("sessionList"));
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionList->setFrameShape(QFrame::NoFrame);
    connect(m_sessionList, &QListWidget::itemClicked,
            this, &MainWindow::onSessionActivated);
    connect(m_sessionList, &QListWidget::customContextMenuRequested,
            this, &MainWindow::onSessionContextMenu);
    sideLayout->addWidget(m_sessionList, 1);

    // 左栏底部展示额度：DashScope API Key 无权查询账号级额度（经实测），
    // 因此走 P1.4 方案 B——本地手动记录 + 跳转控制台查看真实用量。
    m_quotaLabel = new QLabel(sidebar);
    m_quotaLabel->setObjectName(QStringLiteral("quotaLabel"));
    m_quotaLabel->setToolTip(QStringLiteral(
        "百炼未提供用 DashScope API Key 查询额度的接口。\n"
        "以下数据为本地手动记录（设置中填写），厂商真实用量请在控制台查看。"));
    sideLayout->addWidget(m_quotaLabel);

    outer->addWidget(sidebar);

    // ---------- 右侧：对话区 ----------
    auto *rightWrap = new QWidget(central);
    auto *root = new QVBoxLayout(rightWrap);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 顶部工具条 ----------
    auto *toolbar = new QWidget(central);
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setAttribute(Qt::WA_StyledBackground, true); // 没有这行 QSS 的 background 不生效
    toolbar->setFixedHeight(52);

    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(16, 8, 16, 8);
    toolLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Egent"), toolbar);
    title->setObjectName(QStringLiteral("toolbarTitle"));
    toolLayout->addWidget(title);

    toolLayout->addStretch(1);

    m_providerBox = new QComboBox(toolbar);
    for (const QString &id : ProviderRegistry::instance().ids())
        m_providerBox->addItem(ProviderRegistry::instance().displayName(id), id);
    {
        const int idx = m_providerBox->findData(ProviderRegistry::instance().currentId());
        m_providerBox->blockSignals(true);
        m_providerBox->setCurrentIndex(idx >= 0 ? idx : 0);
        m_providerBox->blockSignals(false);
    }
    m_providerBox->setMinimumWidth(88);
    m_providerBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_providerBox->setToolTip(QStringLiteral("切换模型厂商"));
    connect(m_providerBox, &QComboBox::currentIndexChanged,
            this, &MainWindow::onProviderSelected);
    toolLayout->addWidget(m_providerBox);

    m_modelBox = new QComboBox(toolbar);
    refreshModelList();
    m_modelBox->setToolTip(QStringLiteral("模型选择（实时生效）"));
    m_modelBox->setMinimumWidth(150);
    m_modelBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_modelBox, &QComboBox::currentIndexChanged,
            this, &MainWindow::onModelSelected);
    toolLayout->addWidget(m_modelBox);

    // 恢复上次选择的厂商与模型（持久化，重启后保持）
    {
        QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
        const QString savedProv = s.value(QStringLiteral("ui/provider_id")).toString();
        const int pi = savedProv.isEmpty() ? -1 : m_providerBox->findData(savedProv);
        if (pi >= 0 && pi != m_providerBox->currentIndex())
            m_providerBox->setCurrentIndex(pi); // 触发 onProviderSelected → 恢复模型
        const QString savedModel = s.value(QStringLiteral("ui/model")).toString();
        const int mi = m_modelBox->findText(savedModel);
        if (mi >= 0)
            m_modelBox->setCurrentIndex(mi);
    }

    m_themeBox = new QComboBox(toolbar);
    const QStringList ids = ThemeManager::instance().availableThemes();
    for (const QString &id : ids) {
        m_themeBox->addItem(ThemeManager::instance().themeDisplayName(id), id);
    }
    const int current = m_themeBox->findData(ThemeManager::instance().currentThemeId());
    if (current >= 0) {
        m_themeBox->blockSignals(true);
        m_themeBox->setCurrentIndex(current);
        m_themeBox->blockSignals(false);
    }
    m_themeBox->setMinimumWidth(80);
    m_themeBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_themeBox->setToolTip(QStringLiteral("切换主题（运行时即时生效）"));
    connect(m_themeBox, &QComboBox::currentIndexChanged,
            this, &MainWindow::onThemeSelected);
    toolLayout->addWidget(m_themeBox);

    auto *settingsBtn = new QPushButton(toolbar);
    settingsBtn->setObjectName(QStringLiteral("iconButton"));
    settingsBtn->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    settingsBtn->setIconSize(QSize(18, 18));
    settingsBtn->setFixedSize(34, 34);
    settingsBtn->setToolTip(QStringLiteral("参数设置（temperature / max_tokens / 超时）"));
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    toolLayout->addWidget(settingsBtn);

    m_thinkingBox = new QCheckBox(QStringLiteral("深度思考"), toolbar);
    m_thinkingBox->setChecked(false);
    m_thinkingBox->setToolTip(
        QStringLiteral("默认关闭：实测同一问题首字 0.6s vs 7.5s。\n"
                       "遇到需要推敲的难题再打开。"));
    toolLayout->addWidget(m_thinkingBox);

    auto *genImageBtn = new QPushButton(QStringLiteral("生成图片"), toolbar);
    genImageBtn->setObjectName(QStringLiteral("genImageButton"));
    genImageBtn->setFixedHeight(34);
    genImageBtn->setToolTip(QStringLiteral("文生图（百炼通义万相 qwen-image-3.0）"));
    connect(genImageBtn, &QPushButton::clicked, this, &MainWindow::onGenerateImage);
    toolLayout->addWidget(genImageBtn);

    root->addWidget(toolbar);

    // ---------- 对话区 ----------
    auto *chatWrap = new QWidget(central);
    auto *chatLayout = new QVBoxLayout(chatWrap);
    chatLayout->setContentsMargins(16, 12, 16, 8);

    m_chatView = new ChatView(chatWrap);
    chatLayout->addWidget(m_chatView);

    root->addWidget(chatWrap, 1);

    // ---------- 输入区 ----------
    auto *inputWrap = new QWidget(central);
    auto *inputLayout = new QHBoxLayout(inputWrap);
    inputLayout->setContentsMargins(16, 0, 16, 14);
    inputLayout->setSpacing(10);

    m_input = new QPlainTextEdit(inputWrap);
    m_input->setPlaceholderText(
        QStringLiteral("说点什么……（Ctrl+Enter 发送 · Esc 停止 · Ctrl+N 新会话 · F1 帮助）"));
    m_input->setFixedHeight(78);
    inputLayout->addWidget(m_input, 1);

    // 技能命令菜单按钮（/help、/calc 等，点击插入命令到输入框）
    m_skillMenuBtn = new QToolButton(inputWrap);
    m_skillMenuBtn->setObjectName(QStringLiteral("skillButton"));
    m_skillMenuBtn->setText(QStringLiteral("/"));
    m_skillMenuBtn->setFixedSize(40, 40);
    m_skillMenuBtn->setToolTip(QStringLiteral("技能命令（输入以 / 开头，例如 /calc 1+2*3）"));
    m_skillMenuBtn->setPopupMode(QToolButton::InstantPopup);
    m_skillMenuBtn->setCursor(Qt::PointingHandCursor);
    auto *skillMenu = new QMenu(m_skillMenuBtn);
    skillMenu->setObjectName(QStringLiteral("skillMenu"));
    connect(skillMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildSkillMenu);
    m_skillMenuBtn->setMenu(skillMenu);
    rebuildSkillMenu();
    inputLayout->addWidget(m_skillMenuBtn, 0, Qt::AlignBottom);

    // 图片选择按钮 + 已附加图片的缩略预览（点击预览移除）
    m_imageBtn = new QPushButton(inputWrap);
    m_imageBtn->setObjectName(QStringLiteral("imageButton"));
    m_imageBtn->setIcon(QIcon(QStringLiteral(":/icons/image.svg")));
    m_imageBtn->setIconSize(QSize(17, 17));
    m_imageBtn->setFixedSize(40, 40);
    m_imageBtn->setToolTip(QStringLiteral("发送图片"));
    m_imageBtn->setCursor(Qt::PointingHandCursor);
    connect(m_imageBtn, &QPushButton::clicked, this, &MainWindow::onPickImage);
    inputLayout->addWidget(m_imageBtn, 0, Qt::AlignBottom);

    m_imagePreview = new QToolButton(inputWrap);
    m_imagePreview->setObjectName(QStringLiteral("imagePreview"));
    m_imagePreview->setFixedSize(40, 40);
    m_imagePreview->setIconSize(QSize(36, 36));
    m_imagePreview->setToolTip(QStringLiteral("已附加图片，点击移除"));
    m_imagePreview->setCursor(Qt::PointingHandCursor);
    m_imagePreview->hide();
    connect(m_imagePreview, &QToolButton::clicked, this, &MainWindow::onClearImage);
    inputLayout->addWidget(m_imagePreview, 0, Qt::AlignBottom);

    m_sendBtn = new QPushButton(QStringLiteral("发送"), inputWrap);
    m_sendBtn->setObjectName(QStringLiteral("sendButton"));
    m_sendBtn->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    m_sendBtn->setIconSize(QSize(17, 17));
    m_sendBtn->setFixedSize(88, 40);
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    inputLayout->addWidget(m_sendBtn, 0, Qt::AlignBottom);

    root->addWidget(inputWrap);

    outer->addWidget(rightWrap, 1);
    setCentralWidget(central);
}

void MainWindow::setupShortcuts()
{
    // 统一在窗口级注册（WindowShortcut）：模态对话框打开时主窗口不活跃，
    // 这些快捷键自动失效，不会和对话框里的 Esc / 回车打架。
    auto bind = [this](const QKeySequence &ks, const std::function<void()> &fn) {
        auto *sc = new QShortcut(ks, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    // 发送：主回车与小键盘回车都触发（聊天输入里回车换行、用 Ctrl 发送）
    bind(QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { onSendClicked(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this] { onSendClicked(); });

    // Esc：生成中 = 停止；否则清空输入框（输入为空时不做事）
    bind(QKeySequence(Qt::Key_Escape), [this] {
        if (m_streamingMsgIndex >= 0) {
            if (m_agentLoop)
                m_agentLoop->cancel();
        } else if (m_input && !m_input->toPlainText().isEmpty()) {
            m_input->clear();
        }
    });

    // 新会话
    bind(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { onNewSession(); });

    // 快速切模型：聚焦模型下拉并展开，↑↓ 选择、回车确认
    bind(QKeySequence(Qt::CTRL | Qt::Key_K), [this] {
        if (m_modelBox) {
            m_modelBox->setFocus();
            m_modelBox->showPopup();
        }
    });

    // 重新生成上一条回答（生成中忽略，由 onRegenerateMessage 内部再判一次）
    bind(QKeySequence(Qt::CTRL | Qt::Key_R), [this] {
        int idx = -1;
        for (int i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages.at(i).first == QLatin1String("assistant")) {
                idx = i;
                break;
            }
        }
        if (idx >= 0)
            onRegenerateMessage(idx);
    });

    // 导出当前会话为 Markdown
    bind(QKeySequence(Qt::CTRL | Qt::Key_E), [this] { onExportSession(m_currentSessionId); });

    // 打开设置（与多数桌面应用一致：Ctrl+,）
    bind(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openSettings(); });

    // 快捷键帮助：F1 或 Ctrl+/ 都可以
    bind(QKeySequence(Qt::Key_F1), [this] { showShortcutsHelp(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_Slash), [this] { showShortcutsHelp(); });
}

void MainWindow::showShortcutsHelp()
{
    const QString text = QStringLiteral(
        "<table cellspacing='6'>"
        "<tr><td><b>Ctrl + Enter</b></td><td>发送消息</td></tr>"
        "<tr><td><b>Esc</b></td><td>生成中停止；否则清空输入框</td></tr>"
        "<tr><td><b>Ctrl + N</b></td><td>新建会话</td></tr>"
        "<tr><td><b>Ctrl + K</b></td><td>快速切换模型</td></tr>"
        "<tr><td><b>Ctrl + R</b></td><td>重新生成上一条回答</td></tr>"
        "<tr><td><b>Ctrl + E</b></td><td>导出当前会话为 Markdown</td></tr>"
        "<tr><td><b>Ctrl + ,</b></td><td>打开设置</td></tr>"
        "<tr><td><b>F1 / Ctrl + /</b></td><td>显示本帮助</td></tr>"
        "</table>");
    QMessageBox::information(this, QStringLiteral("快捷键"), text);
}

void MainWindow::refreshQuotaLabel()
{
    if (!m_quotaLabel)
        return;
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    const double limit = s.value(QStringLiteral("quota/monthly_limit"), 0.0).toDouble();
    const double used = s.value(QStringLiteral("quota/month_used"), 0.0).toDouble();
    const QString text = limit > 0.0
        ? QStringLiteral("本月已用 ¥%1 / ¥%2")
              .arg(used, 0, 'f', 2)
              .arg(limit, 0, 'f', 0)
        : QStringLiteral("本月已用 ¥%1（未设上限）").arg(used, 0, 'f', 2);
    m_quotaLabel->setText(text);
}

void MainWindow::onThemeSelected(int index)
{
    if (index < 0)
        return;
    const QString id = m_themeBox->itemData(index).toString();
    ThemeManager::instance().applyTheme(id);
}

void MainWindow::onThemeChanged(const QString &id)
{
    Q_UNUSED(id)
    // 主题变了：对话区是 HTML，必须整体重渲染，否则旧消息还是旧颜色
    renderChat();

    // 让动态属性控件重新走一遍样式（Qt 的经典坑）
    const auto widgets = findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
}

void MainWindow::reloadSessionList()
{
    if (!m_sessionList)
        return;

    const QSignalBlocker blocker(m_sessionList);
    m_sessionList->clear();

    const QDate today = QDate::currentDate();
    const QColor headerColor =
        ThemeManager::instance().color(QStringLiteral("text.secondary"), QColor(0x88, 0x88, 0x88));

    QString currentGroup;
    for (const auto &info : m_store.sessions()) {
        // 按日期打组：今天 / 昨天 / 更早
        const QDate d = QDateTime::fromSecsSinceEpoch(info.updatedAt).date();
        QString group;
        if (d == today)
            group = QStringLiteral("今天");
        else if (d == today.addDays(-1))
            group = QStringLiteral("昨天");
        else
            group = QStringLiteral("更早");

        if (group != currentGroup) {
            currentGroup = group;
            auto *header = new QListWidgetItem(group, m_sessionList);
            // 分组标题不可选、不可点
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(headerColor);
            QFont f = header->font();
            f.setPointSize(qMax(8, f.pointSize() - 1));
            f.setBold(true);
            header->setFont(f);
            header->setData(Qt::UserRole, -1);
        }

        auto *item = new QListWidgetItem(info.title, m_sessionList);
        item->setData(Qt::UserRole, info.id);
        item->setToolTip(QStringLiteral("%1 条消息").arg(info.messageCount));
        if (info.id == m_currentSessionId)
            m_sessionList->setCurrentItem(item);
    }
}

int MainWindow::ensureSession()
{
    if (m_currentSessionId > 0)
        return m_currentSessionId;
    if (!m_store.isOpen())
        return -1;

    m_currentSessionId = m_store.createSession(QStringLiteral("新会话"));
    reloadSessionList();
    return m_currentSessionId;
}

void MainWindow::loadSession(int sessionId)
{
    if (sessionId <= 0)
        return;

    m_currentSessionId = sessionId;
    m_messages.clear();
    m_messageImages.clear();
    m_streamingMsgIndex = -1;
    m_streamBuf.clear();

    if (m_store.isOpen()) {
        for (const Message &m : m_store.messages(sessionId)) {
            m_messages.append({roleToStr(m.role), m.content});
            m_messageImages.append(m.imagePath);
        }
    }

    if (m_chatView)
        m_chatView->rebuild(m_messages, m_messageImages);

    if (m_sessionList) {
        for (int i = 0; i < m_sessionList->count(); ++i) {
            auto *it = m_sessionList->item(i);
            if (it->data(Qt::UserRole).toInt() == sessionId) {
                m_sessionList->setCurrentItem(it);
                break;
            }
        }
    }
}

void MainWindow::onNewSession()
{
    if (!m_store.isOpen())
        return;
    const int id = m_store.createSession(QStringLiteral("新会话"));
    if (id <= 0)
        return;
    reloadSessionList();
    loadSession(id);
}

void MainWindow::onSessionActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const int id = item->data(Qt::UserRole).toInt();
    if (id > 0 && id != m_currentSessionId)
        loadSession(id);
}

void MainWindow::onSessionContextMenu(const QPoint &pos)
{
    if (!m_sessionList)
        return;
    QListWidgetItem *item = m_sessionList->itemAt(pos);
    if (!item)
        return;

    const int id = item->data(Qt::UserRole).toInt();
    QMenu menu(this);
    QAction *renameAct = menu.addAction(QStringLiteral("重命名"));
    QAction *exportAct = menu.addAction(QStringLiteral("导出为 Markdown…"));
    QAction *exportHtmlAct = menu.addAction(QStringLiteral("导出为 HTML…"));
    QAction *clearAct = menu.addAction(QStringLiteral("清空消息"));
    menu.addSeparator();
    QAction *deleteAct = menu.addAction(QStringLiteral("删除会话"));

    QAction *chosen = menu.exec(m_sessionList->viewport()->mapToGlobal(pos));
    if (chosen == renameAct) {
        bool ok = false;
        const QString title = QInputDialog::getText(
            this, QStringLiteral("重命名会话"), QStringLiteral("新的标题"),
            QLineEdit::Normal, item->text(), &ok);
        if (ok && !title.trimmed().isEmpty()) {
            m_store.renameSession(id, title.trimmed());
            reloadSessionList();
        }
    } else if (chosen == exportAct) {
        onExportSession(id);
    } else if (chosen == exportHtmlAct) {
        onExportSessionHtml(id);
    } else if (chosen == clearAct) {
        const auto ret = QMessageBox::question(
            this, QStringLiteral("清空消息"),
            QStringLiteral("确定清空「%1」的全部对话内容？此操作不可撤销。").arg(item->text()));
        if (ret != QMessageBox::Yes)
            return;
        if (!m_store.truncateAfter(id, 0))
            return;
        reloadSessionList();
        if (id == m_currentSessionId)
            loadSession(id);
        statusBar()->showMessage(QStringLiteral("已清空消息"), 3000);
    } else if (chosen == deleteAct) {
        const auto ret = QMessageBox::question(
            this, QStringLiteral("删除会话"),
            QStringLiteral("确定删除「%1」？对话记录会一并删除。").arg(item->text()));
        if (ret != QMessageBox::Yes)
            return;

        m_store.deleteSession(id);
        if (id == m_currentSessionId) {
            m_currentSessionId = -1;
            const auto rest = m_store.sessions();
            if (!rest.isEmpty())
                loadSession(rest.first().id);
            else
                loadSession(ensureSession());
        }
        reloadSessionList();
    }
}

void MainWindow::refreshModelList()
{
    if (!m_modelBox)
        return;
    const QString current = m_modelBox->currentText();
    const QStringList models =
        ProviderRegistry::instance().models(ProviderRegistry::instance().currentId());

    m_modelBox->blockSignals(true);
    m_modelBox->clear();
    m_modelBox->addItems(models);
    const int keep = m_modelBox->findText(current);
    if (keep >= 0)
        m_modelBox->setCurrentIndex(keep);
    m_modelBox->blockSignals(false);
}

void MainWindow::onProviderSelected(int index)
{
    if (index < 0)
        return;
    const QString id = m_providerBox->itemData(index).toString();
    ProviderRegistry::instance().setCurrentId(id);

    IModelProvider *p = ProviderRegistry::instance().current();
    if (!p)
        return;

    m_provider = p;
    if (m_agentLoop)
        m_agentLoop->setProvider(p);
    // 重新绑定生图信号到新 provider（旧连接先解绑，避免重复触发）
    if (m_provider) {
        disconnect(m_provider, &IModelProvider::imageReady, this, &MainWindow::onImageReady);
        disconnect(m_provider, &IModelProvider::imageError, this, &MainWindow::onImageError);
        connect(m_provider, &IModelProvider::imageReady, this, &MainWindow::onImageReady);
        connect(m_provider, &IModelProvider::imageError, this, &MainWindow::onImageError);
    }
    refreshModelList();

    // 持久化：记住本次选择的厂商与模型，重启后保持（P2 小增强）
    {
        QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
        s.setValue(QStringLiteral("ui/provider_id"), id);
        const QString savedModel = s.value(QStringLiteral("ui/model")).toString();
        const int mi = m_modelBox->findText(savedModel);
        if (mi >= 0)
            m_modelBox->setCurrentIndex(mi);
    }

    applyParamsFromSettings(); // 新厂商的 Key / 工具开关重新下发
}

void MainWindow::onModelSelected(int)
{
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    s.setValue(QStringLiteral("ui/model"), m_modelBox->currentText());
}

void MainWindow::startRequest(const QList<Message> &history)
{
    if (!m_provider || !m_agentLoop)
        return;
    // 先切按钮再发请求：若 send 同步失败，onProviderError 会把文字改回来
    m_sendBtn->setText(QStringLiteral("停止"));
    m_sendBtn->setIcon(QIcon(QStringLiteral(":/icons/stop.svg")));
    m_provider->setOption(QStringLiteral("enable_thinking"),
                          m_thinkingBox && m_thinkingBox->isChecked());
    m_agentLoop->setModel(m_modelBox->currentText());
    m_agentLoop->start(history);
}

void MainWindow::onToolStarted(const QString &name, const QString &argsJson)
{
    // 工具调用发生在「最终回答」之前，所以卡片要插在正在生成的那条回答前面
    const QString text =
        QStringLiteral("⚙ %1  ·  执行中…\n%2").arg(name, argsJson.left(300));

    int at = (m_streamingMsgIndex >= 0) ? m_streamingMsgIndex : m_messages.size();
    m_messages.insert(at, {QStringLiteral("tool"), text});
    m_messageImages.insert(at, QString());
    m_chatView->insertMessage(at, QStringLiteral("tool"), text);

    // 助手气泡被推到后面一位，流式刷新的索引要跟着走
    if (m_streamingMsgIndex >= 0)
        ++m_streamingMsgIndex;
}

void MainWindow::onPermissionRequired(const QString &toolName, const QString &detail)
{
    const bool isWrite = (toolName == QLatin1String("write_file"));

    // 命令有「从不询问」策略；写文件没有 —— L4 一律逐次确认
    if (!isWrite
        && (m_commandPolicy == QLatin1String("never")
            || (m_commandPolicy == QLatin1String("session") && m_commandApprovedOnce))) {
        m_agentLoop->respondPermission(true);
        return;
    }

    QString shown = detail;
    QString title = QStringLiteral("确认执行命令");
    QString question = QStringLiteral("模型请求在工作区执行以下命令：");

    const QJsonDocument doc = QJsonDocument::fromJson(detail.toUtf8());
    const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

    if (isWrite) {
        title = QStringLiteral("确认写入文件");
        question = QStringLiteral("模型请求写入以下文件：");
        const QString path = obj.value(QStringLiteral("path")).toString();
        QString content = obj.value(QStringLiteral("content")).toString();

        // 只展示前 20 行，避免弹窗被长文件撑爆
        const QStringList lines = content.split(QLatin1Char('\n'));
        if (lines.size() > 20) {
            content = lines.mid(0, 20).join(QLatin1Char('\n'))
                      + QStringLiteral("\n…（共 %1 行，仅预览前 20 行）").arg(lines.size());
        }
        shown = path + QStringLiteral("\n\n") + content;
    } else {
        shown = obj.value(QStringLiteral("command")).toString(detail);
    }

    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setIcon(QMessageBox::Warning);
    box.setText(question);
    box.setInformativeText(shown);
    box.setDetailedText(QStringLiteral("工具：%1\n参数：%2").arg(toolName, detail));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    box.button(QMessageBox::Yes)->setText(QStringLiteral("允许执行"));
    box.button(QMessageBox::No)->setText(QStringLiteral("拒绝"));

    const bool granted = (box.exec() == QMessageBox::Yes);
    if (granted && !isWrite && m_commandPolicy == QLatin1String("session"))
        m_commandApprovedOnce = true;

    m_agentLoop->respondPermission(granted);
}

void MainWindow::onToolFinished(const QString &name, bool ok, const QString &result)
{
    // 卡片的索引不再固定是「最后一条」——助手气泡排在它后面
    int idx = -1;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages.at(i).first == QLatin1String("tool")) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return;

    const QString text = QStringLiteral("⚙ %1  ·  %2\n%3")
                             .arg(name,
                                  ok ? QStringLiteral("成功") : QStringLiteral("失败"),
                                  result.left(500));
    m_messages[idx].second = text;
    m_chatView->updateTextAt(idx, text);
}

void MainWindow::onPickImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择图片"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
    if (path.isEmpty())
        return;

    m_pendingImage = path;
    m_pendingImageMime = guessImageMime(path);
    updateImagePreview();
}

void MainWindow::onClearImage()
{
    m_pendingImage.clear();
    m_pendingImageMime.clear();
    updateImagePreview();
}

void MainWindow::updateImagePreview()
{
    if (!m_imagePreview)
        return;

    if (m_pendingImage.isEmpty()) {
        m_imagePreview->hide();
        return;
    }

    m_imagePreview->setIcon(QIcon(m_pendingImage));
    m_imagePreview->setToolTip(QStringLiteral("已附加图片（点击移除）：\n%1").arg(m_pendingImage));
    m_imagePreview->show();
}

QString MainWindow::guessImageMime(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    if (suffix == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    return QStringLiteral("image/png");
}

void MainWindow::onCopyMessage(const QString &text)
{
    QApplication::clipboard()->setText(text);
}

void MainWindow::onEditMessage(int index)
{
    if (!m_provider || m_streamingMsgIndex >= 0)
        return;
    if (index < 0 || index >= m_messages.size())
        return;
    if (m_messages.at(index).first != QLatin1String("user"))
        return;

    bool ok = false;
    const QString edited = QInputDialog::getMultiLineText(
        this, QStringLiteral("编辑后重发"),
        QStringLiteral("修改后将从这里重新发送，该消息之后的对话会被丢弃："),
        m_messages.at(index).second, &ok);
    if (!ok || edited.trimmed().isEmpty())
        return;

    const QString newText = edited.trimmed();

    // 气泡列表里混着工具卡片，而库里只存 user/assistant，
    // 所以要用「排除 tool 后的序号」去定位数据库行，不能直接用 UI 索引。
    int dbIndex = 0;
    for (int i = 0; i < index; ++i) {
        if (m_messages.at(i).first != QLatin1String("tool"))
            ++dbIndex;
    }

    m_messages.resize(index + 1);
    m_messageImages.resize(index + 1);
    m_messages[index].second = newText;

    if (m_store.isOpen() && m_currentSessionId > 0) {
        m_store.updateMessageAt(m_currentSessionId, dbIndex, newText);
        m_store.truncateAfter(m_currentSessionId, dbIndex + 1);
    }

    m_chatView->rebuild(m_messages, m_messageImages);

    QList<Message> history;
    for (int i = 0; i <= index; ++i) {
        if (m_messages.at(i).first == QLatin1String("tool"))
            continue;
        history.append({roleFromStr(m_messages.at(i).first), m_messages.at(i).second,
                        m_messageImages.at(i), QString()});
    }

    m_messages.append({QStringLiteral("assistant"), QString()});
    m_messageImages.append(QString());
    m_streamingMsgIndex = m_messages.size() - 1;
    m_streamBuf.clear();
    m_chatView->appendMessage(QStringLiteral("assistant"), QString());

    startRequest(history);
}

void MainWindow::onRegenerateMessage(int index)
{
    if (!m_provider || m_streamingMsgIndex >= 0)
        return; // 生成中不接受重新生成
    if (index < 0 || index >= m_messages.size())
        return;
    if (m_messages.at(index).first != QLatin1String("assistant"))
        return;

    // 往回找这条回答对应的那条提问
    int userIdx = index - 1;
    while (userIdx >= 0 && m_messages.at(userIdx).first != QLatin1String("user"))
        --userIdx;
    if (userIdx < 0)
        return;

    // 如果那条旧回答已经落库，先把它删掉，避免库里出现两条回答
    if (m_store.isOpen() && m_currentSessionId > 0) {
        const auto stored = m_store.messages(m_currentSessionId);
        if (!stored.isEmpty() && stored.last().role == Role::Assistant)
            m_store.dropLastMessage(m_currentSessionId);
    }

    // 截断到该提问为止，后面的回答（可能有多条）全部丢掉
    m_messages.resize(userIdx + 1);
    m_messageImages.resize(userIdx + 1);
    m_chatView->rebuild(m_messages, m_messageImages);

    QList<Message> history;
    for (int i = 0; i <= userIdx; ++i)
        history.append({roleFromStr(m_messages.at(i).first), m_messages.at(i).second,
                        m_messageImages.at(i), QString()});

    // 重新挂一个空的助手气泡，等流式填充
    m_messages.append({QStringLiteral("assistant"), QString()});
    m_messageImages.append(QString());
    m_streamingMsgIndex = m_messages.size() - 1;
    m_streamBuf.clear();
    m_chatView->appendMessage(QStringLiteral("assistant"), QString());

    startRequest(history);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    applyParamsFromSettings();

    // 启动时应用上次保存的字号
    {
        QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
        ThemeManager::instance().setFontSize(
            s.value(QStringLiteral("ui/font_size"), 0).toInt());
    }

    // ---- M3：打开本地会话库，载入最近一次会话（没有就新建） ----
    if (!m_store.open()) {
        qWarning().noquote() << "[db] 无法打开会话库:" << m_store.lastError();
    }
    reloadSessionList();
    const auto existing = m_store.sessions();
    if (!existing.isEmpty())
        loadSession(existing.first().id);
    else
        loadSession(ensureSession());
    refreshQuotaLabel();
}

void MainWindow::applyParamsFromSettings()
{
    if (!m_provider)
        return;
    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    m_provider->setOption(QStringLiteral("temperature"),
                          s.value(QStringLiteral("params/temperature"), 0.7).toDouble());
    m_provider->setOption(QStringLiteral("max_tokens"),
                          s.value(QStringLiteral("params/max_tokens"), 4096).toInt());
    m_provider->setOption(QStringLiteral("timeout_ms"),
                          s.value(QStringLiteral("params/timeout_sec"), 60).toInt() * 1000);

    // ---- Agent 只读能力 ----
    const bool agentOn = s.value(QStringLiteral("agent/enabled"), false).toBool();
    const QString workspace = s.value(QStringLiteral("agent/workspace")).toString();
    m_provider->setOption(QStringLiteral("tools_enabled"), agentOn);
    m_commandPolicy =
        s.value(QStringLiteral("agent/command_policy"), QStringLiteral("off")).toString();
    const bool allowCommand =
        agentOn && !workspace.isEmpty() && m_commandPolicy != QLatin1String("off");

    const bool writeOn = s.value(QStringLiteral("agent/write_enabled"), false).toBool();
    const bool allowWrite = agentOn && !workspace.isEmpty() && writeOn;

    if (agentOn && !workspace.isEmpty()) {
        ToolRegistry::instance().registerDefaults(allowCommand, allowWrite);
        PathGuard::instance().setWorkspace(workspace);
    } else {
        // 关掉时清空工作区，等于彻底关闭文件访问
        PathGuard::instance().setWorkspace(QString());
    }
}

void MainWindow::onExportSession(int sessionId)
{
    const QList<Message> msgs = m_store.messages(sessionId);
    if (msgs.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"),
                                 QStringLiteral("这个会话还没有内容。"));
        return;
    }

    QString title = QStringLiteral("会话");
    for (const auto &info : m_store.sessions()) {
        if (info.id == sessionId) {
            title = info.title;
            break;
        }
    }

    QString safe = title;
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(safe + QStringLiteral(".md"));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出对话"), suggested,
        QStringLiteral("Markdown (*.md);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QString err;
    if (!exportSessionToFile(sessionId, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), err);
        return;
    }

    statusBar()->showMessage(QStringLiteral("已导出到 %1").arg(path), 5000);
}

bool MainWindow::exportSessionToFile(int sessionId, const QString &path, QString *error)
{
    if (sessionId <= 0)
        sessionId = m_currentSessionId;
    if (sessionId <= 0) {
        if (error)
            *error = QStringLiteral("没有可导出的会话");
        return false;
    }

    const QList<Message> msgs = m_store.messages(sessionId);
    if (msgs.isEmpty()) {
        if (error)
            *error = QStringLiteral("这个会话还没有内容");
        return false;
    }

    QString title = QStringLiteral("会话");
    for (const auto &info : m_store.sessions()) {
        if (info.id == sessionId) {
            title = info.title;
            break;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }

    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "# " << title << "\n\n";
    ts << "> 导出自 Egent　·　" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")
       << "\n\n";

    for (const Message &m : msgs) {
        ts << (m.role == Role::User ? "## 你\n\n" : "## Egent\n\n");
        ts << m.content << "\n\n";
    }
    f.close();
    return true;
}

void MainWindow::onExportSessionHtml(int sessionId)
{
    const QList<Message> msgs = m_store.messages(sessionId);
    if (msgs.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"),
                                 QStringLiteral("这个会话还没有内容。"));
        return;
    }

    QString title = QStringLiteral("会话");
    for (const auto &info : m_store.sessions()) {
        if (info.id == sessionId) {
            title = info.title;
            break;
        }
    }

    QString safe = title;
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(safe + QStringLiteral(".html"));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出对话"), suggested,
        QStringLiteral("HTML (*.html);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QString err;
    if (!exportSessionToHtmlFile(sessionId, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), err);
        return;
    }

    statusBar()->showMessage(QStringLiteral("已导出到 %1").arg(path), 5000);
}

bool MainWindow::exportSessionToHtmlFile(int sessionId, const QString &path, QString *error)
{
    if (sessionId <= 0)
        sessionId = m_currentSessionId;
    if (sessionId <= 0) {
        if (error)
            *error = QStringLiteral("没有可导出的会话");
        return false;
    }

    const QList<Message> msgs = m_store.messages(sessionId);
    if (msgs.isEmpty()) {
        if (error)
            *error = QStringLiteral("这个会话还没有内容");
        return false;
    }

    QString title = QStringLiteral("会话");
    for (const auto &info : m_store.sessions()) {
        if (info.id == sessionId) {
            title = info.title;
            break;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }

    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n";
    ts << "<title>" << title.toHtmlEscaped() << "</title>\n";
    ts << MessageBubble::markdownCss() << "\n";
    ts << "<style>\n"
          "body{font-family:-apple-system,'Segoe UI',Roboto,'Microsoft YaHei',sans-serif;"
          "background:#f5f6f8;color:#1b1f28;margin:0;padding:24px;line-height:1.6;}\n"
          ".msg{max-width:760px;margin:12px auto;padding:12px 16px;border-radius:14px;"
          "white-space:pre-wrap;word-wrap:break-word;}\n"
          ".user{background:#e6e9ef;text-align:right;}\n"
          ".assistant{background:#ffffff;border:1px solid #e3e6ea;}\n"
          ".meta{color:#8a8f99;font-size:13px;text-align:center;}\n"
          "</style>\n";
    ts << "</head>\n<body>\n";
    ts << "<h1>" << title.toHtmlEscaped() << "</h1>\n";
    ts << "<p class='meta'>导出自 Egent · "
       << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") << "</p>\n";
    for (const Message &m : msgs)
        ts << MessageBubble::messageFragment(roleToStr(m.role), m.content) << "\n";
    ts << "</body>\n</html>\n";
    f.close();
    return true;
}

void MainWindow::rebuildSkillMenu()
{
    QMenu *menu = m_skillMenuBtn ? m_skillMenuBtn->menu() : nullptr;
    if (!menu)
        return;
    menu->clear();

    const QList<ISkill *> skills = SkillRegistry::instance().skills();
    if (skills.isEmpty()) {
        menu->addAction(QStringLiteral("（暂无技能）"))->setEnabled(false);
        return;
    }
    for (ISkill *s : skills) {
        QAction *act = menu->addAction(QStringLiteral("/%1  %2").arg(s->command(), s->summary()));
        act->setToolTip(s->description());
        act->setStatusTip(s->description());
        connect(act, &QAction::triggered, this, [this, s] {
            if (m_input)
                m_input->setPlainText(QLatin1Char('/') + s->command());
        });
    }
}

void MainWindow::handleSlashCommand(const QString &text)
{
    // text 形如 "/calc 3+4" 或 "/help calc"；仅本地执行，无网络调用。
    const QStringList parts =
        text.mid(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString cmd = parts.isEmpty() ? QString() : parts.first().toLower();
    const QStringList args = parts.mid(1);

    ISkill *skill = SkillRegistry::instance().skillByName(cmd);
    const QString result =
        skill ? skill->execute(args)
              : QStringLiteral("未知命令 /%1，输入 /help 查看可用技能。").arg(cmd);

    // UI 展示（user 原文 + assistant 结果）
    m_chatView->appendMessage(QStringLiteral("user"), text);
    m_chatView->appendMessage(QStringLiteral("assistant"), result);

    // 与普通消息一致地落库（有会话时才写）
    const int sid = ensureSession();
    if (m_store.isOpen() && sid > 0) {
        const int before = m_store.messages(sid).size();
        m_store.appendMessage(sid, Role::User, text, QString(), QString());
        m_store.appendMessage(sid, Role::Assistant, result, QString(), QString());
        if (before == 0) {
            m_store.renameSession(sid, text.left(20));
            reloadSessionList();
        }
    }
    m_input->clear();
}

void MainWindow::onSendClicked()
{
    // 生成中：按钮点击 = 停止（首字可能要等好几秒，必须能中止）
    if (m_streamingMsgIndex >= 0) {
        if (m_agentLoop)
            m_agentLoop->cancel();
        return;
    }

    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() && m_pendingImage.isEmpty())
        return;

    // 技能命令路由：以 / 开头（且非 //）视为本地技能，不走模型
    if (text.startsWith(QLatin1Char('/')) && !text.startsWith(QLatin1String("//"))) {
        handleSlashCommand(text);
        return;
    }

    if (!m_provider)
        return;

    m_messages.append({QStringLiteral("user"), text});
    m_messageImages.append(m_pendingImage);
    m_messages.append({QStringLiteral("assistant"), QString()});
    m_messageImages.append(QString());
    m_streamingMsgIndex = m_messages.size() - 1;
    m_streamBuf.clear();
    m_input->clear();

    // 构造历史：必须包含刚加入的 user 消息，但**不能**包含末尾那个空的 assistant 占位，
    // 否则模型收到的历史里没有本次提问（这是个极易漏掉的顺序问题）。
    QList<Message> history;
    for (int i = 0; i < m_streamingMsgIndex; ++i) {
        // 工具卡片只是给人看的，模型上下文里的 tool 消息由 AgentLoop 自己管
        if (m_messages.at(i).first == QLatin1String("tool"))
            continue;
        history.append({roleFromStr(m_messages.at(i).first), m_messages.at(i).second,
                        m_messageImages.at(i), QString()});
    }

    // ---- 落库：先确保有会话，再写入这条提问 ----
    const int sid = ensureSession();
    if (m_store.isOpen() && sid > 0) {
        const int before = m_store.messages(sid).size();
        m_store.appendMessage(sid, Role::User, text, m_pendingImage, m_pendingImageMime);
        // 新会话的第一条消息顺手当标题
        if (before == 0) {
            const QString title = text.isEmpty()
                ? QStringLiteral("[图片]")
                : text.left(20);
            m_store.renameSession(sid, title);
        }
        reloadSessionList();
    }

    // 增量追加，不整体重建：流式期间只更新最后一个气泡
    m_chatView->appendMessage(QStringLiteral("user"), text, m_pendingImage);
    m_chatView->appendMessage(QStringLiteral("assistant"), QString());

    // 已发出的附件从输入区移除，留出下一次附加的位置
    m_pendingImage.clear();
    m_pendingImageMime.clear();
    updateImagePreview();

    startRequest(history);
}

void MainWindow::onProviderToken(const QString &delta)
{
    m_streamBuf += delta;
    m_streamTimer->start();
}

void MainWindow::flushStream()
{
    if (m_streamingMsgIndex < 0)
        return;
    if (!m_streamBuf.isEmpty()) {
        m_messages[m_streamingMsgIndex].second += m_streamBuf;
        m_streamBuf.clear();
        m_chatView->updateTextAt(m_streamingMsgIndex,
                                 m_messages.at(m_streamingMsgIndex).second);
    }
}

void MainWindow::onProviderFinished()
{
    m_streamTimer->stop();
    flushStream();

    // 流式结束后整条落库（过程中不逐字写，避免写放大）
    if (m_store.isOpen() && m_currentSessionId > 0 && m_streamingMsgIndex >= 0) {
        const QString answer = m_messages.at(m_streamingMsgIndex).second;
        if (!answer.trimmed().isEmpty())
            m_store.appendMessage(m_currentSessionId, Role::Assistant, answer);
        reloadSessionList();
    }

    m_streamingMsgIndex = -1;
    if (m_sendBtn) {
        m_sendBtn->setText(QStringLiteral("发送"));
        m_sendBtn->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    }
}

void MainWindow::onProviderError(const QString &msg)
{
    m_streamTimer->stop();
    if (m_streamingMsgIndex >= 0) {
        m_messages[m_streamingMsgIndex].second = QStringLiteral("⚠ 出错：") + msg;
        m_streamBuf.clear();
        m_streamingMsgIndex = -1;
        m_chatView->updateTextAt(m_streamingMsgIndex >= 0 ? m_streamingMsgIndex
                                                         : (m_messages.size() - 1),
                                 m_messages.at(m_messages.size() - 1).second);
    }
    if (m_sendBtn) {
        m_sendBtn->setText(QStringLiteral("发送"));
        m_sendBtn->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    }
}

void MainWindow::onGenerateImage()
{
    if (!m_provider)
        return;

    bool ok = false;
    const QString prompt = QInputDialog::getText(
        this, QStringLiteral("生成图片"),
        QStringLiteral("描述你想生成的画面（中文或英文均可）："),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || prompt.trimmed().isEmpty())
        return;

    QSettings s(QStringLiteral("egent"), QStringLiteral("egent"));
    const QString wsId = s.value(QStringLiteral("image/workspace_id")).toString().trimmed();
    statusBar()->showMessage(QStringLiteral("正在生成图片…"));
    m_provider->generateImage(prompt.trimmed(), wsId);
}

void MainWindow::onImageReady(const QByteArray &imageData, const QString &prompt)
{
    statusBar()->clearMessage();

    QPixmap image;
    if (!image.loadFromData(imageData)) {
        QMessageBox::warning(this, QStringLiteral("生成失败"),
                             QStringLiteral("图片解码失败。"));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("生成结果"));
    dlg.setModal(true);

    auto *imgLabel = new QLabel(&dlg);
    imgLabel->setPixmap(image.scaledToWidth(480, Qt::SmoothTransformation));
    imgLabel->setAlignment(Qt::AlignCenter);

    auto *saveBtn = new QPushButton(QStringLiteral("保存图片…"), &dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(saveBtn, &QPushButton::clicked, [&dlg, &image]() {
        const QString path = QFileDialog::getSaveFileName(
            &dlg, QStringLiteral("保存图片"), QString(),
            QStringLiteral("PNG 图片 (*.png);;所有文件 (*)"));
        if (!path.isEmpty())
            image.save(path, "PNG");
    });

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(closeBtn);

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(imgLabel);
    root->addWidget(new QLabel(prompt, &dlg));
    root->addLayout(btnRow);
    dlg.exec();
}

void MainWindow::onImageError(const QString &message)
{
    statusBar()->clearMessage();
    QMessageBox::warning(this, QStringLiteral("生成失败"), message);
}

void MainWindow::renderChat()
{
    // 气泡与头像的颜色全部由 QSS 动态属性控制，
    // 主题切换时整体重建一次，历史消息就会一起换色。
    m_chatView->rebuild(m_messages, m_messageImages);
}
