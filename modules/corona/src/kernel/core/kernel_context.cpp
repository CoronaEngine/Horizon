#include "corona/kernel/core/kernel_context.h"

#include <memory>
#include <mutex>

#include "corona/kernel/core/i_logger.h"

namespace Corona::Kernel {

// Forward declare factory functions
std::unique_ptr<IEventBus> create_event_bus();
std::unique_ptr<IEventBusStream> create_event_bus_stream();
std::unique_ptr<IVirtualFileSystem> create_vfs();
std::unique_ptr<IPluginManager> create_plugin_manager();
std::unique_ptr<ISystemManager> create_system_manager();

// Static mutex for initialization
static std::mutex init_mutex;

KernelContext& KernelContext::instance() {
    static KernelContext instance;
    return instance;
}

bool KernelContext::initialize() {
    std::lock_guard<std::mutex> lock(init_mutex);

    if (initialized_) {
        return true;
    }

    // 初始化日志系统（必须最先）
    CoronaLogger::initialize();

    // Initialize services in order
    event_bus_ = create_event_bus();
    if (!event_bus_) {
        return false;
    }

    event_stream_ = create_event_bus_stream();
    if (!event_stream_) {
        return false;
    }

    vfs_ = create_vfs();
    if (!vfs_) {
        return false;
    }

    plugin_manager_ = create_plugin_manager();
    if (!plugin_manager_) {
        return false;
    }

    system_manager_ = create_system_manager();
    if (!system_manager_) {
        return false;
    }

    initialized_ = true;
    CFW_LOG_INFO("Kernel initialized successfully");

    return true;
}

void KernelContext::shutdown() {
    std::lock_guard<std::mutex> lock(init_mutex);

    if (!initialized_) {
        return;
    }

    CFW_LOG_INFO("Shutting down kernel...");

    // Shutdown services in reverse order
    system_manager_.reset();
    plugin_manager_.reset();
    vfs_.reset();
    event_stream_.reset();
    event_bus_.reset();

    initialized_ = false;
}

}  // namespace Corona::Kernel
