// M1 验收：真实调用百炼接口，验证 HTTPS 握手 + SSE 流式 + 中文编码。
// 用法：DASHSCOPE_API_KEY 已在环境变量中，直接运行即可。
// 退出码 0 = 通过，1 = 失败。

#include "core/providers/openaicompatprovider.h"
#include "core/providers/providerregistry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (qgetenv("DASHSCOPE_API_KEY").isEmpty()) {
        qWarning().noquote() << "FAIL 环境变量 DASHSCOPE_API_KEY 为空";
        return 1;
    }

    OpenAiCompatProvider provider(ProviderRegistry::bailianConfig());

    QStringList deltas;
    QString error;
    bool finished = false;
    bool timedOut = false;

    QElapsedTimer clock;
    qint64 firstTokenMs = -1;

    QObject::connect(&provider, &IModelProvider::tokenReceived,
                     [&](const QString &d) {
                         if (firstTokenMs < 0)
                             firstTokenMs = clock.elapsed();
                         deltas << d;
                     });
    QObject::connect(&provider, &IModelProvider::finished, [&]() {
        finished = true;
        QCoreApplication::quit();
    });
    QObject::connect(&provider, &IModelProvider::errorOccurred,
                     [&](const QString &m) {
                         error = m;
                         QCoreApplication::quit();
                     });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&]() {
        timedOut = true;
        QCoreApplication::quit();
    });

    QList<Message> history;
    history.append({Role::User,
                    QStringLiteral("用一句话解释什么是原子操作，不要超过二十个字。")});

    // 失败场景开关：
    //   --cancel   1.5 秒后主动中止，验证「取消不报错、仍能收尾」
    //   --badkey   用假 Key 触发 401，验证错误文案被翻译
    //   --timeout  把超时压到 1ms，验证超时文案
    const QString mode = (argc > 1) ? QString::fromLatin1(argv[1]) : QString();
    const bool cancelMode = (mode == QLatin1String("--cancel"));
    const bool badKeyMode = (mode == QLatin1String("--badkey"));
    const bool shortTimeoutMode = (mode == QLatin1String("--timeout"));
    const bool expectFailure = badKeyMode || shortTimeoutMode;

    if (badKeyMode)
        qputenv("DASHSCOPE_API_KEY", "sk-invalid-for-test");
    if (shortTimeoutMode)
        provider.setOption(QStringLiteral("timeout_ms"), 1);

    qInfo().noquote() << QStringLiteral("发送请求 model=qwen3.8-flash %1 ...")
                             .arg(mode.isEmpty() ? QString() : ("(" + mode + ")"));
    clock.start();
    timeout.start(45000);
    provider.send(history, QStringLiteral("qwen3.8-flash"));

    if (cancelMode) {
        QTimer::singleShot(1500, [&provider]() { provider.cancel(); });
    }

    QCoreApplication::exec();

    const qint64 totalMs = clock.elapsed();
    const QString text = deltas.join(QString());

    qInfo().noquote() << "";
    qInfo().noquote() << "---------- 结果 ----------";
    qInfo().noquote() << QStringLiteral("状态      : %1")
                             .arg(timedOut ? "超时" : (error.isEmpty() ? "完成" : "出错"));
    if (!error.isEmpty())
        qInfo().noquote() << QStringLiteral("错误信息  : %1").arg(error);
    qInfo().noquote() << QStringLiteral("分片数    : %1").arg(deltas.size());
    qInfo().noquote() << QStringLiteral("首字延迟  : %1 ms").arg(firstTokenMs);
    qInfo().noquote() << QStringLiteral("总耗时    : %1 ms").arg(totalMs);
    qInfo().noquote() << QStringLiteral("回答内容  : %1").arg(text);

    // ---- 判定 ----
    int fail = 0;
    auto check = [&fail](bool cond, const char *what, const QString &detail = QString()) {
        if (cond) {
            qInfo().noquote() << "  PASS " << what;
        } else {
            ++fail;
            qWarning().noquote() << "  FAIL " << what
                                 << (detail.isEmpty() ? QString() : ("  -> " + detail));
        }
    };

    qInfo().noquote() << "";
    qInfo().noquote() << "---------- 判定 ----------";
    if (!expectFailure) {
        check(error.isEmpty(), "无网络错误（HTTPS/Schannel 握手通过）");
        check(!timedOut, "未超时");
        check(finished, "收到 finished 信号");
    }
    if (!cancelMode && !expectFailure) {
        // 取消模式下 1.5s 内首字通常还没到，不能要求有内容
        check(deltas.size() > 1, "确实是流式（分片数 > 1）");
        check(!text.trimmed().isEmpty(), "拿到了回答内容");
    }
    check(!text.contains(QChar(0xFFFD)), "无 U+FFFD 替换字符（中文未被截断）");

    if (badKeyMode) {
        check(error.contains(QStringLiteral("API Key")),
              "401 被翻译成「API Key 无效」", error);
        check(!finished, "401 只报错，不发 finished");
    }
    if (shortTimeoutMode) {
        check(error.contains(QStringLiteral("超时")),
              "超时被翻译成「请求超时」", error);
    }
    if (cancelMode) {
        check(error.isEmpty(), "取消不产生 errorOccurred（不弹错误给用户）");
        check(finished, "取消后仍收到 finished（界面能退出「生成中」）");
        check(totalMs < 20000, "取消确实提前结束了请求");
    }

    qInfo().noquote() << QStringLiteral("\n===== %1 =====").arg(fail == 0 ? "M1 验收通过" : "存在失败项");
    return fail == 0 ? 0 : 1;
}
