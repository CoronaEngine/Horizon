# World 集成设计文档（Phase 4）

## 1. 概述

本文档描述 Corona Framework ECS 模块中 **World（世界）** 的设计方案。World 是 ECS 的顶层容器，整合 EntityManager、Archetype 存储和 Query 系统，提供统一的实体操作接口。

### 1.1 设计目标

- **统一入口**：提供创建/销毁实体、增删组件的一站式 API
- **自动化管理**：自动处理 Archetype 创建、实体迁移、缓存更新
- **System 集成**：支持 System 注册和调度
- **Command Buffer**：延迟命令执行，支持并行写入
- **可扩展性**：支持多 World 场景

### 1.2 核心概念

| 术语 | 说明 |
|------|------|
| **World** | ECS 上下文，管理所有实体和组件 |
| **CommandBuffer** | 命令缓冲区，延迟执行结构变更 |
| **System** | 处理逻辑单元，操作 World 中的实体 |
| **Schedule** | 系统调度器，管理执行顺序和并行化 |

---

## 2. World 设计

### 2.1 接口设计

```cpp
namespace Corona::Kernel::ECS {

/// ECS 世界
class World {
public:
    World();
    ~World();
    
    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    
    // 支持移动
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    
    // ========================================
    // 实体生命周期
    // ========================================
    
    /**
     * @brief 创建空实体
     * @return 新实体的 ID
     */
    [[nodiscard]] EntityId create_entity();
    
    /**
     * @brief 创建带组件的实体
     * @tparam Components 组件类型列表
     * @param components 组件初始值
     * @return 新实体的 ID
     */
    template <Component... Ts>
    [[nodiscard]] EntityId create_entity(Ts&&... components);
    
    /**
     * @brief 销毁实体
     * @param entity 实体 ID
     * @return 销毁成功返回 true
     */
    bool destroy_entity(EntityId entity);
    
    /**
     * @brief 检查实体是否存活
     * @param entity 实体 ID
     * @return 存活返回 true
     */
    [[nodiscard]] bool is_alive(EntityId entity) const;
    
    // ========================================
    // 组件操作
    // ========================================
    
    /**
     * @brief 添加组件
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @param component 组件值
     * @return 添加成功返回 true
     */
    template <Component T>
    bool add_component(EntityId entity, T&& component);
    
    /**
     * @brief 移除组件
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 移除成功返回 true
     */
    template <Component T>
    bool remove_component(EntityId entity);
    
    /**
     * @brief 获取组件
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 组件指针，不存在返回 nullptr
     */
    template <Component T>
    [[nodiscard]] T* get_component(EntityId entity);
    
    template <Component T>
    [[nodiscard]] const T* get_component(EntityId entity) const;
    
    /**
     * @brief 设置组件值
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @param component 组件值
     * @return 设置成功返回 true
     */
    template <Component T>
    bool set_component(EntityId entity, T&& component);
    
    /**
     * @brief 检查实体是否有指定组件
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 有该组件返回 true
     */
    template <Component T>
    [[nodiscard]] bool has_component(EntityId entity) const;
    
    // ========================================
    // 查询
    // ========================================
    
    /**
     * @brief 创建查询
     * @tparam Components 查询描述
     * @return Query 对象
     */
    template <typename... Components>
    [[nodiscard]] Query<Components...> query();
    
    // ========================================
    // Command Buffer
    // ========================================
    
    /**
     * @brief 获取命令缓冲区
     * @return CommandBuffer 引用
     */
    [[nodiscard]] CommandBuffer& command_buffer();
    
    /**
     * @brief 执行所有挂起的命令
     */
    void flush_commands();
    
    // ========================================
    // 统计信息
    // ========================================
    
    /// 获取实体总数
    [[nodiscard]] std::size_t entity_count() const;
    
    /// 获取 Archetype 数量
    [[nodiscard]] std::size_t archetype_count() const;

private:
    /// 获取或创建 Archetype
    Archetype& get_or_create_archetype(const ArchetypeSignature& signature);
    
    /// 执行实体迁移
    void migrate_entity(EntityId entity, Archetype& target);

    EntityManager entity_manager_;                          ///< 实体管理
    std::unordered_map<std::size_t, std::unique_ptr<Archetype>> archetypes_; ///< Archetype 存储
    QueryCache query_cache_;                                ///< 查询缓存
    std::unique_ptr<CommandBuffer> command_buffer_;         ///< 命令缓冲
    ArchetypeId next_archetype_id_ = 0;                     ///< Archetype ID 分配
};

} // namespace Corona::Kernel::ECS
```

