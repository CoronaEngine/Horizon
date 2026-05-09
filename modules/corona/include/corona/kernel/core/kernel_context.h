#pragma once
#include <memory>

#include "../event/i_event_bus.h"
#include "../event/i_event_stream.h"
#include "../system/i_system_manager.h"
#include "i_logger.h"
#include "i_plugin_manager.h"
#include "i_vfs.h"

namespace Corona::Kernel {

/**
 * @brief 内核上下文单例类
 *
 * KernelContext 是引擎的核心管理类，负责初始化和管理所有内核服务。
 * 包括日志系统、事件总线、事件流、虚拟文件系统、插件管理器和系统管理器。
 *
 * 使用方式：
 * 1. 通过 instance() 获取单例
 * 2. 调用 initialize() 初始化所有服务
 * 3. 通过访问器获取各个服务指针
 * 4. 程序结束前调用 shutdown() 清理资源
 *
 * 线程安全：单例获取是线程安全的，但初始化和关闭应在单一线程中进行
 */
class KernelContext {
   public:
    /**
     * @brief 获取内核上下文单例
     * @return 内核上下文的唯一实例引用
     */
    static KernelContext& instance();

    /**
     * @brief 初始化所有内核服务
     *
     * 按顺序初始化：日志 -> 事件总线 -> 事件流 -> 虚拟文件系统 -> 插件管理器 -> 系统管理器
     *
     * @return 初始化成功返回 true，失败返回 false
     */
    bool initialize();

    /**
     * @brief 关闭所有内核服务
     *
     * 按初始化相反的顺序关闭各个服务，确保资源正确释放
     */
    void shutdown();

    // ========================================
    // 服务访问器
    // ========================================

    /** @brief 获取事件总线指针 */
    IEventBus* event_bus() const { return event_bus_.get(); }

    /** @brief 获取事件流指针 */
    IEventBusStream* event_stream() const { return event_stream_.get(); }

    /** @brief 获取虚拟文件系统指针 */
    IVirtualFileSystem* vfs() const { return vfs_.get(); }

    /** @brief 获取插件管理器指针 */
    IPluginManager* plugin_manager() const { return plugin_manager_.get(); }

    /** @brief 获取系统管理器指针 */
    ISystemManager* system_manager() const { return system_manager_.get(); }

   private:
    KernelContext() = default;
    ~KernelContext() = default;

    // 禁止拷贝和移动
    KernelContext(const KernelContext&) = delete;
    KernelContext& operator=(const KernelContext&) = delete;
    KernelContext(KernelContext&&) = delete;
    KernelContext& operator=(KernelContext&&) = delete;

    // 核心服务实例
    std::unique_ptr<IEventBus> event_bus_;            ///< 事件总线（即时消息）
    std::unique_ptr<IEventBusStream> event_stream_;   ///< 事件流（队列消息）
    std::unique_ptr<IVirtualFileSystem> vfs_;         ///< 虚拟文件系统
    std::unique_ptr<IPluginManager> plugin_manager_;  ///< 插件管理器
    std::unique_ptr<ISystemManager> system_manager_;  ///< 系统管理器

    bool initialized_ = false;  ///< 初始化状态标志
};

}  // namespace Corona::Kernel
