#pragma once
#include <cstdint>
#include <string_view>

namespace Corona::Kernel {

// 前向声明
class IEventBus;
class IEventBusStream;
class IVirtualFileSystem;
class IPluginManager;
class ISystem;

/**
 * @brief 系统上下文接口
 *
 * ISystemContext 为系统提供对内核服务和其他系统的访问。
 * 在系统的 initialize() 阶段传入，系统可以保存此指针供后续使用。
 *
 * Characteristics:
 * - Service access: Access to core services like event bus, virtual file system, etc.
 * - System interaction: Get other systems by name (not recommended, prefer event communication)
 * - Time information: Get main thread frame time and frame number
 *
 * Thread safety:
 * - 服务访问器是线程安全的
 * - get_system() 可能涉及锁，应谨慎使用
 * - 时间信息会在每帧更新，多线程访问可能读到不一致的值
 *
 * Usage example:
 * @code
 * class MySystem : public SystemBase {
 * public:
 *     bool initialize(ISystemContext* ctx) override {
 *         context_ = ctx;
 *         CFW_LOG_INFO("MySystem initialized");
 *         return true;
 *     }
 *
 *     void update() override {
 *         // 使用上下文访问服务
 *         auto dt = context_->get_delta_time();
 *         // ... 更新逻辑
 *     }
 * private:
 *     ISystemContext* context_ = nullptr;
 * };
 * @endcode
 */
class ISystemContext {
   public:
    virtual ~ISystemContext() = default;

    // ========================================
    // 内核服务访问
    // ========================================

    /**
     * @brief 获取事件总线
     * @return 事件总线指针
     *
     * 用于发布和订阅即时事件
     */
    virtual IEventBus* event_bus() = 0;

    /**
     * @brief 获取事件流
     * @return 事件流指针
     *
     * 用于队列消息传递
     */
    virtual IEventBusStream* event_stream() = 0;

    /**
     * @brief 获取虚拟文件系统
     * @return 虚拟文件系统指针
     *
     * 用于访问文件和目录
     */
    virtual IVirtualFileSystem* vfs() = 0;

    /**
     * @brief 获取插件管理器
     * @return 插件管理器指针
     *
     * 用于加载和管理插件
     */
    virtual IPluginManager* plugin_manager() = 0;

    // ========================================
    // 系统互访
    // ========================================

    /**
     * @brief 根据名称获取其他系统
     * @param name 系统名称
     * @return 系统指针，未找到返回 nullptr
     *
     * ⚠️ 注意：
     * - 直接访问其他系统会产生强耦合
     * - 优先使用事件总线或事件流进行跨系统通信
     * - 仅在必要时使用（如渲染系统需要访问相机系统）
     * - 注意循环依赖问题
     */
    virtual ISystem* get_system(std::string_view name) = 0;

    // ========================================
    // 时间信息
    // ========================================

    /**
     * @brief 获取主线程帧时间
     * @return 距上一帧的时间间隔（秒）
     *
     * 注意：此值基于主线程的帧循环，各系统的实际帧时间可能不同
     */
    virtual float get_delta_time() const = 0;

    /**
     * @brief 获取主线程帧号
     * @return 当前帧号（从 0 开始递增）
     *
     * 用于标识主线程的帧序列号
     */
    virtual uint64_t get_frame_number() const = 0;
};

}  // namespace Corona::Kernel