### 2.2 实现细节

#### 创建带组件的实体

```cpp
template <Component... Ts>
EntityId World::create_entity(Ts&&... components) {
    // 1. 构建签名
    auto signature = ArchetypeSignature::create<std::decay_t<Ts>...>();
    
    // 2. 获取或创建 Archetype
    Archetype& archetype = get_or_create_archetype(signature);
    
    // 3. 分配实体 ID
    EntityId entity = entity_manager_.allocate();
    
    // 4. 在 Archetype 中分配槽位
    EntityLocation location = archetype.allocate_entity();
    
    // 5. 更新实体记录
    entity_manager_.update_location(entity, archetype.id(), location);
    
    // 6. 设置组件值
    (archetype.set_component<std::decay_t<Ts>>(location, std::forward<Ts>(components)), ...);
    
    return entity;
}
```

#### 添加组件

```cpp
template <Component T>
bool World::add_component(EntityId entity, T&& component) {
    if (!is_alive(entity)) return false;
    
    const EntityRecord* record = entity_manager_.get_record(entity);
    Archetype* source = get_archetype(record->archetype_id);
    
    // 检查是否已有该组件
    if (source->has_component<T>()) {
        // 直接更新值
        return set_component(entity, std::forward<T>(component));
    }
    
    // 构建新签名
    auto new_signature = source->signature();
    new_signature.add<T>();
    
    // 获取目标 Archetype
    Archetype& target = get_or_create_archetype(new_signature);
    
    // 迁移实体
    migrate_entity(entity, target);
    
    // 设置新组件值
    const EntityRecord* new_record = entity_manager_.get_record(entity);
    target.set_component<T>(new_record->location, std::forward<T>(component));
    
    return true;
}
```

---

## 3. CommandBuffer 设计

### 3.1 概念

CommandBuffer 允许延迟执行结构变更操作，适用于：
- 在 Query 迭代中安全地创建/销毁实体
- 并行系统中收集命令，统一执行
- 事务式操作

### 3.2 命令类型

```cpp
namespace Corona::Kernel::ECS {

/// 命令类型
enum class CommandType {
    CreateEntity,
    DestroyEntity,
    AddComponent,
    RemoveComponent,
    SetComponent
};

/// 命令基类
struct Command {
    CommandType type;
    EntityId entity;
    
    virtual ~Command() = default;
    virtual void execute(World& world) = 0;
};

/// 创建实体命令
struct CreateEntityCommand : Command {
    std::function<void(EntityId)> callback;  ///< 创建后回调
    
    void execute(World& world) override {
        EntityId id = world.create_entity();
        if (callback) callback(id);
    }
};

/// 销毁实体命令
struct DestroyEntityCommand : Command {
    void execute(World& world) override {
        world.destroy_entity(entity);
    }
};

/// 添加组件命令（类型擦除）
struct AddComponentCommand : Command {
    ComponentTypeId component_type;
    std::function<void(void*)> initializer;  ///< 组件初始化函数
    
    void execute(World& world) override;
};

} // namespace Corona::Kernel::ECS
```

### 3.3 CommandBuffer 接口

```cpp
namespace Corona::Kernel::ECS {

/// 命令缓冲区
class CommandBuffer {
public:
    /**
     * @brief 记录创建实体命令
     * @param callback 实体创建后的回调
     */
    void create_entity(std::function<void(EntityId)> callback = nullptr);
    
    /**
     * @brief 记录创建带组件的实体命令
     * @tparam Ts 组件类型
     * @param components 组件值
     * @param callback 创建后回调
     */
    template <Component... Ts>
    void create_entity_with(std::tuple<Ts...> components,
                           std::function<void(EntityId)> callback = nullptr);
    
    /**
     * @brief 记录销毁实体命令
     * @param entity 实体 ID
     */
    void destroy_entity(EntityId entity);
    
    /**
     * @brief 记录添加组件命令
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @param component 组件值
     */
    template <Component T>
    void add_component(EntityId entity, T component);
    
    /**
     * @brief 记录移除组件命令
     * @tparam T 组件类型
     * @param entity 实体 ID
     */
    template <Component T>
    void remove_component(EntityId entity);
    
    /**
     * @brief 执行所有挂起的命令
     * @param world 目标 World
     */
    void flush(World& world);
    
    /**
     * @brief 清空所有命令（不执行）
     */
    void clear();
    
    /**
     * @brief 获取挂起命令数量
     */
    [[nodiscard]] std::size_t pending_count() const;
    
    /**
     * @brief 检查是否为空
     */
    [[nodiscard]] bool empty() const;

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace Corona::Kernel::ECS
```

