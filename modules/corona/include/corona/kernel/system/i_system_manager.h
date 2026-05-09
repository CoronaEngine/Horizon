#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "i_system.h"

namespace Corona::Kernel {

/**
 * @brief 系统统计信息结构体
 *
 * 包含系统运行时的各项指标
 */
struct SystemStats {
    std::string_view name;             ///< 系统名称
    SystemState state;                 ///< 系统状态
    int target_fps;                    ///< 目标帧率
    float actual_fps;                  ///< 实际帧率
    float average_frame_time_ms;       ///< 平均帧时间（毫秒）
    float max_frame_time_ms;           ///< 最大帧时间（毫秒）
    std::uint64_t total_frames;        ///< 总帧数
    std::uint64_t total_update_calls;  ///< 总更新次数
};

/**
 * @brief 系统管理器接口
 *
 * ISystemManager 负责管理引擎中的所有子系统。
 *
 * 主要职责：
 * - 系统注册：注册和获取系统实例
 * - 生命周期管理：统一初始化、启动、停止、关闭所有系统
 * - 优先级排序：根据系统优先级控制初始化顺序
 * - 状态控制：批量控制系统的运行状态（暂停/恢复）
 * - 性能监控：收集和查询系统性能统计
 *
 * 典型使用流程：
 * @code
 * auto sys_mgr = KernelContext::instance().system_manager();
 *
 * // 1. 注册系统
 * sys_mgr->register_system(std::make_shared<RenderSystem>());
 * sys_mgr->register_system(std::make_shared<PhysicsSystem>());
 * sys_mgr->register_system(std::make_shared<AudioSystem>());
 *
 * // 2. 初始化所有系统（按优先级）
 * if (!sys_mgr->initialize_all()) {
 *     // 处理初始化失败
 * }
 *
 * // 3. 启动所有系统线程
 * sys_mgr->start_all();
 *
 * // 主循环...
 * while (running) {
 *     // 可选：同步点
 *     sys_mgr->sync_all();
 *
 *     // 查看统计
 *     auto stats = sys_mgr->get_all_stats();
 * }
 *
 * // 4. 停止和关闭
 * sys_mgr->stop_all();
 * sys_mgr->shutdown_all();
 * @endcode
 */
class ISystemManager {
   public:
    virtual ~ISystemManager() = default;

    // ========================================
    // 系统注册与获取
    // ========================================

    /**
     * @brief 注册系统
     * @param system 系统实例的共享指针
     *
     * 将系统添加到管理器。
     * 应在初始化前完成所有系统的注册。
     */
    virtual void register_system(std::shared_ptr<ISystem> system) = 0;

    /**
     * @brief 根据名称获取系统
     * @param name 系统名称
     * @return 系统的共享指针，未找到返回 nullptr
     */
    virtual std::shared_ptr<ISystem> get_system(std::string_view name) = 0;

    /**
     * @brief 获取所有已注册的系统
     * @return 系统列表（按优先级排序）
     */
    virtual std::vector<std::shared_ptr<ISystem>> get_all_systems() = 0;

    // ========================================
    // 生命周期管理（批量操作）
    // ========================================

    /**
     * @brief 初始化所有系统
     * @return 所有系统初始化成功返回 true，任一失败返回 false
     *
     * 按优先级从高到低依次调用各系统的 initialize()。
     * 如果某个系统初始化失败，会停止后续系统的初始化。
     */
    virtual bool initialize_all() = 0;

    /**
     * @brief 启动所有系统线程
     *
     * 调用所有系统的 start()，启动各自的工作线程。
     */
    virtual void start_all() = 0;

    /**
     * @brief 暂停所有系统
     *
     * 调用所有系统的 pause()，暂停 update() 循环。
     */
    virtual void pause_all() = 0;

    /**
     * @brief 恢复所有系统
     *
     * 调用所有系统的 resume()，恢复 update() 循环。
     */
    virtual void resume_all() = 0;

    /**
     * @brief 停止所有系统线程
     *
     * 调用所有系统的 stop()，停止并销毁工作线程。
     */
    virtual void stop_all() = 0;

    /**
     * @brief 关闭所有系统
     *
     * 按优先级从低到高依次调用各系统的 shutdown()。
     * 顺序与初始化相反，确保依赖关系正确。
     */
    virtual void shutdown_all() = 0;

    // ========================================
    // 同步控制
    // ========================================

    /**
     * @brief 同步点：等待所有系统完成当前帧
     *
     * 调用所有系统的 sync() 方法。
     * 用于需要栅栏同步的场景（如帧结束时同步）。
     */
    virtual void sync_all() = 0;

    // ========================================
    // 性能统计
    // ========================================

    /**
     * @brief 获取单个系统的统计信息
     * @param name 系统名称
     * @return 系统统计信息
     */
    virtual SystemStats get_system_stats(std::string_view name) = 0;

    /**
     * @brief 获取所有系统的统计信息
     * @return 所有系统的统计信息列表
     */
    virtual std::vector<SystemStats> get_all_stats() = 0;
};

/**
 * @brief 创建系统管理器实例
 * @return 系统管理器的唯一指针
 */
std::unique_ptr<ISystemManager> create_system_manager();

}  // namespace Corona::Kernel
