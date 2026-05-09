// Storage Example - Thread-safe Object Pool Usage
// Demonstrates efficient object allocation, access, and iteration using Storage

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "corona/kernel/utils/storage.h"

using namespace Corona::Kernel::Utils;

// ========================================
// Define Example Data Structures
// ========================================

// Game Entity
struct GameEntity {
    int id = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    int health = 100;
    bool active = true;
};

// Player Data
struct Player {
    int player_id = 0;
    std::string username;
    int level = 1;
    int experience = 0;
    int score = 0;
};

// ========================================
// Helper Functions
// ========================================

void print_separator(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// ========================================
// Example 1: Basic Usage - Allocate, Read/Write, Deallocate
// ========================================

void example_basic_usage() {
    print_separator("Example 1: Basic Usage");

    // Create Storage with buffer capacity 4, initial 1 buffer
    Storage<GameEntity, 4, 1> entity_storage;

    std::cout << "Initial capacity: " << entity_storage.capacity() << std::endl;
    std::cout << "Initial count: " << entity_storage.count() << std::endl;

    // Allocate entities
    auto handle1 = entity_storage.allocate();
    if (auto accessor = entity_storage.acquire_write(handle1)) {
        accessor->id = 1;
        accessor->name = "Player";
        accessor->x = 100.0f;
        accessor->y = 200.0f;
        accessor->health = 100;
    }

    auto handle2 = entity_storage.allocate();
    if (auto accessor = entity_storage.acquire_write(handle2)) {
        accessor->id = 2;
        accessor->name = "Enemy";
        accessor->x = 300.0f;
        accessor->y = 400.0f;
        accessor->health = 50;
    }

    std::cout << "Count after allocation: " << entity_storage.count() << std::endl;

    // Read entity
    std::cout << "\nReading entity data:" << std::endl;
    if (auto accessor = entity_storage.acquire_read(handle1)) {
        const auto& entity = *accessor;
        std::cout << "  ID: " << entity.id << ", Name: " << entity.name
                  << ", Pos: (" << entity.x << ", " << entity.y << ")"
                  << ", Health: " << entity.health << std::endl;
    }

    // Modify entity
    if (auto accessor = entity_storage.acquire_write(handle1)) {
        auto& entity = *accessor;
        entity.x += 10.0f;
        entity.y += 20.0f;
        entity.health -= 10;
    }

    std::cout << "\nAfter modification:" << std::endl;
    if (auto accessor = entity_storage.acquire_read(handle1)) {
        const auto& entity = *accessor;
        std::cout << "  ID: " << entity.id << ", Name: " << entity.name
                  << ", Pos: (" << entity.x << ", " << entity.y << ")"
                  << ", Health: " << entity.health << std::endl;
    }

    // Deallocate entity
    entity_storage.deallocate(handle1);

    std::cout << "\nCount after deallocation: " << entity_storage.count() << std::endl;

    // Try reading deallocated entity
    bool success = false;
    if (auto accessor = entity_storage.acquire_read(handle1)) {
        std::cout << "  Data read" << std::endl;
        success = true;
    }
    std::cout << "Attempt to read deallocated entity: " << (success ? "Success" : "Failed (expected)") << std::endl;
}

// ========================================
// Example 2: Dynamic Expansion
// ========================================

void example_dynamic_expansion() {
    print_separator("Example 2: Dynamic Expansion");

    // Buffer capacity 4, initial 1 buffer
    Storage<Player, 4, 1> player_storage;

    std::cout << "Initial capacity: " << player_storage.capacity() << std::endl;

    // Allocate more objects than initial capacity, triggering expansion
    std::vector<Storage<Player, 4, 1>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = player_storage.allocate();
        if (auto accessor = player_storage.acquire_write(handle)) {
            accessor->player_id = i + 1;
            accessor->username = "Player_" + std::to_string(i + 1);
            accessor->level = (i % 5) + 1;
            accessor->experience = i * 100;
            accessor->score = i * 50;
        }
        handles.push_back(handle);

        if (i == 3 || i == 7) {
            std::cout << "Capacity after allocating " << (i + 1) << " objects: "
                      << player_storage.capacity() << std::endl;
        }
    }

    std::cout << "Final capacity: " << player_storage.capacity() << std::endl;
    std::cout << "Occupied count: " << player_storage.count() << std::endl;
}

