// ECS Example - Entity Component System Usage
// Demonstrates Entity creation, Component management, and System-like iteration

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "corona/kernel/ecs/world.h"

using namespace Corona::Kernel::ECS;

// ========================================
// Define Components
// ========================================

/// 位置组件
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/// 速度组件
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

/// 生命值组件
struct Health {
    int current = 100;
    int max = 100;
};

/// 名称组件
struct Name {
    std::string value;
};

/// 渲染组件
struct Renderable {
    std::string sprite;
    int layer = 0;
    bool visible = true;
};

/// AI 组件
struct AIController {
    std::string behavior = "idle";
    float think_interval = 1.0f;
    float time_since_think = 0.0f;
};

// ========================================
// Helper Functions
// ========================================

void print_separator(const std::string& title) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

void print_entity_info(World& world, EntityId entity, const std::string& label) {
    std::cout << label << " (ID: " << entity.raw() << ")";

    if (auto* name = world.get_component<Name>(entity)) {
        std::cout << " Name: \"" << name->value << "\"";
    }

    if (auto* pos = world.get_component<Position>(entity)) {
        std::cout << " Pos: (" << pos->x << ", " << pos->y << ", " << pos->z << ")";
    }

    if (auto* vel = world.get_component<Velocity>(entity)) {
        std::cout << " Vel: (" << vel->vx << ", " << vel->vy << ", " << vel->vz << ")";
    }

    if (auto* health = world.get_component<Health>(entity)) {
        std::cout << " HP: " << health->current << "/" << health->max;
    }

    std::cout << std::endl;
}

// ========================================
// Example 1: Basic Entity and Component Operations
// ========================================

void example_basic_usage() {
    print_separator("Example 1: Basic Entity and Component Operations");

    World world;

    std::cout << "Initial entity count: " << world.entity_count() << std::endl;

    // 创建空实体
    EntityId empty_entity = world.create_entity();
    std::cout << "Created empty entity: " << empty_entity.raw() << std::endl;

    // 创建带组件的实体
    EntityId player = world.create_entity(
        Position{100.0f, 200.0f, 0.0f},
        Velocity{1.0f, 0.0f, 0.0f},
        Health{100, 100},
        Name{"Hero"});
    std::cout << "Created player entity: " << player.raw() << std::endl;

    // 创建敌人实体
    EntityId enemy = world.create_entity(
        Position{500.0f, 200.0f, 0.0f},
        Velocity{-0.5f, 0.0f, 0.0f},
        Health{50, 50},
        Name{"Goblin"});
    std::cout << "Created enemy entity: " << enemy.raw() << std::endl;

    std::cout << "\nTotal entity count: " << world.entity_count() << std::endl;
    std::cout << "Archetype count: " << world.archetype_count() << std::endl;

    // 打印实体信息
    std::cout << "\nEntity details:" << std::endl;
    print_entity_info(world, player, "  Player");
    print_entity_info(world, enemy, "  Enemy");

    // 检查实体是否存活
    std::cout << "\nEntity alive status:" << std::endl;
    std::cout << "  Player alive: " << (world.is_alive(player) ? "yes" : "no") << std::endl;
    std::cout << "  Enemy alive: " << (world.is_alive(enemy) ? "yes" : "no") << std::endl;
}

// ========================================
// Example 2: Component Modification
// ========================================

void example_component_modification() {
    print_separator("Example 2: Component Modification");

    World world;

    // 创建实体
    EntityId entity = world.create_entity(
        Position{0.0f, 0.0f, 0.0f},
        Velocity{10.0f, 5.0f, 0.0f},
        Health{100, 100});

    std::cout << "Initial state:" << std::endl;
    print_entity_info(world, entity, "  Entity");

    // 修改位置组件
    if (auto* pos = world.get_component<Position>(entity)) {
        pos->x += 10.0f;
        pos->y += 5.0f;
    }

    // 使用 set_component 替换组件
    world.set_component<Velocity>(entity, Velocity{20.0f, 10.0f, 0.0f});

    // 修改生命值
    if (auto* health = world.get_component<Health>(entity)) {
        health->current -= 25;
    }

    std::cout << "\nAfter modification:" << std::endl;
    print_entity_info(world, entity, "  Entity");

    // 检查组件是否存在
    std::cout << "\nComponent checks:" << std::endl;
    std::cout << "  Has Position: " << (world.has_component<Position>(entity) ? "yes" : "no") << std::endl;
    std::cout << "  Has Velocity: " << (world.has_component<Velocity>(entity) ? "yes" : "no") << std::endl;
    std::cout << "  Has Renderable: " << (world.has_component<Renderable>(entity) ? "yes" : "no") << std::endl;
}

