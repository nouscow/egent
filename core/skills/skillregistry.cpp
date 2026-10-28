#include "core/skills/skillregistry.h"

#include "core/skills/builtinskills.h"

#include <QList>
#include <QStringList>

SkillRegistry &SkillRegistry::instance()
{
    static SkillRegistry reg;
    return reg;
}

SkillRegistry::~SkillRegistry()
{
    qDeleteAll(m_skills);
}

void SkillRegistry::registerSkill(ISkill *skill)
{
    if (!skill)
        return;
    const QString key = skill->command().toLower();
    delete m_skills.take(key);
    m_skills.insert(key, skill);
}

void SkillRegistry::unregisterSkill(const QString &command)
{
    delete m_skills.take(command.toLower());
}

ISkill *SkillRegistry::skillByName(const QString &command) const
{
    return m_skills.value(command.toLower(), nullptr);
}

QList<ISkill *> SkillRegistry::skills() const
{
    QList<ISkill *> out;
    for (ISkill *s : std::as_const(m_skills))
        out.append(s);
    std::sort(out.begin(), out.end(), [](const ISkill *a, const ISkill *b) {
        return a->command() < b->command();
    });
    return out;
}

QStringList SkillRegistry::commands() const
{
    QStringList out;
    for (ISkill *s : skills())
        out.append(QLatin1Char('/') + s->command());
    return out;
}

bool SkillRegistry::contains(const QString &command) const
{
    return m_skills.contains(command.toLower());
}

void SkillRegistry::reloadBuiltins()
{
    qDeleteAll(m_skills);
    m_skills.clear();
    registerBuiltins();
}

void SkillRegistry::registerBuiltins()
{
    registerSkill(new HelpSkill);
    registerSkill(new TimeSkill);
    registerSkill(new CalcSkill);
}