### 3.4 使用示例

```cpp
// 在 Query 迭代中安全地销毁实体
auto& cmd = world.command_buffer();

world.query<Read<Health>>().for_each([&](EntityId entity, const Health& health) {
    if (health.current <= 0) {
        cmd.destroy_entity(entity);  // 延迟销毁
    }
});

world.flush_commands();  // 统一执行所有销毁
```

---

## 4. System 集成

### 4.1 ISystem 接口

```cpp
namespace Corona::Kernel::ECS {

/// ECS System 接口
class ISystem {
public:
    virtual ~ISystem() = default;
    
    /// 系统名称
    [[nodiscard]] virtual std::string_view name() const = 0;
    
    /// 执行优先级（越大越先执行）
    [[nodiscard]] virtual int priority() const { return 0; }
    
    /// 初始化
    virtual bool initialize(World& world) { return true; }
    
    /// 更新
    virtual void update(World& world, float delta_time) = 0;
    
    /// 关闭
    virtual void shutdown(World& world) {}
};

} // namespace Corona::Kernel::ECS
```

### 4.2 System 示例

```cpp
class MovementSystem : public ISystem {
public:
    std::string_view name() const override { return "Movement"; }
    int priority() const override { return 100; }
    
    void update(World& world, float dt) override {
        world.query<Write<Position>, Read<Velocity>>()
            .for_each([dt](Position& pos, const Velocity& vel) {
                pos.x += vel.vx * dt;
                pos.y += vel.vy * dt;
                pos.z += vel.vz * dt;
            });
    }
};

class HealthSystem : public ISystem {
public:
    std::string_view name() const override { return "Health"; }
    int priority() const override { return 50; }
    
    void update(World& world, float dt) override {
        auto& cmd = world.command_buffer();
        
        world.query<Read<Health>>()
            .for_each_with_entity([&](EntityId entity, const Health& health) {
                if (health.current <= 0) {
                    cmd.destroy_entity(entity);
                }
            });
    }
};
```

### 4.3 SystemScheduler

```cpp
namespace Corona::Kernel::ECS {

/// 系统调度器
class SystemScheduler {
public:
    /**
     * @brief 注册系统
     * @tparam T 系统类型
     * @param args 构造参数
     * @return 系统引用
     */
    template <typename T, typename... Args>
    T& add_system(Args&&... args);
    
    /**
     * @brief 移除系统
     * @param name 系统名称
     */
    void remove_system(std::string_view name);
    
    /**
     * @brief 初始化所有系统
     * @param world World 引用
     */
    bool initialize_all(World& world);
    
    /**
     * @brief 更新所有系统
     * @param world World 引用
     * @param delta_time 帧时间
     */
    void update_all(World& world, float delta_time);
    
    /**
     * @brief 关闭所有系统
     * @param world World 引用
     */
    void shutdown_all(World& world);

private:
    std::vector<std::unique_ptr<ISystem>> systems_;
    bool initialized_ = false;
};

} // namespace Corona::Kernel::ECS
```

---

## 5. 使用示例

### 5.1 完整游戏循环

```cpp
#include "corona/kernel/ecs/world.h"

using namespace Corona::Kernel::ECS;

// 组件定义
struct Position { float x, y, z; };
struct Velocity { float vx, vy, vz; };
struct Health { int current, max; };
struct EnemyTag {};

int main() {
    // 注册组件
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    CORONA_REGISTER_COMPONENT(Health);
    CORONA_REGISTER_COMPONENT(EnemyTag);
    
    // 创建 World
    World world;
    
    // 创建实体
    for (int i = 0; i < 1000; ++i) {
        world.create_entity(
            Position{float(i), 0.0f, 0.0f},
            Velocity{1.0f, 0.0f, 0.0f},
            Health{100, 100}
        );
    }
    
    // 创建敌人
    for (int i = 0; i < 100; ++i) {
        EntityId enemy = world.create_entity(
            Position{float(i * 10), 100.0f, 0.0f},
            Health{50, 50}
        );
        world.add_component<EnemyTag>(enemy, EnemyTag{});
    }
    
    // 注册系统
    SystemScheduler scheduler;
    scheduler.add_system<MovementSystem>();
    scheduler.add_system<HealthSystem>();
    scheduler.initialize_all(world);
    
    // 游戏循环
    float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 1000; ++frame) {
        scheduler.update_all(world, dt);
        world.flush_commands();
    }
    
    scheduler.shutdown_all(world);
    return 0;
}
```