// ========================================
// Example 3: Adding and Removing Components
// ========================================

void example_add_remove_components() {
    print_separator("Example 3: Adding and Removing Components");

    World world;

    // 创建基础实体
    EntityId entity = world.create_entity(
        Position{50.0f, 50.0f, 0.0f},
        Name{"Dynamic Entity"});

    std::cout << "Initial archetype count: " << world.archetype_count() << std::endl;
    std::cout << "Has Velocity: " << (world.has_component<Velocity>(entity) ? "yes" : "no") << std::endl;
    std::cout << "Has Health: " << (world.has_component<Health>(entity) ? "yes" : "no") << std::endl;

    // 添加 Velocity 组件
    std::cout << "\nAdding Velocity component..." << std::endl;
    bool added = world.add_component<Velocity>(entity, Velocity{5.0f, 5.0f, 0.0f});
    std::cout << "  Add result: " << (added ? "success" : "failed") << std::endl;
    std::cout << "  Has Velocity: " << (world.has_component<Velocity>(entity) ? "yes" : "no") << std::endl;
    std::cout << "  Archetype count: " << world.archetype_count() << std::endl;

    // 添加 Health 组件
    std::cout << "\nAdding Health component..." << std::endl;
    world.add_component<Health>(entity, Health{75, 100});
    std::cout << "  Has Health: " << (world.has_component<Health>(entity) ? "yes" : "no") << std::endl;
    std::cout << "  Archetype count: " << world.archetype_count() << std::endl;

    print_entity_info(world, entity, "\nEntity with all components");

    // 移除 Velocity 组件
    std::cout << "\nRemoving Velocity component..." << std::endl;
    bool removed = world.remove_component<Velocity>(entity);
    std::cout << "  Remove result: " << (removed ? "success" : "failed") << std::endl;
    std::cout << "  Has Velocity: " << (world.has_component<Velocity>(entity) ? "yes" : "no") << std::endl;
    std::cout << "  Archetype count: " << world.archetype_count() << std::endl;

    print_entity_info(world, entity, "Entity after removal");
}

// ========================================
// Example 4: Iterating Entities with each()
// ========================================

void example_iteration() {
    print_separator("Example 4: Iterating Entities with each()");

    World world;

    // 创建多个实体
    std::cout << "Creating entities..." << std::endl;

    // 玩家 - 有所有组件
    (void)world.create_entity(
        Position{0.0f, 0.0f, 0.0f},
        Velocity{1.0f, 0.0f, 0.0f},
        Health{100, 100},
        Name{"Player"});

    // 敌人 - 有 Position, Velocity, Health
    for (int i = 0; i < 5; ++i) {
        (void)world.create_entity(
            Position{static_cast<float>(i * 100), 0.0f, 0.0f},
            Velocity{-1.0f, 0.0f, 0.0f},
            Health{50, 50});
    }

    // 静态物体 - 只有 Position
    for (int i = 0; i < 3; ++i) {
        (void)world.create_entity(
            Position{static_cast<float>(i * 50 + 25), 100.0f, 0.0f});
    }

    std::cout << "Total entities: " << world.entity_count() << std::endl;
    std::cout << "Archetype count: " << world.archetype_count() << std::endl;

    // 模拟物理更新 - 遍历有 Position 和 Velocity 的实体
    std::cout << "\n--- Physics Update (Position + Velocity) ---" << std::endl;
    float delta_time = 0.016f;  // ~60 FPS
    int physics_count = 0;

    world.each<Position, Velocity>([&](Position& pos, Velocity& vel) {
        pos.x += vel.vx * delta_time;
        pos.y += vel.vy * delta_time;
        pos.z += vel.vz * delta_time;
        physics_count++;
    });
    std::cout << "Updated " << physics_count << " entities with physics" << std::endl;

    // 伤害系统 - 遍历有 Health 的实体
    std::cout << "\n--- Damage System (Health) ---" << std::endl;
    int damage_count = 0;

    world.each<Health>([&](Health& health) {
        health.current -= 1;  // 每帧扣 1 点血
        if (health.current < 0) health.current = 0;
        damage_count++;
    });
    std::cout << "Applied damage to " << damage_count << " entities" << std::endl;

    // 带 EntityId 的遍历
    std::cout << "\n--- Entity Report (with EntityId) ---" << std::endl;
    world.each_with_entity<Position, Health>([&](EntityId id, Position& pos, Health& health) {
        std::cout << "  Entity " << id.raw()
                  << ": pos=(" << pos.x << "," << pos.y << ")"
                  << " hp=" << health.current << "/" << health.max
                  << std::endl;
    });
}

