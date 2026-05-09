/**
 * @file main.cpp
 * @brief Complete Game Loop Example - Corona Framework
 *
 * 这个示例展示了一个完整的游戏主循环架构，包括：
 * - 游戏系统（输入、物理、渲染、音频、游戏逻辑）
 * - EventStream 异步事件通信（推荐用于多线程架构）
 * - 实体管理
 * - 性能监控
 * - 游戏状态管理
 *
 * 架构说明：
 * - 使用 EventStream 进行跨线程异步通信
 * - 发布者立即返回，不阻塞
 * - 订阅者主动拉取事件（try_pop/wait）
 * - 完全线程安全，性能隔离
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_stream.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;

// ============================================================================
// 游戏事件定义
// ============================================================================

// 游戏状态
enum class GameState { loading,
                       menu,
                       playing,
                       paused,
                       game_over };

// 玩家输入事件
struct InputEvent {
    enum class Type { move_up,
                      move_down,
                      move_left,
                      move_right,
                      action };
    Type type;
    int player_id;
};

// 实体生成事件
struct EntitySpawnEvent {
    int entity_id;
    std::string type;  // "player", "enemy", "bullet"
    float x, y;
};

// 碰撞事件
struct CollisionEvent {
    int entity_a;
    int entity_b;
    std::string type_a;
    std::string type_b;
};

// 得分事件
struct ScoreEvent {
    int player_id;
    int points;
    int total_score;
};

// 游戏状态改变事件
struct GameStateEvent {
    GameState old_state;
    GameState new_state;
};

// ============================================================================
// 实体管理
// ============================================================================

struct Entity {
    int id;
    std::string type;
    float x, y;
    float vx, vy;  // 速度
    bool active;

    Entity() : id(0), type(""), x(0), y(0), vx(0), vy(0), active(false) {}

    Entity(int id, const std::string& type, float x, float y)
        : id(id), type(type), x(x), y(y), vx(0), vy(0), active(true) {}
};

class EntityManager {
   public:
    int spawn(const std::string& type, float x, float y) {
        int id = next_id_++;
        entities_[id] = Entity(id, type, x, y);
        return id;
    }

    void destroy(int id) {
        if (entities_.count(id)) {
            entities_[id].active = false;
        }
    }

    Entity* get(int id) {
        auto it = entities_.find(id);
        return it != entities_.end() && it->second.active ? &it->second : nullptr;
    }

    std::vector<Entity*> get_by_type(const std::string& type) {
        std::vector<Entity*> result;
        for (auto& [id, entity] : entities_) {
            if (entity.active && entity.type == type) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    std::vector<Entity*> get_all_active() {
        std::vector<Entity*> result;
        for (auto& [id, entity] : entities_) {
            if (entity.active) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    size_t active_count() const {
        size_t count = 0;
        for (const auto& [id, entity] : entities_) {
            if (entity.active) count++;
        }
        return count;
    }

    void cleanup() {
        for (auto it = entities_.begin(); it != entities_.end();) {
            if (!it->second.active) {
                it = entities_.erase(it);
            } else {
                ++it;
            }
        }
    }

   private:
    std::unordered_map<int, Entity> entities_;
    int next_id_ = 1;
};

// ============================================================================
// 输入系统 - 使用 EventStream 发布事件
// ============================================================================

class InputSystem : public SystemBase {
   public:
    explicit InputSystem(EntityManager* entities) : entities_(entities) {}

    std::string_view get_name() const override { return "InputSystem"; }
    int get_priority() const override { return 1000; }  // 最高优先级
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 获取 EventStream（推荐用于跨线程通信）
        input_stream_ = ctx_->event_stream()->get_stream<InputEvent>();

        CFW_LOG_INFO("InputSystem initialized (EventStream mode)");
        return true;
    }

    void update() override {
        frame_count_++;

        // 模拟玩家输入（每秒随机移动）
        if (frame_count_ % 60 == 30) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, 3);

            InputEvent::Type types[] = {
                InputEvent::Type::move_up,
                InputEvent::Type::move_down,
                InputEvent::Type::move_left,
                InputEvent::Type::move_right};

            InputEvent event{types[dis(gen)], 1};
            input_stream_->publish(event);  // 异步发布，立即返回
        }

        // 随机射击
        if (frame_count_ % 45 == 0) {
            InputEvent event{InputEvent::Type::action, 1};
            input_stream_->publish(event);
        }
    }

    void shutdown() override {
        CFW_LOG_INFO("InputSystem shutdown");
    }

   private:
    ISystemContext* ctx_ = nullptr;
    EntityManager* entities_;
    int frame_count_ = 0;
    std::shared_ptr<EventStream<InputEvent>> input_stream_;
};

// ============================================================================
// 物理系统 - 订阅输入事件，发布碰撞事件
// ============================================================================

class PhysicsSystem : public SystemBase {
   public:
    explicit PhysicsSystem(EntityManager* entities) : entities_(entities) {}

    std::string_view get_name() const override { return "PhysicsSystem"; }
    int get_priority() const override { return 500; }
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 订阅输入事件（EventStream 方式）
        input_subscription_ = ctx_->event_stream()->get_stream<InputEvent>()->subscribe();

        // 获取碰撞事件发布流
        collision_stream_ = ctx_->event_stream()->get_stream<CollisionEvent>();
        spawn_stream_ = ctx_->event_stream()->get_stream<EntitySpawnEvent>();

        CFW_LOG_INFO("PhysicsSystem initialized (EventStream mode)");
        return true;
    }

    void update() override {
        const float dt = 1.0f / 60.0f;

        // 处理输入事件（非阻塞拉取）
        while (auto event = input_subscription_.try_pop()) {
            on_input(*event);
        }

        // 更新所有实体位置
        for (auto* entity : entities_->get_all_active()) {
            entity->x += entity->vx * dt;
            entity->y += entity->vy * dt;

            // 世界边界
            if (entity->x < 0) entity->x = 0;
            if (entity->x > 800) entity->x = 800;
            if (entity->y < 0) entity->y = 0;
            if (entity->y > 600) entity->y = 600;

            // 摩擦力
            entity->vx *= 0.98f;
            entity->vy *= 0.98f;

            // 子弹超出边界销毁
            if (entity->type == "bullet") {
                if (entity->x < -10 || entity->x > 810 ||
                    entity->y < -10 || entity->y > 610) {
                    entities_->destroy(entity->id);
                }
            }
        }

        // 碰撞检测
        detect_collisions();
    }

    void shutdown() override {
        CFW_LOG_INFO("PhysicsSystem shutdown - total collisions: {}", collision_count_);
        input_subscription_.close();
    }

   private:
    void on_input(const InputEvent& evt) {
        auto players = entities_->get_by_type("player");
        if (players.empty()) return;

        Entity* player = players[0];
        const float speed = 150.0f;

        switch (evt.type) {
            case InputEvent::Type::move_up:
                player->vy = -speed;
                break;
            case InputEvent::Type::move_down:
                player->vy = speed;
                break;
            case InputEvent::Type::move_left:
                player->vx = -speed;
                break;
            case InputEvent::Type::move_right:
                player->vx = speed;
                break;
            case InputEvent::Type::action:
                spawn_bullet(player);
                break;
        }
    }

    void spawn_bullet(Entity* player) {
        int id = entities_->spawn("bullet", player->x, player->y);
        if (Entity* bullet = entities_->get(id)) {
            bullet->vy = -400.0f;  // 向上飞

            EntitySpawnEvent evt{id, "bullet", bullet->x, bullet->y};
            spawn_stream_->publish(evt);  // 使用 EventStream
        }
    }

    void detect_collisions() {
        auto bullets = entities_->get_by_type("bullet");
        auto enemies = entities_->get_by_type("enemy");

        const float collision_radius = 20.0f;

        for (auto* bullet : bullets) {
            for (auto* enemy : enemies) {
                float dx = bullet->x - enemy->x;
                float dy = bullet->y - enemy->y;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < collision_radius) {
                    // 碰撞！发布到 EventStream
                    CollisionEvent evt{
                        bullet->id, enemy->id,
                        "bullet", "enemy"};
                    collision_stream_->publish(evt);

                    entities_->destroy(bullet->id);
                    entities_->destroy(enemy->id);
                    collision_count_++;
                    break;
                }
            }
        }
    }

   private:
    ISystemContext* ctx_ = nullptr;
    EntityManager* entities_;
    int collision_count_ = 0;

    // EventStream 订阅和发布
    EventSubscription<InputEvent> input_subscription_;
    std::shared_ptr<EventStream<CollisionEvent>> collision_stream_;
    std::shared_ptr<EventStream<EntitySpawnEvent>> spawn_stream_;
};

// ============================================================================
// 渲染系统
// ============================================================================

class RenderSystem : public SystemBase {
   public:
    explicit RenderSystem(EntityManager* entities) : entities_(entities) {}

    std::string_view get_name() const override { return "RenderSystem"; }
    int get_priority() const override { return 100; }  // 最低优先级，最后渲染
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;
        CFW_LOG_INFO("RenderSystem initialized");
        return true;
    }

    void update() override {
        frame_count_++;

        // 每秒报告一次渲染统计
        if (frame_count_ % 60 == 0) {
            size_t entity_count = entities_->active_count();
            CFW_LOG_INFO("Render frame {} - Entities: {}", frame_count_, entity_count);
        }
    }

    void shutdown() override {
        CFW_LOG_INFO("RenderSystem shutdown - total frames: {}", frame_count_);
    }

   private:
    ISystemContext* ctx_ = nullptr;
    EntityManager* entities_;
    int frame_count_ = 0;
};

// ============================================================================
// 音频系统 - 订阅事件并播放音效
// ============================================================================

class AudioSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "AudioSystem"; }
    int get_priority() const override { return 200; }
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 订阅碰撞和得分事件（EventStream 方式）
        collision_subscription_ = ctx_->event_stream()->get_stream<CollisionEvent>()->subscribe();
        score_subscription_ = ctx_->event_stream()->get_stream<ScoreEvent>()->subscribe();

        CFW_LOG_INFO("AudioSystem initialized (EventStream mode)");
        return true;
    }

    void update() override {
        // 处理碰撞事件（非阻塞）
        while (auto evt = collision_subscription_.try_pop()) {
            play_sound("explosion.wav");
        }

        // 处理得分事件
        while (auto evt = score_subscription_.try_pop()) {
            play_sound("score.wav");
        }
    }

    void shutdown() override {
        CFW_LOG_INFO("AudioSystem shutdown - sounds played: {}", sounds_played_);
        collision_subscription_.close();
        score_subscription_.close();
    }

   private:
    void play_sound(const std::string& sound) {
        sounds_played_++;
        // 实际游戏中这里会调用音频API
        if (sounds_played_ % 10 == 0) {
            CFW_LOG_DEBUG("Playing sound: {}", sound);
        }
    }

   private:
    ISystemContext* ctx_ = nullptr;
    int sounds_played_ = 0;

    // EventStream 订阅
    EventSubscription<CollisionEvent> collision_subscription_;
    EventSubscription<ScoreEvent> score_subscription_;
};

// ============================================================================
// 游戏逻辑系统
// ============================================================================

class GameLogicSystem : public SystemBase {
   public:
    explicit GameLogicSystem(EntityManager* entities)
        : entities_(entities), state_(GameState::loading) {}

    std::string_view get_name() const override { return "GameLogicSystem"; }
    int get_priority() const override { return 800; }
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 订阅碰撞事件（EventStream 方式）
        collision_subscription_ = ctx_->event_stream()->get_stream<CollisionEvent>()->subscribe();

        // 获取发布流
        score_stream_ = ctx_->event_stream()->get_stream<ScoreEvent>();
        state_stream_ = ctx_->event_stream()->get_stream<GameStateEvent>();
        spawn_stream_ = ctx_->event_stream()->get_stream<EntitySpawnEvent>();

        CFW_LOG_INFO("GameLogicSystem initialized (EventStream mode)");
        return true;
    }

    void update() override {
        frame_count_++;

        // 处理碰撞事件
        while (auto evt = collision_subscription_.try_pop()) {
            on_collision(*evt);
        }

        switch (state_) {
            case GameState::loading:
                update_loading();
                break;
            case GameState::menu:
                update_menu();
                break;
            case GameState::playing:
                update_playing();
                break;
            case GameState::paused:
                break;
            case GameState::game_over:
                update_game_over();
                break;
        }
    }

    void shutdown() override {
        collision_subscription_.close();
        CFW_LOG_INFO("GameLogicSystem shutdown - final score: {}", score_);
    }

   private:
    void update_loading() {
        if (frame_count_ == 60) {  // 1秒后
            change_state(GameState::menu);
        }
    }

    void update_menu() {
        if (frame_count_ == 120) {  // 再1秒后开始游戏
            change_state(GameState::playing);
            start_game();
        }
    }

    void update_playing() {
        // 定期生成敌人
        if (frame_count_ % 120 == 0) {
            spawn_enemy();
        }

        // 20秒后游戏结束
        if (frame_count_ >= 1200) {
            change_state(GameState::game_over);
        }
    }

    void update_game_over() {
        // 游戏结束
    }

    void change_state(GameState new_state) {
        GameState old_state = state_;
        state_ = new_state;

        std::string states[] = {"Loading", "Menu", "Playing", "Paused", "GameOver"};
        CFW_LOG_INFO("Game state: {} -> {}", states[static_cast<int>(old_state)], states[static_cast<int>(new_state)]);

        GameStateEvent evt{old_state, new_state};
        state_stream_->publish(evt);  // 使用 EventStream
    }

    void start_game() {
        // 生成玩家
        int player_id = entities_->spawn("player", 400, 500);
        EntitySpawnEvent evt{player_id, "player", 400, 500};
        spawn_stream_->publish(evt);  // 使用 EventStream

        CFW_LOG_INFO("Player spawned at center");

        // 初始生成几个敌人
        for (int i = 0; i < 3; ++i) {
            spawn_enemy();
        }
    }

    void spawn_enemy() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<> dist_x(50, 750);

        float x = dist_x(gen);
        int id = entities_->spawn("enemy", x, 50);

        if (Entity* enemy = entities_->get(id)) {
            // 敌人缓慢向下移动
            enemy->vy = 30.0f;

            EntitySpawnEvent evt{id, "enemy", x, 50};
            spawn_stream_->publish(evt);  // 使用 EventStream
        }
    }

    void on_collision(const CollisionEvent& evt) {
        if (evt.type_a == "bullet" && evt.type_b == "enemy") {
            // 击中敌人，加分
            score_ += 100;

            ScoreEvent score_evt{1, 100, score_};
            score_stream_->publish(score_evt);  // 使用 EventStream

            CFW_LOG_INFO("Enemy destroyed! Score: {}", score_);
        }
    }

   private:
    ISystemContext* ctx_ = nullptr;
    EntityManager* entities_;
    GameState state_;
    int frame_count_ = 0;
    int score_ = 0;

    // EventStream 订阅和发布
    EventSubscription<CollisionEvent> collision_subscription_;
    std::shared_ptr<EventStream<ScoreEvent>> score_stream_;
    std::shared_ptr<EventStream<GameStateEvent>> state_stream_;
    std::shared_ptr<EventStream<EntitySpawnEvent>> spawn_stream_;
};

// ============================================================================
// 主程序
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "=========================================\n";
    std::cout << "  Corona Framework - Game Loop Example  \n";
    std::cout << "=========================================\n";
    std::cout << "\n";
    std::cout << "这个示例演示了一个完整的游戏循环，包括：\n";
    std::cout << "- 多个游戏系统协同工作\n";
    std::cout << "- 事件驱动的游戏逻辑\n";
    std::cout << "- 实体管理（玩家、敌人、子弹）\n";
    std::cout << "- 碰撞检测和计分系统\n";
    std::cout << "- 游戏状态管理\n";
    std::cout << "\n";
    std::cout << "游戏将运行 20 秒...\n";
    std::cout << "\n";

    try {
        // 初始化内核上下文
        auto& context = KernelContext::instance();
        if (!context.initialize()) {
            std::cerr << "Failed to initialize kernel!" << std::endl;
            return 1;
        }

        auto* system_manager = context.system_manager();
        auto* event_bus = context.event_bus();

        // 设置日志级别
        CFW_LOG_INFO("=== Game Engine Starting ===");
        // 创建实体管理器
        EntityManager entity_manager;

        // 注册所有游戏系统（按优先级自动排序）
        system_manager->register_system(
            std::make_shared<InputSystem>(&entity_manager));

        system_manager->register_system(
            std::make_shared<PhysicsSystem>(&entity_manager));

        system_manager->register_system(
            std::make_shared<GameLogicSystem>(&entity_manager));

        system_manager->register_system(
            std::make_shared<AudioSystem>());

        system_manager->register_system(
            std::make_shared<RenderSystem>(&entity_manager));

        // 订阅关键事件（用于演示）
        int event_count = 0;
        event_bus->subscribe<GameStateEvent>([&](const GameStateEvent& evt) {
            std::cout << ">>> GAME STATE CHANGED <<<\n";
            event_count++;
        });

        event_bus->subscribe<ScoreEvent>([&](const ScoreEvent& evt) {
            std::cout << ">>> SCORE: " << evt.total_score << " (+100) <<<\n";
            event_count++;
        });

        // 初始化所有系统
        CFW_LOG_INFO("Initializing all systems...");
        if (!system_manager->initialize_all()) {
            CFW_LOG_ERROR("Failed to initialize systems");
            return 1;
        }

        // 启动游戏循环
        CFW_LOG_INFO("Starting game loop...");
        system_manager->start_all();

        std::cout << "\n---> Game Running <---\n\n";

        // 运行 20 秒
        std::this_thread::sleep_for(std::chrono::seconds(20));

        // 停止游戏
        std::cout << "\n---> Stopping Game <---\n\n";
        CFW_LOG_INFO("Stopping game loop...");
        system_manager->stop_all();

        // 清理
        entity_manager.cleanup();

        // 显示统计
        std::cout << "\n";
        std::cout << "=========================================\n";
        std::cout << "            Game Statistics              \n";
        std::cout << "=========================================\n";

        auto stats = system_manager->get_all_stats();
        for (const auto& stat : stats) {
            std::cout << "\n[" << stat.name << "]\n";
            std::cout << "  Priority: " << stat.target_fps << " FPS\n";
            std::cout << "  Total Frames: " << stat.total_frames << "\n";
            std::cout << "  Average FPS: " << stat.actual_fps << "\n";
        }

        std::cout << "\nTotal Events Processed: " << event_count << "\n";
        std::cout << "Final Entity Count: " << entity_manager.active_count() << "\n";

        // 关闭
        system_manager->shutdown_all();
        CFW_LOG_INFO("=== Game Engine Stopped ===");
        context.shutdown();

        std::cout << "\n游戏正常退出\n\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
