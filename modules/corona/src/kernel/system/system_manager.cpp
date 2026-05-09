#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "corona/kernel/core/i_logger.h"
#include "corona/kernel/core/i_plugin_manager.h"
#include "corona/kernel/core/i_vfs.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"
#include "corona/kernel/system/i_system_context.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

namespace Corona::Kernel {

// SystemContext 实现
class SystemContext : public ISystemContext {
   public:
    explicit SystemContext(ISystemManager* system_manager)
        : system_manager_(system_manager),
          main_thread_delta_time_(0.0f),
          main_thread_frame_number_(0) {}

    IEventBus* event_bus() override {
        return KernelContext::instance().event_bus();
    }

    IEventBusStream* event_stream() override {
        return KernelContext::instance().event_stream();
    }

    IVirtualFileSystem* vfs() override {
        return KernelContext::instance().vfs();
    }

    IPluginManager* plugin_manager() override {
        return KernelContext::instance().plugin_manager();
    }

    ISystem* get_system(std::string_view name) override {
        auto sys = system_manager_->get_system(name);
        return sys ? sys.get() : nullptr;
    }

    float get_delta_time() const override {
        return main_thread_delta_time_;
    }

    uint64_t get_frame_number() const override {
        return main_thread_frame_number_;
    }

    // 由主线程更新
    void update_frame_info(float delta_time, uint64_t frame_number) {
        main_thread_delta_time_ = delta_time;
        main_thread_frame_number_ = frame_number;
    }

   private:
    ISystemManager* system_manager_;
    float main_thread_delta_time_;
    uint64_t main_thread_frame_number_;
};

// SystemManager 实现
class SystemManager : public ISystemManager {
   public:
    SystemManager() : context_(std::make_unique<SystemContext>(this)) {}

    void register_system(std::shared_ptr<ISystem> system) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // 设置上下文（如果是 SystemBase）
        auto* base = dynamic_cast<SystemBase*>(system.get());
        if (base) {
            base->set_context(context_.get());
        }

        systems_.push_back(system);
        systems_by_name_[std::string(system->get_name())] = system;
        systems_sorted_ = false;  // Mark as unsorted when new system is registered
    }

    std::shared_ptr<ISystem> get_system(std::string_view name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = systems_by_name_.find(std::string(name));
        if (it != systems_by_name_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool initialize_all() override {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only sort if systems were modified since last sort
        if (!systems_sorted_) {
            std::sort(systems_.begin(), systems_.end(),
                      [](const auto& a, const auto& b) {
                          return a->get_priority() > b->get_priority();
                      });
            systems_sorted_ = true;
        }

        // 初始化所有系统
        for (auto& system : systems_) {
            if (!system->initialize(context_.get())) {
                CFW_LOG_ERROR("Failed to initialize system: {}", system->get_name());
                return false;
            }

            CFW_LOG_INFO("Initialized system: {}", system->get_name());
        }

        return true;
    }

    void start_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& system : systems_) {
            system->start();
            CFW_LOG_INFO("Started system: {}", system->get_name());
        }
    }

    void pause_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& system : systems_) {
            system->pause();
        }
    }

    void resume_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& system : systems_) {
            system->resume();
        }
    }

    void stop_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        // 按相反顺序停止
        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
            (*it)->stop();
            CFW_LOG_INFO("Stopped system: {}", (*it)->get_name());
        }
    }

    void shutdown_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        // 按相反顺序关闭
        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
            (*it)->shutdown();
            CFW_LOG_INFO("Shutdown system: {}", (*it)->get_name());
        }
    }

    void sync_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& system : systems_) {
            system->sync();
        }
    }

    std::vector<std::shared_ptr<ISystem>> get_all_systems() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return systems_;
    }

    SystemStats get_system_stats(std::string_view name) override {
        std::lock_guard<std::mutex> lock(mutex_);

        auto sys = get_system_unlocked(name);
        if (!sys) {
            return SystemStats{};
        }

        SystemStats stats;
        stats.name = sys->get_name();
        stats.state = sys->get_state();
        stats.target_fps = sys->get_target_fps();
        stats.actual_fps = sys->get_actual_fps();
        stats.average_frame_time_ms = sys->get_average_frame_time();
        stats.max_frame_time_ms = sys->get_max_frame_time();
        stats.total_frames = sys->get_total_frames();
        stats.total_update_calls = sys->get_total_frames();  // 与总帧数相同

        return stats;
    }

    std::vector<SystemStats> get_all_stats() override {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<SystemStats> all_stats;
        all_stats.reserve(systems_.size());

        for (const auto& sys : systems_) {
            SystemStats stats;
            stats.name = sys->get_name();
            stats.state = sys->get_state();
            stats.target_fps = sys->get_target_fps();
            stats.actual_fps = sys->get_actual_fps();
            stats.average_frame_time_ms = sys->get_average_frame_time();
            stats.max_frame_time_ms = sys->get_max_frame_time();
            stats.total_frames = sys->get_total_frames();
            stats.total_update_calls = sys->get_total_frames();

            all_stats.push_back(stats);
        }

        return all_stats;
    }

    SystemContext* get_context() {
        return context_.get();
    }

   private:
    // 无锁版本（内部使用）
    std::shared_ptr<ISystem> get_system_unlocked(std::string_view name) {
        auto it = systems_by_name_.find(std::string(name));
        if (it != systems_by_name_.end()) {
            return it->second;
        }
        return nullptr;
    }
    std::unique_ptr<SystemContext> context_;
    std::vector<std::shared_ptr<ISystem>> systems_;
    std::map<std::string, std::shared_ptr<ISystem>> systems_by_name_;
    std::mutex mutex_;
    bool systems_sorted_ = false;  // Track if systems are sorted by priority
};

// Factory function
std::unique_ptr<ISystemManager> create_system_manager() {
    return std::make_unique<SystemManager>();
}

}  // namespace Corona::Kernel