### 5.2 动态组件操作

```cpp
World world;

// 创建实体
EntityId player = world.create_entity(Position{0, 0, 0});

// 动态添加组件
world.add_component<Velocity>(player, Velocity{1.0f, 0.0f, 0.0f});
world.add_component<Health>(player, Health{100, 100});

// 检查组件
if (world.has_component<Health>(player)) {
    Health* health = world.get_component<Health>(player);
    health->current -= 10;
}

// 移除组件
world.remove_component<Velocity>(player);
```

---

## 6. 文件组织

```
include/corona/kernel/ecs/
├── world.h              // World 类
├── command_buffer.h     // CommandBuffer
├── system.h             // ISystem 接口
├── system_scheduler.h   // SystemScheduler
└── ecs.h                // 统一头文件（包含所有 ECS 头文件）

src/kernel/ecs/
├── world.cpp
├── command_buffer.cpp
└── system_scheduler.cpp
```

---

## 7. 性能考量

### 7.1 时间复杂度

| 操作 | 复杂度 | 说明 |
|-----|--------|------|
| create_entity() | O(1) 摊还 | 可能触发 Archetype 创建 |
| destroy_entity() | O(组件数) | 需要迁移处理 |
| add_component() | O(组件数) | 实体迁移 |
| remove_component() | O(组件数) | 实体迁移 |
| get_component() | O(1) | 直接查找 |
| flush_commands() | O(命令数) | 批量执行 |

### 7.2 优化建议

1. **批量创建**：使用 spawn 模式批量创建相同类型实体
2. **延迟命令**：在 System 中使用 CommandBuffer
3. **预分配**：已知实体数量时预分配容量
4. **避免频繁迁移**：设计时考虑组件集合稳定性

---

## 8. 线程安全

### 8.1 当前设计

World **非线程安全**，需要外部同步：
- 实体创建/销毁：需要独占
- 组件操作：需要独占
- Query 迭代：只读可并行

### 8.2 并行 System 执行

```cpp
// 使用 CommandBuffer 实现安全的并行写入
void parallel_update(World& world, float dt) {
    // 每个线程使用独立的 CommandBuffer
    thread_local CommandBuffer local_cmd;
    
    world.query<Read<Position>>()
        .parallel_for_each([&](EntityId entity, const Position& pos) {
            if (should_destroy(pos)) {
                local_cmd.destroy_entity(entity);
            }
        });
    
    // 合并到主 CommandBuffer
    world.command_buffer().merge(local_cmd);
}
```

---

## 9. 测试计划

| 测试文件 | 测试内容 |
|---------|---------|
| `world_test.cpp` | 实体创建/销毁、组件操作 |
| `world_migration_test.cpp` | 组件添加/移除、迁移验证 |
| `command_buffer_test.cpp` | 命令记录、批量执行 |
| `system_scheduler_test.cpp` | 系统注册、优先级、生命周期 |
| `world_integration_test.cpp` | 完整游戏循环测试 |

---

## 10. 实现优先级

### Phase 4.1：World 核心

- [ ] `World` 基本框架
- [ ] 实体创建/销毁
- [ ] 组件增删改查
- [ ] 实体迁移

### Phase 4.2：CommandBuffer

- [ ] 命令类型定义
- [ ] `CommandBuffer` 实现
- [ ] 延迟执行

### Phase 4.3：System 集成

- [ ] `ISystem` 接口
- [ ] `SystemScheduler`

---

## 参考资料

- [Unity DOTS - World 和 EntityManager](https://docs.unity3d.com/Packages/com.unity.entities@latest)
- [EnTT - Registry](https://github.com/skypjack/entt)
- [Flecs - World](https://github.com/SanderMertens/flecs)
- [Bevy ECS - World 和 Commands](https://bevyengine.org/learn/book/getting-started/ecs/)
