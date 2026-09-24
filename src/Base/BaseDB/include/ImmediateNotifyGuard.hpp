#pragma once

/**
 * @brief ImmediateNotifyGuard - 立即通知模式 RAII 守卫
 *
 * 在 Guard 生命周期内，强制所有 DocumentManager 通知都立即处理，
 * 不进行批处理。析构时自动恢复原有模式。
 *
 * 使用示例：
 * @code
 * {
 *     ImmediateNotifyGuard guard;  // 启用立即模式
 *     actor->setPosition(newPos);  // 立即通知
 *     actor->setOpacity(0.5f);     // 立即通知
 * }  // 自动恢复批处理模式
 * @endcode
 */
class ImmediateNotifyGuard {
public:
    /**
     * @brief 构造函数 - 启用立即通知模式
     */
    ImmediateNotifyGuard();

    /**
     * @brief 析构函数 - 恢复之前的通知模式
     */
    ~ImmediateNotifyGuard();

    /**
     * @brief 检查当前是否处于强制立即通知模式
     * @return true 如果当前强制立即通知
     */
    static bool isForceImmediate();

    // 禁止拷贝和移动
    ImmediateNotifyGuard(const ImmediateNotifyGuard&) = delete;
    ImmediateNotifyGuard& operator=(const ImmediateNotifyGuard&) = delete;
    ImmediateNotifyGuard(ImmediateNotifyGuard&&) = delete;
    ImmediateNotifyGuard& operator=(ImmediateNotifyGuard&&) = delete;

private:
    bool m_previousMode;  // 保存之前的模式状态

    // 使用 thread_local 确保线程安全
    static thread_local bool s_forceImmediate;
};

/**
 * @brief 便利宏，用于在代码块内启用立即通知模式
 *
 * 使用示例：
 * @code
 * IMMEDIATE_NOTIFY_SCOPE {
 *     // 这个作用域内的所有通知都立即处理
 *     updateActorProperties();
 * }
 * @endcode
 */
#define IMMEDIATE_NOTIFY_SCOPE \
    if (ImmediateNotifyGuard guard; true)