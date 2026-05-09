#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "i_system.h"
#include "i_system_context.h"

namespace Corona::Kernel {

/**
 * @brief 系统基类
 *
 * SystemBase 提供了 ISystem 接口的默认实现，包含线程管理、帧率控制和性能统计。
 *
 * 核心功能：
 * - 线程管理：自动管理系统的工作线程生命周期
 * - 帧率控制：根据 target_fps 自动节流，避免 CPU 过载
 * - 暂停/恢复：支持运行时暂停和恢复系统
 * - 性能统计：自动收集 FPS、帧时间等指标
 * - Delta Time：计算并提供帧间时间差
 *
 * 派生类只需实现：
 * - get_name() - 系统名称
 * - get_priority() - 初始化优先级
 * - initialize() - 初始化逻辑
 * - update() - 每帧更新逻辑
 * - shutdown() - 清理逻辑
 *
 * 使用示例：
 * @code
 * class PhysicsSystem : public SystemBase {
 * public:
 *     PhysicsSystem() {
 *         set_target_fps(120);  // 物理系统运行在 120 FPS
 *     }
 *
 *     std::string_view get_name() const override { return "Physics"; }
 *     int get_priority() const override { return 90; }
 *
 *     bool initialize(ISystemContext* ctx) override {
 *         // 初始化物理引擎
 *         world_ = create_physics_world();
 *         return world_ != nullptr;
 *     }
 *
 *     void update() override {
 *         float dt = delta_time();  // 获取帧间隔
 *         world_->step(dt);         // 更新物理模拟
 *     }
 *
 *     void shutdown() override {
 *         destroy_physics_world(world_);
 *     }
 *
 * private:
 *     PhysicsWorld* world_ = nullptr;
 * };
 * @endcode
 */
class SystemBase : public ISystem {
   public:
    /**
     * @brief 构造函数
     *
     * 初始化系统状态为 idle，默认目标帧率 60 FPS
     */
    SystemBase();

    /**
     * @brief 析构函数
     *
     * 如果线程仍在运行，自动调用 stop() 确保资源释放
     */
    virtual ~SystemBase();

    // ========================================
    // ISystem 接口实现
    // ========================================

    SystemState get_state() const override;

    int get_target_fps() const override;

    /**
     * @brief 设置目标帧率
     * @param fps 目标 FPS，0 表示不限制
     */
    void set_target_fps(int fps);

    void start() override;

    void pause() override;

    void resume() override;

    void stop() override;

    // ========================================
    // 性能统计接口实现
    // ========================================

    float get_actual_fps() const override;

    float get_average_frame_time() const override;

    float get_max_frame_time() const override;

    std::uint64_t get_total_frames() const override;

    void reset_stats() override;

   protected:
    // ========================================
    // 派生类辅助方法
    // ========================================

    /**
     * @brief 获取系统上下文
     * @return 系统上下文指针
     *
     * 在 initialize() 后可用，提供对内核服务的访问
     */
    ISystemContext* context();

    const ISystemContext* context() const;

    /**
     * @brief 获取当前帧号
     * @return 系统启动以来的帧序号（从 0 开始）
     */
    uint64_t frame_number() const;

    /**
     * @brief 获取上一帧的时间间隔
     * @return 距上一帧的时间差（秒）
     *
     * 在 update() 中使用，用于时间相关的计算（如物理模拟、动画）
     */
    float delta_time() const;

    /**
     * @brief 线程循环函数
     *
     * 默认实现：
     * 1. 检查暂停状态
     * 2. 计算 delta_time
     * 3. 调用 update()
     * 4. 收集性能统计
     * 5. 根据 target_fps 休眠节流
     */
    void thread_loop();

    /**
     * @brief 线程启动回调
     *
     * 在工作线程循环开始前调用。派生类可覆盖以执行线程相关的初始化。
     */
    virtual void on_thread_started();

    /**
     * @brief 线程停止回调
     *
     * 在工作线程循环结束后调用。派生类可覆盖以执行线程相关的清理。
     */
    virtual void on_thread_stopped();

   private:
    friend class SystemManager;  ///< 允许 SystemManager 设置 context

    /**
     * @brief 设置系统上下文（由 SystemManager 调用）
     * @param context 系统上下文指针
     */
    void set_context(ISystemContext* context);

    // ========================================
    // 内部状态
    // ========================================

    std::atomic<SystemState> state_;    ///< 系统状态
    std::atomic<bool> should_run_;      ///< 线程运行标志
    std::atomic<bool> is_paused_;       ///< 暂停标志
    std::thread thread_;                ///< 工作线程
    std::mutex control_mutex_;          ///< 保护 start/stop 操作
    std::mutex pause_mutex_;            ///< 保护暂停状态
    std::condition_variable pause_cv_;  ///< 暂停条件变量

    ISystemContext* context_;  ///< 系统上下文
    int target_fps_;           ///< 目标帧率
    uint64_t frame_number_;    ///< 当前帧号
    float last_delta_time_;    ///< 上一帧时间间隔（秒）

    // 性能统计
    mutable std::mutex stats_mutex_;           ///< 保护统计数据
    std::atomic<std::uint64_t> total_frames_;  ///< 总帧数
    double total_frame_time_;                  ///< 总运行时间（秒）
    float max_frame_time_;                     ///< 最大帧时间（毫秒）
    int stats_window_size_;                    ///< 滑动窗口大小（保留用于未来）
};

}  // namespace Corona::Kernel
