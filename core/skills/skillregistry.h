#pragma once

#include "core/skills/iskill.h"

#include <QMap>
#include <QList>
#include <QStringList>

/**
 * 技能注册表（单例，P2.1/P2.2）。
 *
 * - 全局共享：主程序与插件共享同一注册表（插件经 PluginContext 传入本实例指针）。
 * - 按命令名登记，命令名大小写不敏感（统一转小写比较）。
 * - 同名命令视为插件覆盖内置/已注册技能：后注册者胜，便于插件定制行为。
 */
class SkillRegistry
{
public:
    static SkillRegistry &instance();

    ~SkillRegistry();

    /** 注册技能并接管所有权；同名命令后注册者覆盖先注册者 */
    void registerSkill(ISkill *skill);

    /** 注销指定命令（插件卸载时调用），不存在则静默忽略 */
    void unregisterSkill(const QString &command);

    /** 按命令名查找（大小写不敏感），未命中返回 nullptr */
    ISkill *skillByName(const QString &command) const;

    /** 全部技能，按命令名字母序排序 */
    QList<ISkill *> skills() const;

    /** 全部命令名列表（带 '/' 前缀），按字母序 */
    QStringList commands() const;

    /** 命令是否存在 */
    bool contains(const QString &command) const;

    /** 清空并重新注册内置技能（测试 / 恢复基线时用） */
    void reloadBuiltins();

    /** 注册全部内置技能 */
    void registerBuiltins();

private:
    SkillRegistry() = default;
    Q_DISABLE_COPY_MOVE(SkillRegistry)

    QMap<QString, ISkill *> m_skills;
};