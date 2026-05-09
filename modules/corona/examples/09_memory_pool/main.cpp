// Memory Pool Example - High-Performance Memory Allocation
// Demonstrates ObjectPool, LinearArena, FrameArena usage for game development

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "corona/kernel/memory/memory_pool.h"

using namespace Corona::Kernal::Memory;

// ========================================
// Define Example Data Structures
// ========================================

/// 游戏粒子
struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float life = 1.0f;
    float size = 1.0f;
    uint32_t color = 0xFFFFFFFF;

    Particle() = default;
    Particle(float px, float py, float pvx, float pvy)
        : x(px), y(py), vx(pvx), vy(pvy), life(1.0f) {}
};

/// 游戏事件
struct GameEvent {
    int type = 0;
    int source_id = 0;
    int target_id = 0;
    float value = 0.0f;
    std::string message;
};

/// 网络消息
struct NetworkMessage {
    uint32_t id = 0;
    uint32_t sequence = 0;
    std::vector<uint8_t> payload;

    NetworkMessage() = default;
    NetworkMessage(uint32_t mid, uint32_t seq) : id(mid), sequence(seq) {}
};

/// 帧渲染命令
struct RenderCommand {
    int type = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t texture_id = 0;
    float rotation = 0.0f;
};

// ========================================
// Helper Functions
// ========================================

void print_separator(const std::string& title) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

void print_pool_stats(const PoolStats& stats, const std::string& name) {
    std::cout << name << " Stats:" << std::endl;
    std::cout << "  Total allocations: " << stats.allocation_count << std::endl;
    std::cout << "  Total deallocations: " << stats.deallocation_count << std::endl;
    std::cout << "  Peak memory: " << stats.peak_memory << " bytes" << std::endl;
    std::cout << "  Total memory: " << stats.total_memory << " bytes" << std::endl;
}

// ========================================
// Example 1: ObjectPool - Particle System
// ========================================

void example_object_pool() {
    print_separator("Example 1: ObjectPool - Particle System");

    // 创建粒子对象池（初始容量 1000，线程安全）
    ObjectPool<Particle> particle_pool(1000, true);

    std::cout << "Particle Pool Created:" << std::endl;
    std::cout << "  Initial capacity: " << particle_pool.capacity() << std::endl;
    std::cout << "  Block size: " << particle_pool.block_size() << " bytes" << std::endl;
    std::cout << "  Active particles: " << particle_pool.size() << std::endl;

    // 创建粒子
    std::vector<Particle*> particles;
    std::cout << "\nCreating 100 particles..." << std::endl;

    for (int i = 0; i < 100; ++i) {
        float angle = static_cast<float>(i) * 0.1f;
        Particle* p = particle_pool.create(
            0.0f, 0.0f,               // position
            std::cos(angle) * 10.0f,  // velocity x
            std::sin(angle) * 10.0f   // velocity y
        );
        p->life = 1.0f - static_cast<float>(i) / 100.0f;
        p->color = 0xFF0000FF + (i * 0x010100);
        particles.push_back(p);
    }

    std::cout << "  Active particles: " << particle_pool.size() << std::endl;
    std::cout << "  Available slots: " << particle_pool.available() << std::endl;

    // 模拟粒子更新
    std::cout << "\nSimulating particle update..." << std::endl;
    float delta_time = 0.016f;

    for (auto* p : particles) {
        p->x += p->vx * delta_time;
        p->y += p->vy * delta_time;
        p->life -= delta_time;
    }

    // 销毁生命值耗尽的粒子
    std::cout << "\nDestroying dead particles (life <= 0.5)..." << std::endl;
    int destroyed = 0;

    for (auto it = particles.begin(); it != particles.end();) {
        if ((*it)->life <= 0.5f) {
            particle_pool.destroy(*it);
            it = particles.erase(it);
            destroyed++;
        } else {
            ++it;
        }
    }

    std::cout << "  Destroyed: " << destroyed << " particles" << std::endl;
    std::cout << "  Remaining: " << particle_pool.size() << " particles" << std::endl;
    std::cout << "  Available slots: " << particle_pool.available() << std::endl;

    // 打印统计
    print_pool_stats(particle_pool.stats(), "\nParticle Pool");

    // 清理剩余粒子
    for (auto* p : particles) {
        particle_pool.destroy(p);
    }
}

// ========================================
// Example 2: ObjectPool - Event Queue
// ========================================

