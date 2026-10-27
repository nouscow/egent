#pragma once

#include "core/tools/itool.h"

/**
 * L4 文件写入工具 —— 全项目风险最高的一级能力。
 *
 * 三重约束：
 *  1. requiresConfirmation() 恒 true，且界面向用户展示**写入后的完整内容**
 *  2. 路径必须落在 PathGuard 工作区内（越界直接拒绝）
 *  3. 单次写入上限 1MB，避免误写超大内容把磁盘塞满
 *
 * 只做「创建 / 覆盖」，不做删除 —— 删文件没有撤销，不提供。
 */
class WriteFileTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("write_file"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    ToolResult execute(const QJsonObject &args) override;
    bool requiresConfirmation() const override { return true; }

    static constexpr int kMaxWriteBytes = 1024 * 1024;
};