// ========================================
// Example 3: Batch Iteration
// ========================================

void example_iteration() {
    print_separator("Example 3: Batch Iteration");

    Storage<GameEntity, 8, 1> entity_storage;

    // Create multiple entities
    std::vector<Storage<GameEntity, 8, 1>::ObjectId> handles;
    for (int i = 0; i < 5; ++i) {
        auto handle = entity_storage.allocate();
        if (auto accessor = entity_storage.acquire_write(handle)) {
            accessor->id = i + 1;
            accessor->name = "Entity_" + std::to_string(i + 1);
            accessor->x = static_cast<float>(i * 10);
            accessor->y = static_cast<float>(i * 20);
            accessor->health = 100 - i * 10;
            accessor->active = (i % 2 == 0);
        }
        handles.push_back(handle);
    }

    // Read-only iteration - Print all active entities
    std::cout << "\nActive entities:" << std::endl;
    for (const auto& entity : entity_storage) {
        if (entity.active) {
            std::cout << "  [" << entity.id << "] " << entity.name
                      << " - Health: " << entity.health << std::endl;
        }
    }

    // Write iteration - Update all entity positions
    std::cout << "\nUpdating all entity positions..." << std::endl;
    for (auto& entity : entity_storage) {
        entity.x += 5.0f;
        entity.y += 5.0f;
    }

    // Verify updates
    std::cout << "\nUpdated entity positions:" << std::endl;
    for (const auto& entity : entity_storage) {
        std::cout << "  [" << entity.id << "] Pos: ("
                  << entity.x << ", " << entity.y << ")" << std::endl;
    }

    // Deallocate some entities
    entity_storage.deallocate(handles[1]);
    entity_storage.deallocate(handles[3]);

    // Iterate again (skipping deallocated ones)
    std::cout << "\nIteration after deallocating some entities:" << std::endl;
    int count = 0;
    for (const auto& entity : entity_storage) {
        count++;
        std::cout << "  [" << entity.id << "] " << entity.name << std::endl;
    }
    std::cout << "Actual iteration count: " << count << " (expected 3)" << std::endl;
}

// ========================================
// Example 4: Multithreaded Concurrent Access
// ========================================

