#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/models.h"
#include "core/sessionstore.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class AgentLoop;
class ChatView;
class QCheckBox;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QTimer;
class IModelProvider;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public:
    /**
     * 导出会话为 Markdown。sessionId <= 0 表示当前会话。
     * 抽成 public 是为了让自动化验证能绕过文件对话框直接落盘。
     */
    bool exportSessionToFile(int sessionId, const QString &path, QString *error = nullptr);
    /**
     * 导出会话为 HTML（P2.3）。sessionId <= 0 表示当前会话。
     */
    bool exportSessionToHtmlFile(int sessionId, const QString &path, QString *error = nullptr);

private slots:
    void onThemeChanged(const QString &id);
    void onThemeSelected(int index);
    void onSendClicked();
    void openSettings();
    void onProviderSelected(int index);
    /** 模型切换：持久化到 QSettings，重启后记住选择 */
    void onModelSelected(int index);
    void onCopyMessage(const QString &text);
    void onToolStarted(const QString &name, const QString &argsJson);
    void onToolFinished(const QString &name, bool ok, const QString &result);
    void onPermissionRequired(const QString &toolName, const QString &detail);
    void onRegenerateMessage(int index);
    void onEditMessage(int index);
    void onNewSession();
    void onSessionActivated(QListWidgetItem *item);
    void onSessionContextMenu(const QPoint &pos);
    void onExportSession(int sessionId);
    void onExportSessionHtml(int sessionId);
    void onProviderToken(const QString &delta);
    void onProviderFinished();
    void onProviderError(const QString &msg);
    void flushStream();

    /** 工具栏「生成图片」：弹窗取提示词 → 调 provider 生图 */
    void onGenerateImage();
    /** 生图成功：用对话框展示结果（可保存） */
    void onImageReady(const QByteArray &imageData, const QString &prompt);
    /** 生图失败 */
    void onImageError(const QString &message);

private:
    void buildUi();
    /** 用当前主题令牌重新渲染整个对话区（切换主题时旧内容也要变色） */
    void renderChat();
    /** 把设置里的采样参数 / 超时下发给 provider */
    void applyParamsFromSettings();
    /** 按当前厂商刷新模型下拉 */
    void refreshModelList();
    /** 真正发起请求（发送与重新生成共用） */
    void startRequest(const QList<Message> &history);
    /** M4 快捷键系统：集中注册所有全局快捷键（新会话 / 切模型 / 停止 / 帮助…） */
    void setupShortcuts();
    /** 展示快捷键帮助 */
    void showShortcutsHelp();
    /** 刷新左栏额度显示（额度为本机手动记录，跨月清零） */
    void refreshQuotaLabel();

    /** 重建左侧会话列表 */
    void reloadSessionList();
    /** 切到指定会话（重新从库里读消息并渲染） */
    void loadSession(int sessionId);
    /** 确保有一个可用会话，返回其 id */
    int ensureSession();

    Ui::MainWindow *ui;
    SessionStore m_store;
    QListWidget *m_sessionList = nullptr;
    int m_currentSessionId = -1;
    ChatView *m_chatView = nullptr;
    AgentLoop *m_agentLoop = nullptr;

    // 命令执行策略与「本会话已批准」标记
    QString m_commandPolicy = QStringLiteral("off");
    bool m_commandApprovedOnce = false;
    QPlainTextEdit *m_input = nullptr;
    QComboBox *m_themeBox = nullptr;
    QComboBox *m_providerBox = nullptr;
    QComboBox *m_modelBox = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QCheckBox *m_thinkingBox = nullptr;
    class QLabel *m_quotaLabel = nullptr;

    /** 本地演示消息：M2 接入真实接口后由会话模型替换 */
    QList<QPair<QString, QString>> m_messages; // (role, text)
    /** 与 m_messages 一一对应：每条消息的图片附件路径（无图为空串），用于重建/请求体 */
    QList<QString> m_messageImages;

    /** 输入区待发送的图片附件（未发送前可预览/移除） */
    QString m_pendingImage;
    QString m_pendingImageMime;
    QPushButton *m_imageBtn = nullptr;
    QToolButton *m_imagePreview = nullptr;

    void onPickImage();
    void onClearImage();
    void updateImagePreview();
    /** 按图片扩展名推断 MIME（无法识别时回退 image/png） */
    static QString guessImageMime(const QString &path);

    IModelProvider *m_provider = nullptr;     // 当前 provider（M1 仅 BailianProvider）
    QTimer *m_streamTimer = nullptr;          // 流式渲染节流（50ms）
    QString m_streamBuf;                      // 待刷入的增量文本
    int m_streamingMsgIndex = -1;             // 正在流式输出的助手消息下标
};
#endif // MAINWINDOW_H