// ========================================
// Example 5: Entity Destruction
// ========================================

void example_destruction() {
    print_separator("Example 5: Entity Destruction");

    World world;

    // 创建一批实体
    std::vector<EntityId> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.create_entity(
            Position{static_cast<float>(i * 10), 0.0f, 0.0f},
            Health{100, 100}));
    }

    std::cout << "Created " << entities.size() << " entities" << std::endl;
    std::cout << "Entity count: " << world.entity_count() << std::endl;

    // 销毁偶数索引的实体
    std::cout << "\nDestroying entities at even indices..." << std::endl;
    int destroyed_count = 0;
    for (size_t i = 0; i < entities.size(); i += 2) {
        if (world.destroy_entity(entities[i])) {
            destroyed_count++;
        }
    }
    std::cout << "Destroyed " << destroyed_count << " entities" << std::endl;
    std::cout << "Entity count: " << world.entity_count() << std::endl;

    // 验证存活状态
    std::cout << "\nEntity alive status:" << std::endl;
    for (size_t i = 0; i < entities.size(); ++i) {
        std::cout << "  Entity[" << i << "] (ID: " << entities[i].raw() << "): "
                  << (world.is_alive(entities[i]) ? "alive" : "destroyed") << std::endl;
    }

    // 尝试重复销毁
    std::cout << "\nTrying to destroy already-destroyed entity..." << std::endl;
    bool result = world.destroy_entity(entities[0]);
    std::cout << "Result: " << (result ? "success" : "failed (expected)") << std::endl;
}

// ========================================
// Example 6: Game Loop Simulation
// ========================================

void example_game_loop() {
    print_separator("Example 6: Game Loop Simulation");

    World world;

    // 创建玩家
    EntityId player = world.create_entity(
        Position{0.0f, 0.0f, 0.0f},
        Velocity{2.0f, 1.0f, 0.0f},
        Health{100, 100},
        Name{"Player"});

    // 创建 NPC
    for (int i = 0; i < 3; ++i) {
        (void)world.create_entity(
            Position{static_cast<float>(100 + i * 50), 0.0f, 0.0f},
            Velocity{-0.5f, 0.0f, 0.0f},
            Health{30, 30},
            AIController{"patrol", 2.0f, 0.0f});
    }

    std::cout << "Starting game loop simulation..." << std::endl;
    std::cout << "Entities: " << world.entity_count() << std::endl;

    const float delta_time = 1.0f / 60.0f;  // 60 FPS
    const int frame_count = 5;

    for (int frame = 0; frame < frame_count; ++frame) {
        std::cout << "\n--- Frame " << (frame + 1) << " ---" << std::endl;

        // 移动系统
        world.each<Position, Velocity>([&](Position& pos, Velocity& vel) {
            pos.x += vel.vx * delta_time;
            pos.y += vel.vy * delta_time;
        });

        // AI 系统
        world.each<AIController>([&](AIController& ai) {
            ai.time_since_think += delta_time;
            if (ai.time_since_think >= ai.think_interval) {
                ai.time_since_think = 0.0f;
                // AI 决策逻辑...
            }
        });

        // 打印玩家状态
        if (auto* pos = world.get_component<Position>(player)) {
            std::cout << "Player position: (" << pos->x << ", " << pos->y << ")" << std::endl;
        }

        // 统计存活实体
        int alive_count = 0;
        world.each<Health>([&](Health& h) {
            if (h.current > 0) alive_count++;
        });
        std::cout << "Alive entities: " << alive_count << std::endl;
    }

    std::cout << "\nGame loop simulation completed!" << std::endl;
}

// ========================================
// Main
// ========================================

int main() {
    std::cout << "Corona Framework - ECS Example" << std::endl;
    std::cout << "===============================" << std::endl;

    try {
        example_basic_usage();
        example_component_modification();
        example_add_remove_components();
        example_iteration();
        example_destruction();
        example_game_loop();

        std::cout << "\n===============================" << std::endl;
        std::cout << "All ECS examples completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