void example_multithreading() {
    print_separator("Example 4: Multithreaded Concurrent Access");

    Storage<Player, 16, 1> player_storage;

    // Pre-allocate some players
    std::vector<Storage<Player, 16, 1>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = player_storage.allocate();
        if (auto accessor = player_storage.acquire_write(handle)) {
            accessor->player_id = i + 1;
            accessor->username = "Player_" + std::to_string(i + 1);
            accessor->level = 1;
            accessor->experience = 0;
            accessor->score = 0;
        }
        handles.push_back(handle);
    }

    std::cout << "Initial player count: " << player_storage.count() << std::endl;

    // Launch multiple threads for concurrent updates
    std::vector<std::thread> threads;
    const int num_threads = 4;

    auto worker = [&](int thread_id, int iterations) {
        for (int i = 0; i < iterations; ++i) {
            // Randomly select a player to update
            int player_index = (thread_id + i) % handles.size();
            auto handle = handles[player_index];

            // Update experience and score
            if (auto accessor = player_storage.acquire_write(handle)) {
                auto& player = *accessor;
                player.experience += 10;
                player.score += 5;
                if (player.experience >= 100) {
                    player.level++;
                    player.experience = 0;
                }
            }

            // Brief sleep to simulate real work
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    std::cout << "\nLaunching " << num_threads << " threads for concurrent updates..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i, 25);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Concurrent updates completed, elapsed: " << duration.count() << " ms" << std::endl;

    // Display final results
    std::cout << "\nFinal player data:" << std::endl;
    for (const auto& player : player_storage) {
        std::cout << "  [" << player.player_id << "] " << player.username
                  << " - Level: " << player.level
                  << ", Exp: " << player.experience
                  << ", Score: " << player.score << std::endl;
    }
}

// ========================================
// Example 5: Real-world Scenario - Simple Entity Management System
// ========================================

void example_entity_manager() {
    print_separator("Example 5: Entity Management System");

    Storage<GameEntity, 32, 1> entities;

    std::cout << "Creating game scene..." << std::endl;

    // Create player
    auto player = entities.allocate();
    if (auto accessor = entities.acquire_write(player)) {
        accessor->id = 1;
        accessor->name = "Hero";
        accessor->x = 0.0f;
        accessor->y = 0.0f;
        accessor->health = 100;
        accessor->active = true;
    }

    // Create enemies
    std::vector<Storage<GameEntity, 32, 1>::ObjectId> enemies;
    for (int i = 0; i < 5; ++i) {
        auto enemy = entities.allocate();
        if (auto accessor = entities.acquire_write(enemy)) {
            accessor->id = 100 + i;
            accessor->name = "Enemy_" + std::to_string(i + 1);
            accessor->x = static_cast<float>((i + 1) * 50);
            accessor->y = static_cast<float>((i + 1) * 30);
            accessor->health = 50;
            accessor->active = true;
        }
        enemies.push_back(enemy);
    }

    std::cout << "Total scene entities: " << entities.count() << std::endl;

    // Simulate game loop
    std::cout << "\nSimulating 5 game frames..." << std::endl;
    for (int frame = 1; frame <= 5; ++frame) {
        std::cout << "\n--- Frame " << frame << " ---" << std::endl;

        // Move player
        if (auto accessor = entities.acquire_write(player)) {
            auto& e = *accessor;
            e.x += 10.0f;
            e.y += 5.0f;
        }

        // Update all enemies
        for (auto& e : entities) {
            if (e.id >= 100 && e.active) {  // Enemy
                e.x -= 2.0f;                // Move towards player
                // Simulate taking damage
                if (frame % 2 == 0) {
                    e.health -= 10;
                    if (e.health <= 0) {
                        e.active = false;
                    }
                }
            }
        }

        // Count active enemies
        int alive_count = 0;
        for (const auto& e : entities) {
            if (e.id >= 100 && e.active) {
                alive_count++;
            }
        }

        std::cout << "Remaining enemies: " << alive_count << std::endl;

        // Display player position
        if (auto accessor = entities.acquire_read(player)) {
            const auto& e = *accessor;
            std::cout << "Player position: (" << e.x << ", " << e.y << ")" << std::endl;
        }
    }

    // Clean up dead enemies
    std::cout << "\nCleaning up dead enemies..." << std::endl;
    for (auto enemy_handle : enemies) {
        bool is_dead = true;
        if (auto accessor = entities.acquire_read(enemy_handle)) {
            is_dead = !accessor->active;
        }
        if (is_dead) {
            entities.deallocate(enemy_handle);
        }
    }

    std::cout << "Total entities after cleanup: " << entities.count() << std::endl;
}

// ========================================
// Main Program
// ========================================

int main() {
    std::cout << "=== Corona Framework - Storage Example ===" << std::endl;

    try {
        example_basic_usage();
        example_dynamic_expansion();
        example_iteration();
        example_multithreading();
        example_entity_manager();

        print_separator("All Examples Completed");
        std::cout << "\nStorage provides efficient thread-safe object pools with:" << std::endl;
        std::cout << "  - Dynamic expansion" << std::endl;
        std::cout << "  - Fine-grained concurrency control" << std::endl;
        std::cout << "  - Efficient batch iteration" << std::endl;
        std::cout << "  - Thread-safe access" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
