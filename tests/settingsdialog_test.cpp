// SettingsDialog「恢复默认参数」按钮自测：需要 QApplication（offscreen）。
// 退出码 0 = 全部通过。
//
// 验证点：点「恢复默认参数」后，三个采样参数应回到各自默认值
// （temperature 0.7 / max_tokens 4096 / 超时 60），不论点之前被改成了什么。

#include "ui/settingsdialog.h"
#include "core/providers/providerregistry.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDebug>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char *what, const QString &detail = QString())
{
    if (cond) {
        ++g_pass;
        qInfo().noquote() << "  PASS" << what;
    } else {
        ++g_fail;
        qWarning().noquote() << "  FAIL" << what
                             << (detail.isEmpty() ? QString() : ("  -> " + detail));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // 与真实程序一致：先注册厂商，loadFromSettings 才会读到当前厂商配置
    ProviderRegistry::instance().registerBuiltins();

    SettingsDialog dlg;

    auto *temp = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("temperature"));
    auto *maxT = dlg.findChild<QSpinBox *>(QStringLiteral("maxTokens"));
    auto *to = dlg.findChild<QSpinBox *>(QStringLiteral("timeoutSec"));
    check(temp && maxT && to, "三个参数控件均存在（可按 objectName 定位）");
    if (!temp || !maxT || !to)
        return 1;

    // 先改到非默认值
    temp->setValue(1.5);
    maxT->setValue(123);
    to->setValue(99);
    check(qAbs(temp->value() - 1.5) < 1e-6
              && maxT->value() == 123 && to->value() == 99,
          "改动生效（改到了非默认值）");

    // 找到「恢复默认参数」按钮并点击
    QPushButton *reset = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("恢复默认参数")) {
            reset = b;
            break;
        }
    }
    check(reset != nullptr, "找到「恢复默认参数」按钮");
    if (reset)
        reset->click();

    // 验证回到默认值
    check(qAbs(temp->value() - 0.7) < 1e-6, "temperature 回到 0.7",
          QString::number(temp->value()));
    check(maxT->value() == 4096, "max_tokens 回到 4096", QString::number(maxT->value()));
    check(to->value() == 60, "超时回到 60 秒", QString::number(to->value()));

    qInfo().noquote() << QStringLiteral("\n结果: %1 通过 / %2 失败").arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}
