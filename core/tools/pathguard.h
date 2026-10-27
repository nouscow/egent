#pragma once

#include <QString>

/**
 * 工作区路径守卫（单例）。
 *
 * 所有 Agent 工具的路径参数都必须先过这里，这是 L2 只读能力的安全边界。
 * 三条规则：
 *   1. 未设置工作区 → 拒绝一切访问（安全默认值）
 *   2. 目标必须落在工作区目录之内（前缀校验，Windows 不区分大小写）
 *   3. 先解符号链接再比较，防止 `link -> C:\` 这类跳出
 */
class PathGuard
{
public:
    static PathGuard &instance();

    /** 设置工作区根目录；传空或非目录等于关闭 Agent 文件访问 */
    void setWorkspace(const QString &root);
    QString workspace() const { return m_root; }
    bool enabled() const { return !m_root.isEmpty(); }

    /**
     * 校验用户/模型给的路径，返回规范化后的绝对路径。
     * 不允许时返回空字符串，并通过 error 说明原因。
     */
    QString resolve(const QString &userPath, QString *error = nullptr) const;

private:
    PathGuard() = default;

    QString m_root; // 统一用 '/' 分隔，且为 canonical 形式
};
