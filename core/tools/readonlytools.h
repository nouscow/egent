#pragma once

#include "core/tools/itool.h"

/**
 * L2 只读工具集。
 *
 * 三个都只读，且都强制经过 PathGuard：
 *   list_dir     列目录
 *   read_file    读文本文件（有大小上限、二进制拦截）
 *   search_text  在工作区里搜文本
 *
 * 输出长度都有硬上限——模型的上下文按 token 计费，一次塞进去一整个文件
 * 既贵又容易把有用的上下文挤掉。
 */

class ListDirTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("list_dir"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
};

class ReadFileTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("read_file"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
};

class SearchTextTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("search_text"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
};

namespace ToolLimits {
constexpr qint64 kMaxReadBytes = 256 * 1024; // 单次读文件上限
constexpr int kMaxDirEntries = 200;          // 单次列目录最多返回条数
constexpr int kMaxSearchResults = 40;        // 搜索最多返回条数
constexpr qint64 kMaxSearchFileBytes = 1024 * 1024; // 搜索时跳过超过 1MB 的文件
} // namespace ToolLimits
