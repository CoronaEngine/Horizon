// EventBus Example - 展示事件发布订阅模式
// 演示如何使用类型安全的事件系统进行组件间通信

#include <iostream>
#include <string>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"

using namespace Corona::Kernel;

// ========================================
// 定义自定义事件
// ========================================

// 1. 简单事件 - 用户登录
struct UserLoginEvent {
    std::string username;
    int user_id;
};

// 2. 游戏事件 - 玩家得分
struct PlayerScoreEvent {
    int player_id;
    int score;
    std::string reason;
};

// 3. 系统事件 - 配置变更
struct ConfigChangedEvent {
    std::string key;
    std::string old_value;
    std::string new_value;
};

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - EventBus Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    auto* event_bus = kernel.event_bus();

    // ========================================
    // 示例 1: 基本订阅和发布
    // ========================================
    std::cout << "[Example 1] Basic Publish/Subscribe" << std::endl;

    // 订阅用户登录事件
    event_bus->subscribe<UserLoginEvent>([](const UserLoginEvent& evt) {
        std::cout << "  -> User logged in: " << evt.username
                  << " (ID: " << evt.user_id << ")" << std::endl;
    });

    // 发布事件
    event_bus->publish(UserLoginEvent{"Alice", 1001});
    event_bus->publish(UserLoginEvent{"Bob", 1002});
    std::cout << std::endl;

    // ========================================
    // 示例 2: 多个订阅者
    // ========================================
    std::cout << "[Example 2] Multiple Subscribers" << std::endl;

    // 订阅者 1: 记录分数
    event_bus->subscribe<PlayerScoreEvent>([](const PlayerScoreEvent& evt) {
        std::cout << "  [ScoreLogger] Player " << evt.player_id
                  << " scored " << evt.score << " points" << std::endl;
    });

    // 订阅者 2: 检查成就
    event_bus->subscribe<PlayerScoreEvent>([](const PlayerScoreEvent& evt) {
        if (evt.score >= 100) {
            std::cout << "  [AchievementSystem] [TROPHY] Player " << evt.player_id
                      << " unlocked 'High Scorer' achievement!" << std::endl;
        }
    });

    // 订阅者 3: 更新排行榜
    int total_score = 0;
    event_bus->subscribe<PlayerScoreEvent>([&total_score](const PlayerScoreEvent& evt) {
        total_score += evt.score;
        std::cout << "  [Leaderboard] Total score: " << total_score << std::endl;
    });

    // 发布多个得分事件
    event_bus->publish(PlayerScoreEvent{1, 50, "defeated enemy"});
    event_bus->publish(PlayerScoreEvent{2, 150, "completed level"});
    event_bus->publish(PlayerScoreEvent{1, 75, "collected bonus"});
    std::cout << std::endl;

    // ========================================
    // 示例 3: 使用 Logger 记录事件
    // ========================================
    std::cout << "[Example 3] Event Logging" << std::endl;

    event_bus->subscribe<ConfigChangedEvent>([](const ConfigChangedEvent& evt) {
        CFW_LOG_INFO("Config changed: {} = {} (was: {})", evt.key, evt.new_value, evt.old_value);
    });

    event_bus->publish(ConfigChangedEvent{"volume", "50", "80"});
    event_bus->publish(ConfigChangedEvent{"difficulty", "easy", "hard"});
    std::cout << std::endl;

    // ========================================
    // 示例 4: 事件驱动的工作流
    // ========================================
    std::cout << "[Example 4] Event-Driven Workflow" << std::endl;

    // 模拟一个完整的游戏流程
    event_bus->subscribe<UserLoginEvent>([event_bus](const UserLoginEvent& evt) {
        std::cout << "  Step 1: User " << evt.username << " logged in" << std::endl;
        // 登录成功后,发布玩家准备事件
        event_bus->publish(PlayerScoreEvent{evt.user_id, 0, "game started"});
    });

    event_bus->subscribe<PlayerScoreEvent>([](const PlayerScoreEvent& evt) {
        if (evt.reason == "game started") {
            std::cout << "  Step 2: Player " << evt.player_id << " started playing" << std::endl;
        }
    });

    // 触发工作流
    event_bus->publish(UserLoginEvent{"Charlie", 1003});
    std::cout << std::endl;

    // ========================================
    // 性能统计
    // ========================================
    std::cout << "[Performance Test] Publishing 10,000 events..." << std::endl;

    int event_count = 0;
    event_bus->subscribe<PlayerScoreEvent>([&event_count](const PlayerScoreEvent&) {
        event_count++;
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        event_bus->publish(PlayerScoreEvent{i, i * 10, "test"});
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Published and processed " << event_count << " events in "
              << duration.count() << " ms" << std::endl;
    std::cout << "  Average: " << (duration.count() * 1000.0 / event_count)
              << " microseconds per event" << std::endl;

    // 清理
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
