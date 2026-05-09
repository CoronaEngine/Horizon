// System Example - 展示自定义系统生命周期
// 演示如何创建游戏系统、管理优先级、以及系统间通信

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;

// ========================================
// 自定义事件
// ========================================

struct TickEvent {
    int frame_number;
    double delta_time;
};

struct GameStateEvent {
    enum class State { Starting,
                       Running,
                       Paused,
                       Stopped };
    State state;
};

// ========================================
// 自定义游戏系统
// ========================================

// 渲染系统 - 最低优先级(最后执行)
class RenderSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "RenderSystem"; }
    int get_priority() const override { return 100; }  // 低优先级
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        std::cout << "  [RenderSystem] Initializing graphics..." << std::endl;
        ctx_ = ctx;

        // 订阅Tick事件
        ctx->event_bus()->subscribe<TickEvent>([this](const TickEvent& evt) {
            frame_count_++;
        });

        return true;
    }

    void update() override {
        // 模拟渲染工作
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        if (frame_count_ % 60 == 0 && frame_count_ > 0) {
            CFW_LOG_INFO("RenderSystem: Rendered {} frames", frame_count_);
        }
    }

    void on_thread_started() override {
        std::cout << "  [RenderSystem] Render thread started." << std::endl;
    }

    void on_thread_stopped() override {
        std::cout << "  [RenderSystem] Render thread stopped." << std::endl;
    }

    void shutdown() override {
        std::cout << "  [RenderSystem] Shutting down graphics. Total frames: "
                  << frame_count_ << std::endl;
    }

   private:
    ISystemContext* ctx_ = nullptr;
    int frame_count_ = 0;
};

// 物理系统 - 中等优先级
class PhysicsSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "PhysicsSystem"; }
    int get_priority() const override { return 50; }  // 中优先级
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        std::cout << "  [PhysicsSystem] Initializing physics engine..." << std::endl;
        ctx_ = ctx;

        ctx->event_bus()->subscribe<TickEvent>([this](const TickEvent& evt) {
            simulation_time_ += evt.delta_time;
        });

        return true;
    }

    void update() override {
        // 模拟物理计算
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        update_count_++;

        if (update_count_ % 60 == 0 && update_count_ > 0) {
            CFW_LOG_INFO("PhysicsSystem: Simulated {} seconds", simulation_time_);
        }
    }

    void shutdown() override {
        std::cout << "  [PhysicsSystem] Shutting down. Total updates: "
                  << update_count_ << std::endl;
    }

   private:
    ISystemContext* ctx_ = nullptr;
    int update_count_ = 0;
    double simulation_time_ = 0.0;
};

// 输入系统 - 最高优先级(最先执行)
class InputSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "InputSystem"; }
    int get_priority() const override { return 10; }     // 高优先级
    int get_target_fps() const override { return 120; }  // 更高的更新频率

    bool initialize(ISystemContext* ctx) override {
        std::cout << "  [InputSystem] Initializing input handlers..." << std::endl;
        ctx_ = ctx;
        return true;
    }

    void update() override {
        // 模拟输入处理
        poll_count_++;

        // 每120帧发布一次tick事件
        if (poll_count_ % 120 == 0) {
            ctx_->event_bus()->publish(TickEvent{poll_count_ / 120, 1.0});
        }
    }

    void shutdown() override {
        std::cout << "  [InputSystem] Shutting down. Total polls: "
                  << poll_count_ << std::endl;
    }

   private:
    ISystemContext* ctx_ = nullptr;
    int poll_count_ = 0;
};

// 音频系统
class AudioSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "AudioSystem"; }
    int get_priority() const override { return 75; }
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        std::cout << "  [AudioSystem] Initializing audio device..." << std::endl;
        ctx_ = ctx;

        // 监听游戏状态变化
        ctx->event_bus()->subscribe<GameStateEvent>([this](const GameStateEvent& evt) {
            switch (evt.state) {
                case GameStateEvent::State::Running:
                    std::cout << "  [AudioSystem] [AUDIO ON] Resuming audio" << std::endl;
                    break;
                case GameStateEvent::State::Paused:
                    std::cout << "  [AudioSystem] [AUDIO OFF] Pausing audio" << std::endl;
                    break;
                default:
                    break;
            }
        });

        return true;
    }

    void update() override {
        // 模拟音频处理
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    void shutdown() override {
        std::cout << "  [AudioSystem] Shutting down audio" << std::endl;
    }

   private:
    ISystemContext* ctx_ = nullptr;
};

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - System Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    auto* system_manager = kernel.system_manager();
    auto* event_bus = kernel.event_bus();

    // ========================================
    // 示例 1: 注册系统
    // ========================================
    std::cout << "[Example 1] Registering Systems" << std::endl;

    system_manager->register_system(std::make_shared<RenderSystem>());
    system_manager->register_system(std::make_shared<PhysicsSystem>());
    system_manager->register_system(std::make_shared<InputSystem>());
    system_manager->register_system(std::make_shared<AudioSystem>());

    std::cout << "  Registered 4 systems" << std::endl;
    std::cout << std::endl;

    // ========================================
    // 示例 2: 初始化系统(按优先级排序)
    // ========================================
    std::cout << "[Example 2] Initializing Systems (sorted by priority)" << std::endl;

    if (!system_manager->initialize_all()) {
        std::cerr << "Failed to initialize systems!" << std::endl;
        return 1;
    }

    std::cout << "  [OK] All systems initialized" << std::endl;
    std::cout << std::endl;

    // ========================================
    // 示例 3: 运行系统
    // ========================================
    std::cout << "[Example 3] Running Systems" << std::endl;
    std::cout << "  Systems will run for 2 seconds..." << std::endl;

    system_manager->start_all();
    event_bus->publish(GameStateEvent{GameStateEvent::State::Running});

    // 让系统运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << std::endl;

    // ========================================
    // 示例 4: 暂停和恢复
    // ========================================
    std::cout << "[Example 4] Pause and Resume" << std::endl;

    system_manager->pause_all();
    event_bus->publish(GameStateEvent{GameStateEvent::State::Paused});
    std::cout << "  [PAUSE] Systems paused" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    system_manager->resume_all();
    event_bus->publish(GameStateEvent{GameStateEvent::State::Running});
    std::cout << "  [RESUME] Systems resumed" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << std::endl;

    // ========================================
    // 示例 5: 查询系统
    // ========================================
    std::cout << "[Example 5] Query Systems" << std::endl;

    auto render_sys = system_manager->get_system("RenderSystem");
    if (render_sys) {
        std::cout << "  Found system: " << render_sys->get_name()
                  << " (Priority: " << render_sys->get_priority() << ")" << std::endl;
    }

    auto physics_sys = system_manager->get_system("PhysicsSystem");
    if (physics_sys) {
        std::cout << "  Found system: " << physics_sys->get_name()
                  << " (Priority: " << physics_sys->get_priority() << ")" << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 示例 6: 停止和清理
    // ========================================
    std::cout << "[Example 6] Stopping Systems" << std::endl;

    system_manager->stop_all();
    event_bus->publish(GameStateEvent{GameStateEvent::State::Stopped});
    std::cout << "  [STOP] Systems stopped" << std::endl;
    std::cout << std::endl;

    std::cout << "[Cleanup] Shutting down..." << std::endl;
    system_manager->shutdown_all();
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
