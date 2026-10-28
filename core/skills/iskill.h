#pragma once

#include <QString>
#include <QStringList>

/**
 * 技能接口（核心技能系统，P2.1）。
 *
 * 技能与工具（ITool）的区别：
 *  - ITool 由 Agent 模型在对话循环中按需调用，参数来自模型 JSON；
 *  - ISkill 由用户以「/命令」形式直接触发，参数来自命令行，结果直接以文本回显。
 *
 * 技能实现必须是纯文本往返：执行 → 返回结果字符串，不直接操作 UI、
 * 不直接访问网络（需要时可走 ProviderRegistry / 文件工具），保证 core 层
 * 可独立单测，也可被插件经 PluginContext 注册进同一注册表。
 */
class ISkill
{
public:
    virtual ~ISkill() = default;

    /** 命令名（不含前导 '/'，全小写），例如 "help"、"time"、"calc" */
    virtual QString command() const = 0;

    /** 一行摘要，用于 /命令菜单 */
    virtual QString summary() const = 0;

    /** 详细说明，用于 /help 与 /命令菜单 */
    virtual QString description() const = 0;

    /**
     * 执行技能并返回结果文本。
     * @param args 去掉命令本身后的完整参数（例如 "/calc 1+2*3" 得到 {"1+2*3"}）
     * @return 供 UI 展示的结果；执行失败时返回以「错误：」开头的文本亦可。
     */
    virtual QString execute(const QStringList &args) = 0;
};