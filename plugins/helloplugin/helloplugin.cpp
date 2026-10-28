#include "helloplugin.h"

#include "core/plugins/plugincontext.h"
#include "core/skills/iskill.h"
#include "core/skills/skillregistry.h"

namespace {

/** 由示例插件提供的 /hello 技能（纯文本往返，符合 ISkill 约束） */
class HelloSkill : public ISkill
{
public:
    QString command() const override { return QStringLiteral("hello"); }
    QString summary() const override { return QStringLiteral("示例插件技能：打个招呼"); }
    QString description() const override
    {
        return QStringLiteral("/hello [名字] —— 返回插件演示问候语，未传名字时显示插件数据目录。");
    }
    QString execute(const QStringList &args) override
    {
        if (!args.isEmpty() && !args.first().isEmpty())
            return QStringLiteral("你好，%1！我是 Hello 插件（v1.0.0）。").arg(args.first());
        return QStringLiteral("你好！我是由示例插件注册的技能（/hello [名字] 可带称呼）。");
    }
};

} // namespace

QString HelloPlugin::install(PluginContext *ctx)
{
    if (!ctx || !ctx->skillRegistry())
        return QStringLiteral("上下文注入口不完整（缺少 SkillRegistry）");

    m_ctx = ctx;
    m_ctx->setLogFn([](const QString &, const QString &) {}); // 使用默认日志即可

    ctx->skillRegistry()->registerSkill(new HelloSkill());
    m_installed = true;

    ctx->log(QStringLiteral("info"),
             QStringLiteral("已注册 /hello 技能，数据目录: %1").arg(ctx->dataDir()));
    return {};
}

void HelloPlugin::uninstall()
{
    if (m_installed && m_ctx && m_ctx->skillRegistry())
        m_ctx->skillRegistry()->unregisterSkill(QStringLiteral("hello"));
    m_installed = false;
    m_ctx = nullptr;
}