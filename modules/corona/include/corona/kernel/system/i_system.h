#pragma once
#include <cstdint>
#include <string_view>

namespace Corona::Kernel {

/**
 * @brief 系统状态枚举
 *
 * 描述系统的运行状态，用于生命周期管理
 */
enum class SystemState {
    idle,      ///< 未启动：系统已创建但未初始化
    running,   ///< 正在运行：系统线程正在执行 update() 循环
    paused,    ///< 已暂停：系统线程存在但暂停执行
    stopping,  ///< 正在停止：系统正在关闭过程中
    stopped    ///< 已停止：系统线程已结束
};

class ISystemContext;

/**
 * @brief 系统接口
 *
 * ISystem 定义了引擎中一个可独立运行的子系统。
 * 每个系统在独立线程中运行，拥有自己的更新循环和帧率控制。
 *
 * 生命周期：
 * 1. 创建系统实例
 * 2. 注册到 SystemManager
 * 3. initialize() - 在主线程初始化
 * 4. start() - 启动系统线程
 * 5. update() - 在系统线程循环调用
 * 6. stop() - 停止系统线程
 * 7. shutdown() - 在主线程清理资源
 *
 * 特性：
 * - 独立线程：每个系统在自己的线程中运行
 * - 帧率控制：可设置目标 FPS，自动节流
 * - 生命周期管理：支持 start/pause/resume/stop
 * - 性能统计：自动收集 FPS、帧时间等指标
 * - 优先级排序：通过 get_priority() 控制初始化顺序
 *
 * 使用示例：
 * @code
 * class RenderSystem : public SystemBase {
 * public:
 *     std::string_view get_name() const override { return "Render"; }
 *     int get_priority() const override { return 100; }
 *
 *     bool initialize(ISystemContext* ctx) override {
 *         // 初始化渲染器
 *         return true;
 *     }
 *
 *     void update() override {
 *         // 渲染一帧
 *     }
 *
 *     void shutdown() override {
 *         // 清理资源
 *     }
 * };
 * @endcode
 */
class ISystem {
   public:
    virtual ~ISystem() = default;

    // ========================================
    // 系统标识
    // ========================================

    /**
     * @brief 获取系统名称
     * @return 系统的唯一名称
     */
    virtual std::string_view get_name() const = 0;

    /**
     * @brief 获取系统优先级
     * @return 优先级值，数值越大越优先初始化
     *
     * 用于控制系统初始化顺序。
     * 例如：Logger(1000) -> EventBus(900) -> GameLogic(100)
     */
    virtual int get_priority() const = 0;

    // ========================================
    // 生命周期管理
    // ========================================

    /**
     * @brief 初始化系统
     * @param context 系统上下文，提供对内核服务和其他系统的访问
     * @return 初始化成功返回 true，失败返回 false
     *
     * 在主线程中调用，用于初始化系统所需的资源。
     * 在此阶段可以访问 context 获取其他服务。
     */
    virtual bool initialize(ISystemContext* context) = 0;

    /**
     * @brief 关闭系统
     *
     * 在主线程中调用，用于清理系统占用的资源。
     * 调用前应确保系统线程已停止。
     */
    virtual void shutdown() = 0;

    /**
     * @brief 系统更新函数
     *
     * 在系统的独立线程中循环调用。
     * 实现此方法以执行系统的核心逻辑。
     *
     * 注意：
     * - 在独立线程中执行，注意线程安全
     * - 不应长时间阻塞，以免影响 FPS
     * - 推荐通过事件进行跨系统通信
     */
    virtual void update() = 0;

    /**
     * @brief 帧同步点（可选）
     *
     * 对于需要与主线程或其他系统同步的系统，可以实现此方法。
     * SystemManager::sync_all() 会调用所有系统的 sync()。
     *
     * 使用场景：
     * - 渲染系统需要等待逻辑系统完成
     * - 多系统间需要栅栏同步
     */
    virtual void sync() {}

    // ========================================
    // 帧率控制
    // ========================================

    /**
     * @brief 获取目标帧率
     * @return 目标 FPS，0 表示不限制
     *
     * 系统会尽量保持此帧率运行。
     * 例如：渲染系统 60 FPS，物理系统 120 FPS
     */
    virtual int get_target_fps() const = 0;

    // ========================================
    // 状态查询
    // ========================================

    /**
     * @brief 获取当前系统状态
     * @return 系统的当前状态
     */
    virtual SystemState get_state() const = 0;

    // ========================================
    // 线程控制
    // ========================================

    /**
     * @brief 启动系统线程
     *
     * 创建并启动系统的工作线程，开始调用 update() 循环。
     * 只能在 idle 或 stopped 状态下调用。
     */
    virtual void start() = 0;

    /**
     * @brief 暂停系统
     *
     * 暂停 update() 循环，但不销毁线程。
     * 只能在 running 状态下调用。
     */
    virtual void pause() = 0;

    /**
     * @brief 恢复系统
     *
     * 恢复 update() 循环。
     * 只能在 paused 状态下调用。
     */
    virtual void resume() = 0;

    /**
     * @brief 停止系统线程
     *
     * 停止并销毁系统的工作线程。
     * 可以在 running 或 paused 状态下调用。
     */
    virtual void stop() = 0;

    // ========================================
    // 性能统计
    // ========================================

    /**
     * @brief 获取实际帧率
     * @return 当前实际 FPS（基于滑动窗口统计）
     */
    virtual float get_actual_fps() const = 0;

    /**
     * @brief 获取平均帧时间
     * @return 平均每帧耗时（毫秒）
     */
    virtual float get_average_frame_time() const = 0;

    /**
     * @brief 获取最大帧时间
     * @return 统计窗口内的最大帧时间（毫秒）
     */
    virtual float get_max_frame_time() const = 0;

    /**
     * @brief 获取总帧数
     * @return 系统启动以来执行的总帧数
     */
    virtual std::uint64_t get_total_frames() const = 0;

    /**
     * @brief 重置统计信息
     *
     * 清零所有性能统计数据，用于测试或分段统计
     */
    virtual void reset_stats() = 0;
};

}  // namespace Corona::Kernel