void example_event_queue() {
    print_separator("Example 2: ObjectPool - Event Queue");

    ObjectPool<GameEvent> event_pool(256, true);

    std::cout << "Event Pool Created:" << std::endl;
    std::cout << "  Capacity: " << event_pool.capacity() << std::endl;

    // 发送事件
    std::vector<GameEvent*> pending_events;

    std::cout << "\nQueuing game events..." << std::endl;

    // 创建不同类型的事件
    auto* attack_event = event_pool.create();
    attack_event->type = 1;  // ATTACK
    attack_event->source_id = 1;
    attack_event->target_id = 2;
    attack_event->value = 25.0f;
    attack_event->message = "Player attacks Enemy";
    pending_events.push_back(attack_event);

    auto* heal_event = event_pool.create();
    heal_event->type = 2;  // HEAL
    heal_event->source_id = 3;
    heal_event->target_id = 1;
    heal_event->value = 15.0f;
    heal_event->message = "Healer heals Player";
    pending_events.push_back(heal_event);

    auto* spawn_event = event_pool.create();
    spawn_event->type = 3;  // SPAWN
    spawn_event->source_id = 0;
    spawn_event->target_id = 4;
    spawn_event->value = 0.0f;
    spawn_event->message = "New enemy spawned";
    pending_events.push_back(spawn_event);

    std::cout << "  Queued events: " << pending_events.size() << std::endl;
    std::cout << "  Active in pool: " << event_pool.size() << std::endl;

    // 处理事件
    std::cout << "\nProcessing events:" << std::endl;
    for (auto* event : pending_events) {
        std::cout << "  [Type " << event->type << "] " << event->message
                  << " (value: " << event->value << ")" << std::endl;
        event_pool.destroy(event);
    }

    std::cout << "\nAfter processing:" << std::endl;
    std::cout << "  Active in pool: " << event_pool.size() << std::endl;

    print_pool_stats(event_pool.stats(), "\nEvent Pool");
}

// ========================================
// Example 3: LinearArena - Temporary Allocations
// ========================================

void example_linear_arena() {
    print_separator("Example 3: LinearArena - Temporary Allocations");

    // 创建 1MB 线性分配器
    LinearArena arena(1024 * 1024);

    std::cout << "Linear Arena Created:" << std::endl;
    std::cout << "  Capacity: " << arena.capacity() << " bytes (1 MB)" << std::endl;
    std::cout << "  Used: " << arena.used() << " bytes" << std::endl;

    // 分配临时数据
    std::cout << "\nAllocating temporary data..." << std::endl;

    // 分配 float 数组
    float* positions = arena.allocate_array<float>(1000);
    std::cout << "  Allocated 1000 floats: " << arena.used() << " bytes used" << std::endl;

    // 初始化数据
    for (int i = 0; i < 1000; ++i) {
        positions[i] = static_cast<float>(i) * 0.1f;
    }

    // 分配结构体数组
    RenderCommand* commands = arena.allocate_array<RenderCommand>(100);
    std::cout << "  Allocated 100 RenderCommands: " << arena.used() << " bytes used" << std::endl;

    for (int i = 0; i < 100; ++i) {
        commands[i].type = i % 5;
        commands[i].x = static_cast<float>(i * 10);
        commands[i].y = static_cast<float>(i * 5);
    }

    // 分配单个对象
    auto* single_particle = arena.create<Particle>(10.0f, 20.0f, 1.0f, 2.0f);
    std::cout << "  Allocated 1 Particle: " << arena.used() << " bytes used" << std::endl;
    std::cout << "    Particle pos: (" << single_particle->x << ", " << single_particle->y << ")" << std::endl;

    // 显示使用情况
    std::cout << "\nArena status:" << std::endl;
    std::cout << "  Total used: " << arena.used() << " bytes" << std::endl;
    std::cout << "  Available: " << arena.available() << " bytes" << std::endl;
    std::cout << "  Utilization: " << (arena.utilization() * 100.0) << "%" << std::endl;

    // 重置分配器
    std::cout << "\nResetting arena..." << std::endl;
    arena.reset();
    std::cout << "  Used after reset: " << arena.used() << " bytes" << std::endl;
    std::cout << "  Available: " << arena.available() << " bytes" << std::endl;
}

// ========================================
// Example 4: FrameArena - Double-Buffered Frame Allocator
// ========================================

