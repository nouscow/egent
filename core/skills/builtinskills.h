#pragma once

#include "core/skills/iskill.h"

/** /help —— 列出全部可用技能（由注册表驱动，天然跟随插件动态增删） */
class HelpSkill : public ISkill
{
public:
    QString command() const override { return QStringLiteral("help"); }
    QString summary() const override { return QStringLiteral("列出所有可用技能"); }
    QString description() const override
    {
        return QStringLiteral("展示全部已注册技能的命令、摘要与说明。");
    }
    QString execute(const QStringList &args) override;
};

/** /time —— 当前日期与时间 */
class TimeSkill : public ISkill
{
public:
    QString command() const override { return QStringLiteral("time"); }
    QString summary() const override { return QStringLiteral("显示当前日期与时间"); }
    QString description() const override
    {
        return QStringLiteral("返回本机当前日期、时间与星期。");
    }
    QString execute(const QStringList &args) override;
};

/** /calc —— 安全四则运算求值（支持 + - * / % 与括号） */
class CalcSkill : public ISkill
{
public:
    QString command() const override { return QStringLiteral("calc"); }
    QString summary() const override { return QStringLiteral("四则运算求值，如 /calc (1+2)*3"); }
    QString description() const override
    {
        return QStringLiteral(
            "计算算术表达式，支持 + - * / % 与括号、小数，例如 /calc (1.5+2)*4。");
    }
    QString execute(const QStringList &args) override;
};