// skill_test.cpp —— 核心技能系统离线单测（P2.1/P2.2/P2.4）
// 覆盖：注册表注册/查询/排序、内置三技能执行正确性、未知命令、同名覆盖。
#include "core/skills/skillregistry.h"
#include "core/skills/iskill.h"

#include <QCoreApplication>
#include <QDebug>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool cond, const char *msg)
{
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "[FAIL] %s\n", msg);
    } else {
        std::printf("[PASS] %s\n", msg);
    }
}

/** 测试用假技能：仅用于同名覆盖与 /命令菜单契约验证 */
class EchoSkill : public ISkill
{
public:
    QString command() const override { return QStringLiteral("echo"); }
    QString summary() const override { return QStringLiteral("回显参数"); }
    QString description() const override { return QStringLiteral("把参数原样返回。"); }
    QString execute(const QStringList &args) override
    {
        return QStringLiteral("echo: ") + args.join(QLatin1Char(' '));
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    SkillRegistry &reg = SkillRegistry::instance();
    reg.reloadBuiltins();

    // 1. 注册表基础行为
    check(reg.contains(QStringLiteral("help")), "内置技能已注册：help");
    check(reg.contains(QStringLiteral("time")), "内置技能已注册：time");
    check(reg.contains(QStringLiteral("calc")), "内置技能已注册：calc");
    check(!reg.contains(QStringLiteral("nope")), "未注册命令不存在");
    check(!reg.skillByName(QStringLiteral("NOPE")), "skillByName 未知命令返回 nullptr");
    check(reg.skillByName(QStringLiteral("HELP")) != nullptr, "skillByName 大小写不敏感");

    // 2. 命令列表按字母序、带前缀
    const QStringList cmds = reg.commands();
    check(cmds == QStringList{QStringLiteral("/calc"), QStringLiteral("/help"),
                              QStringLiteral("/time")},
          "commands() 带 / 前缀且按字母序");

    // 3. calc 技能正确性
    ISkill *calc = reg.skillByName(QStringLiteral("calc"));
    check(calc != nullptr, "取得 calc 技能");
    check(calc->execute({QStringLiteral("1+2*3")}) == QStringLiteral("7"),
          "/calc 1+2*3 = 7");
    check(calc->execute({QStringLiteral("(1.5+2)*4")}) == QStringLiteral("14"),
          "/calc (1.5+2)*4 = 14");
    check(calc->execute({QStringLiteral("10%3")}) == QStringLiteral("1"),
          "/calc 10%3 = 1");
    check(calc->execute({QStringLiteral("1/0")}).startsWith(QStringLiteral("错误")),
          "/calc 1/0 报除零错误");
    check(calc->execute({QStringLiteral("2+")}).startsWith(QStringLiteral("错误")),
          "/calc 表达式不完整报错");
    check(calc->execute({}).contains(QStringLiteral("用法")), "/calc 无参提示用法");

    // 4. time 技能
    ISkill *timeSkill = reg.skillByName(QStringLiteral("time"));
    const QString ts = timeSkill->execute({});
    check(!ts.isEmpty() && ts.contains(QLatin1Char('-')) && ts.contains(QLatin1Char(':')),
          "/time 输出含日期与时间");

    // 5. help 技能聚合
    ISkill *help = reg.skillByName(QStringLiteral("help"));
    const QString helpText = help->execute({});
    check(helpText.contains(QStringLiteral("/calc")) && helpText.contains(QStringLiteral("/time"))
              && helpText.contains(QStringLiteral("/help")),
          "/help 聚合列出全部技能");
    const QString helpDetail = help->execute({QStringLiteral("calc")});
    check(helpDetail.contains(QStringLiteral("/calc")) && helpDetail.contains(QStringLiteral("四则")),
          "/help calc 显示详细说明");
    check(help->execute({QStringLiteral("nope")}).contains(QStringLiteral("没有名为")),
          "/help 未知技能给出提示");

    // 6. 同名覆盖（插件语义）
    reg.registerSkill(new EchoSkill);
    check(reg.contains(QStringLiteral("echo")), "动态注册 echo 成功");
    check(reg.skillByName(QStringLiteral("ECHO"))->execute({QStringLiteral("a"), QStringLiteral("b")})
              == QStringLiteral("echo: a b"),
          "echo 执行回显");
    reg.unregisterSkill(QStringLiteral("echo"));

    // 7. 卸载后恢复基线
    reg.reloadBuiltins();
    check(reg.commands().size() == 3, "reloadBuiltins 恢复内置基线（3 个）");

    if (g_failures > 0) {
        std::fprintf(stderr, "skill_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("skill_test: all passed\n");
    return 0;
}