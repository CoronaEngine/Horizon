# Corona Framework ECS Guide

This document describes the Entity Component System (ECS) module in Corona Framework.

> **Status**: ECS is currently in development (Phase 1 complete). Entity management, queries, and World integration are planned for future phases.

## Overview

The ECS module provides an archetype-based entity storage system optimized for cache-friendly data access and high-performance iteration. Key features:

- **Archetype Storage**: Entities with identical component sets are stored together
- **SoA Layout**: Structure-of-Arrays memory layout for optimal cache utilization
- **Type Safety**: C++20 concepts ensure compile-time type checking
- **Zero-cost Abstractions**: Direct pointer arithmetic for O(1) component access

## Core Concepts

| Term | Description |
|------|-------------|
| **Component** | Pure data struct satisfying `Component` concept |
| **Archetype** | Storage for entities sharing the same component types |
| **Chunk** | Fixed-size memory block (16KB) within an Archetype |
| **Signature** | Set of component types uniquely identifying an Archetype |

## Headers

```
include/corona/kernel/ecs/
├── ecs_types.h           // Basic types (EntityId, ArchetypeId, EntityLocation)
├── component.h           // Component concept, ComponentTypeInfo, ComponentRegistry
├── archetype_signature.h // ArchetypeSignature for component set identification
├── archetype_layout.h    // Memory layout calculation
├── chunk.h               // Chunk memory management
└── archetype.h           // Main Archetype class
```

## 1. Defining Components

Components are plain data structs that satisfy the `Component` concept:

```cpp
#include "corona/kernel/ecs/component.h"

using namespace Corona::Kernel::ECS;

// Position component
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Velocity component  
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

// Health component
struct Health {
    int current = 100;
    int max = 100;
};

// All components must satisfy the Component concept
static_assert(Component<Position>);
static_assert(Component<Velocity>);
static_assert(Component<Health>);
```

### Component Requirements

The `Component` concept requires:
- Default constructible (`std::is_default_constructible_v<T>`)
- Move constructible (`std::is_move_constructible_v<T>`)
- Destructible (`std::is_destructible_v<T>`)

Non-trivial types (e.g., `std::string`) are supported but have slightly higher overhead.

### Registering Components

Before using components with Archetypes, register them with the `ComponentRegistry`:

```cpp
CORONA_REGISTER_COMPONENT(Position);
CORONA_REGISTER_COMPONENT(Velocity);
CORONA_REGISTER_COMPONENT(Health);
```

## 2. Creating Archetypes

### Using ArchetypeSignature

```cpp
#include "corona/kernel/ecs/archetype.h"

using namespace Corona::Kernel::ECS;

// Create a signature from component types
auto signature = ArchetypeSignature::create<Position, Velocity, Health>();

// Create an Archetype with a unique ID
Archetype archetype(1, signature);

// Check component membership
if (archetype.has_component<Position>()) {
    // ...
}
```

### Dynamic Signature Building

```cpp
ArchetypeSignature signature;
signature.add<Position>();
signature.add<Velocity>();

// Remove a component type
signature.remove<Velocity>();

// Check containment
if (signature.contains<Position>()) {
    // ...
}
```

## 3. Entity Operations

### Allocating Entities

```cpp
// Allocate a new entity slot
EntityLocation loc = archetype.allocate_entity();

// loc.chunk_index    - which Chunk the entity is in
// loc.index_in_chunk - index within that Chunk
```

### Setting Component Values

```cpp
// Set component data
archetype.set_component<Position>(loc, Position{10.0f, 20.0f, 0.0f});
archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
archetype.set_component<Health>(loc, Health{80, 100});
```

### Reading Component Values

```cpp
// Get component pointer (returns nullptr if type not in archetype)
Position* pos = archetype.get_component<Position>(loc);
if (pos) {
    pos->x += 1.0f;
}

// Const access
const Health* health = archetype.get_component<Health>(loc);
```

### Deallocating Entities

The system uses **swap-and-pop** for O(1) deletion while maintaining data compactness:

```cpp
// Deallocate returns the moved entity's original location (if any)
auto moved_from = archetype.deallocate_entity(loc);

if (moved_from.has_value()) {
    // An entity was moved from moved_from to loc
    // Update your entity-to-location mappings accordingly
}
```