void example_frame_arena() {
    print_separator("Example 4: FrameArena - Double-Buffered Allocator");

    // 创建帧分配器（每个缓冲区 256KB）
    FrameArena frame_arena(256 * 1024);

    std::cout << "Frame Arena Created:" << std::endl;
    std::cout << "  Buffer capacity: " << frame_arena.buffer_capacity() << " bytes" << std::endl;
    std::cout << "  Current buffer index: " << frame_arena.current_index() << std::endl;

    // 模拟游戏帧
    const int FRAME_COUNT = 3;

    for (int frame = 0; frame < FRAME_COUNT; ++frame) {
        std::cout << "\n--- Frame " << (frame + 1) << " ---" << std::endl;

        // 帧开始
        frame_arena.begin_frame();
        std::cout << "  Buffer " << frame_arena.current_index() << " active" << std::endl;

        // 分配渲染命令
        int command_count = 50 + frame * 20;
        RenderCommand* commands = frame_arena.allocate_array<RenderCommand>(command_count);
        std::cout << "  Allocated " << command_count << " render commands" << std::endl;

        for (int i = 0; i < command_count; ++i) {
            commands[i].type = i % 3;
            commands[i].x = static_cast<float>(i * 10);
            commands[i].y = static_cast<float>(i * 5);
            commands[i].texture_id = i % 10;
        }

        // 分配临时粒子数据
        int particle_count = 100;
        Particle* frame_particles = frame_arena.allocate_array<Particle>(particle_count);
        std::cout << "  Allocated " << particle_count << " particles" << std::endl;

        for (int i = 0; i < particle_count; ++i) {
            frame_particles[i].x = static_cast<float>(i);
            frame_particles[i].y = static_cast<float>(i * 2);
            frame_particles[i].life = 1.0f;
        }

        std::cout << "  Current buffer used: " << frame_arena.current_used() << " bytes" << std::endl;
        std::cout << "  Current buffer available: " << frame_arena.current_available() << " bytes" << std::endl;

        // 帧结束（交换缓冲区）
        frame_arena.end_frame();
    }

    // 重置所有缓冲区
    std::cout << "\nResetting all buffers..." << std::endl;
    frame_arena.reset_all();
    std::cout << "  Current used: " << frame_arena.current_used() << " bytes" << std::endl;
}

// ========================================
// Example 5: FixedPool - Low-Level Block Allocator
// ========================================

void example_fixed_pool() {
    print_separator("Example 5: FixedPool - Low-Level Block Allocator");

    // 创建固定大小块内存池
    PoolConfig config{
        .block_size = 128,      // 每块 128 字节
        .block_alignment = 64,  // 64 字节对齐
        .chunk_size = 4096,     // 每个 Chunk 4KB
        .initial_chunks = 2,    // 初始 2 个 Chunk
        .max_chunks = 10,       // 最多 10 个 Chunk
        .thread_safe = true,    // 线程安全
        .enable_debug = false   // 关闭调试
    };

    FixedPool pool(config);

    std::cout << "Fixed Pool Created:" << std::endl;
    std::cout << "  Block size: " << pool.block_size() << " bytes" << std::endl;
    std::cout << "  Total blocks: " << pool.total_blocks() << std::endl;
    std::cout << "  Free blocks: " << pool.free_blocks() << std::endl;
    std::cout << "  Used blocks: " << pool.used_blocks() << std::endl;
    std::cout << "  Chunk count: " << pool.chunk_count() << std::endl;

    // 分配一批块
    std::vector<void*> blocks;
    const int ALLOC_COUNT = 50;

    std::cout << "\nAllocating " << ALLOC_COUNT << " blocks..." << std::endl;
    for (int i = 0; i < ALLOC_COUNT; ++i) {
        void* block = pool.allocate();
        if (block) {
            // 写入测试数据
            std::memset(block, i & 0xFF, pool.block_size());
            blocks.push_back(block);
        }
    }

    std::cout << "  Allocated: " << blocks.size() << " blocks" << std::endl;
    std::cout << "  Total blocks: " << pool.total_blocks() << std::endl;
    std::cout << "  Free blocks: " << pool.free_blocks() << std::endl;
    std::cout << "  Used blocks: " << pool.used_blocks() << std::endl;

    // 释放一半
    std::cout << "\nDeallocating half of the blocks..." << std::endl;
    for (size_t i = 0; i < blocks.size() / 2; ++i) {
        pool.deallocate(blocks[i]);
    }
    blocks.erase(blocks.begin(), blocks.begin() + blocks.size() / 2);

    std::cout << "  Free blocks: " << pool.free_blocks() << std::endl;
    std::cout << "  Used blocks: " << pool.used_blocks() << std::endl;

    // 释放剩余
    for (void* block : blocks) {
        pool.deallocate(block);
    }

    std::cout << "\nAfter full deallocation:" << std::endl;
    std::cout << "  Free blocks: " << pool.free_blocks() << std::endl;
    std::cout << "  Used blocks: " << pool.used_blocks() << std::endl;

    print_pool_stats(pool.stats(), "\nFixed Pool");

    // 收缩内存
    std::cout << "\nShrinking to fit..." << std::endl;
    pool.shrink_to_fit();
    std::cout << "  Chunk count after shrink: " << pool.chunk_count() << std::endl;
}

