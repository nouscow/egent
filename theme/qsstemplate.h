#pragma once

#include <QString>
#include <QStringList>
#include <functional>

/**
 * QSS 模板渲染：把 {{令牌}} 替换成实际色值。
 *
 * 刻意不依赖 ThemeManager，只吃一个 lookup 回调，
 * 这样单元测试可以喂假数据，不需要起 QApplication。
 */
namespace QssTemplate {

using Lookup = std::function<QString(const QString &token)>;

struct RenderResult
{
    QString qss;
    /** 模板里出现但 lookup 没给出的令牌名（已去重排序） */
    QStringList unknownTokens;

    bool hasUnknown() const { return !unknownTokens.isEmpty(); }
};

RenderResult render(const QString &tpl, const Lookup &lookup);

} // namespace QssTemplate