## 4. Batch Iteration (Recommended)

For best performance, iterate over Chunks directly:

```cpp
// Physics update example
float delta_time = 1.0f / 60.0f;

for (auto& chunk : archetype.chunks()) {
    auto positions = chunk.get_components<Position>();
    auto velocities = chunk.get_components<Velocity>();
    
    // Process all entities in this chunk
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        positions[i].x += velocities[i].vx * delta_time;
        positions[i].y += velocities[i].vy * delta_time;
        positions[i].z += velocities[i].vz * delta_time;
    }
}
```

### Benefits of Chunk Iteration

1. **Cache Friendly**: Component arrays are contiguous in memory
2. **SIMD Ready**: Enables auto-vectorization by the compiler
3. **Minimal Overhead**: Direct pointer access, no virtual calls

## 5. Memory Layout

### SoA (Structure of Arrays)

Each Chunk uses SoA layout for optimal cache utilization:

```
Chunk (16KB):
┌─────────────────────────────────────────────────────┐
│ [Position_0][Position_1]...[Position_N]             │
├─────────────────────────────────────────────────────┤
│ [Velocity_0][Velocity_1]...[Velocity_N]             │
├─────────────────────────────────────────────────────┤
│ [Health_0][Health_1]...[Health_N]                   │
└─────────────────────────────────────────────────────┘
```

### Layout Information

```cpp
const auto& layout = archetype.layout();

// Entities per chunk (calculated based on component sizes)
std::size_t capacity = layout.entities_per_chunk;

// Find specific component layout
const ComponentLayout* pos_layout = layout.find_component<Position>();
if (pos_layout) {
    std::size_t offset = pos_layout->array_offset;
    std::size_t size = pos_layout->size;
    std::size_t alignment = pos_layout->alignment;
}
```

## 6. Complete Example

```cpp
#include "corona/kernel/ecs/archetype.h"

using namespace Corona::Kernel::ECS;

struct Position { float x, y, z; };
struct Velocity { float vx, vy, vz; };

int main() {
    // Register components
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    
    // Create archetype
    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);
    
    // Spawn 1000 entities
    std::vector<EntityLocation> entities;
    for (int i = 0; i < 1000; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, {float(i), 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, {1.0f, 0.0f, 0.0f});
        entities.push_back(loc);
    }
    
    // Game loop
    float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 100; ++frame) {
        // Update all entities
        for (auto& chunk : archetype.chunks()) {
            auto positions = chunk.get_components<Position>();
            auto velocities = chunk.get_components<Velocity>();
            
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                positions[i].x += velocities[i].vx * dt;
                positions[i].y += velocities[i].vy * dt;
                positions[i].z += velocities[i].vz * dt;
            }
        }
    }
    
    return 0;
}
```

## 7. Performance Considerations

### Do's

- ✅ Use chunk iteration for bulk updates
- ✅ Register components before creating Archetypes
- ✅ Prefer trivially copyable components for best performance
- ✅ Batch entity creation/deletion when possible

### Don'ts

- ❌ Access components by EntityLocation in tight loops (use chunk iteration)
- ❌ Store pointers to components across frame boundaries (may be invalidated)
- ❌ Forget to handle swap-and-pop when tracking entity locations

## 8. Thread Safety

**Current Status**: Archetypes are **not thread-safe**. External synchronization is required for:

- Entity allocation/deallocation
- Component modification

**Safe Operations** (with immutable Archetype structure):
- Concurrent read-only iteration over different Chunks
- Reading component values

## 9. Roadmap

| Phase | Status | Features |
|-------|--------|----------|
| Phase 1 | ✅ Complete | Component, Signature, Layout, Chunk, Archetype |
| Phase 2 | 🔲 Planned | EntityId generation, Entity-Location mapping, Migration |
| Phase 3 | 🔲 Planned | Query system, Archetype graph |
| Phase 4 | 🔲 Planned | World class, System integration |

## References

- Design Document: [doc/design/archetype_design.md](design/archetype_design.md)
- Tests: `tests/kernel/archetype_test.cpp`
- Headers: `include/corona/kernel/ecs/`