// ========================================
// Example 6: Performance Comparison
// ========================================

void example_performance() {
    print_separator("Example 6: Performance Comparison");

    const int ITERATIONS = 10000;
    const int OBJECTS_PER_ITER = 100;

    std::cout << "Comparing allocation performance:" << std::endl;
    std::cout << "  Iterations: " << ITERATIONS << std::endl;
    std::cout << "  Objects per iteration: " << OBJECTS_PER_ITER << std::endl;

    // 使用 ObjectPool
    {
        ObjectPool<Particle> pool(ITERATIONS * OBJECTS_PER_ITER, false);
        std::vector<Particle*> particles;
        particles.reserve(OBJECTS_PER_ITER);

        auto start = std::chrono::high_resolution_clock::now();

        for (int iter = 0; iter < ITERATIONS; ++iter) {
            // 分配
            for (int i = 0; i < OBJECTS_PER_ITER; ++i) {
                particles.push_back(pool.create(1.0f, 2.0f, 3.0f, 4.0f));
            }
            // 释放
            for (auto* p : particles) {
                pool.destroy(p);
            }
            particles.clear();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "\n  ObjectPool: " << duration.count() << " us" << std::endl;
        std::cout << "    Avg per alloc+dealloc: "
                  << (static_cast<double>(duration.count()) / (ITERATIONS * OBJECTS_PER_ITER))
                  << " us" << std::endl;
    }

    // 使用 new/delete
    {
        std::vector<Particle*> particles;
        particles.reserve(OBJECTS_PER_ITER);

        auto start = std::chrono::high_resolution_clock::now();

        for (int iter = 0; iter < ITERATIONS; ++iter) {
            // 分配
            for (int i = 0; i < OBJECTS_PER_ITER; ++i) {
                particles.push_back(new Particle(1.0f, 2.0f, 3.0f, 4.0f));
            }
            // 释放
            for (auto* p : particles) {
                delete p;
            }
            particles.clear();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "\n  new/delete: " << duration.count() << " us" << std::endl;
        std::cout << "    Avg per alloc+dealloc: "
                  << (static_cast<double>(duration.count()) / (ITERATIONS * OBJECTS_PER_ITER))
                  << " us" << std::endl;
    }

    // 使用 LinearArena（批量分配）
    {
        LinearArena arena(OBJECTS_PER_ITER * sizeof(Particle) * 2);

        auto start = std::chrono::high_resolution_clock::now();

        for (int iter = 0; iter < ITERATIONS; ++iter) {
            // 分配
            for (int i = 0; i < OBJECTS_PER_ITER; ++i) {
                (void)arena.create<Particle>(1.0f, 2.0f, 3.0f, 4.0f);
            }
            // 重置（批量释放）
            arena.reset();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "\n  LinearArena: " << duration.count() << " us" << std::endl;
        std::cout << "    Avg per alloc: "
                  << (static_cast<double>(duration.count()) / (ITERATIONS * OBJECTS_PER_ITER))
                  << " us" << std::endl;
    }
}

// ========================================
// Main
// ========================================

int main() {
    std::cout << "Corona Framework - Memory Pool Example" << std::endl;
    std::cout << "=======================================" << std::endl;

    try {
        example_object_pool();
        example_event_queue();
        example_linear_arena();
        example_frame_arena();
        example_fixed_pool();
        example_performance();

        std::cout << "\n=======================================" << std::endl;
        std::cout << "All memory pool examples completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
