#include "corona/kernel/ecs/archetype.h"

#include <cassert>

namespace Corona::Kernel::ECS {

Archetype::Archetype(ArchetypeId id, ArchetypeSignature signature, ChunkAllocator* allocator)
    : id_(id), signature_(std::move(signature)), allocator_(allocator) {
    // 计算内存布局
    layout_ = ArchetypeLayout::calculate(signature_);

    // 如果没有提供分配器，使用全局分配器
    if (!allocator_) {
        allocator_ = &get_global_chunk_allocator();
    }
}

Archetype::~Archetype() {
    // unique_ptr 会自动清理 chunks
}

Archetype::Archetype(Archetype&& other) noexcept
    : id_(other.id_),
      signature_(std::move(other.signature_)),
      layout_(std::move(other.layout_)),
      chunks_(std::move(other.chunks_)),
      allocator_(other.allocator_) {
    other.id_ = kInvalidArchetypeId;
    other.allocator_ = nullptr;

    // 更新所有 Chunk 的 layout 指针，使其指向当前对象的 layout_
    for (auto& chunk : chunks_) {
        if (chunk) {
            chunk->rebind_layout(&layout_);
        }
    }
}

Archetype& Archetype::operator=(Archetype&& other) noexcept {
    if (this != &other) {
        id_ = other.id_;
        signature_ = std::move(other.signature_);
        layout_ = std::move(other.layout_);
        chunks_ = std::move(other.chunks_);
        allocator_ = other.allocator_;

        other.id_ = kInvalidArchetypeId;
        other.allocator_ = nullptr;

        // 更新所有 Chunk 的 layout 指针，使其指向当前对象的 layout_
        for (auto& chunk : chunks_) {
            if (chunk) {
                chunk->rebind_layout(&layout_);
            }
        }
    }
    return *this;
}

std::size_t Archetype::entity_count() const {
    std::size_t total = 0;
    for (const auto& chunk : chunks_) {
        total += chunk->size();
    }
    return total;
}

bool Archetype::has_component(ComponentTypeId type_id) const {
    return signature_.contains(type_id);
}

EntityLocation Archetype::allocate_entity() {
    // 确保有可用空间
    ensure_capacity();

    // 找到有空闲空间的 Chunk
    auto chunk_index = find_available_chunk();
    assert(chunk_index >= 0 && "No available chunk after ensure_capacity");

    // 在该 Chunk 中分配
    auto& chunk = *chunks_[static_cast<std::size_t>(chunk_index)];
    auto index_in_chunk = chunk.allocate();

    return EntityLocation{static_cast<std::size_t>(chunk_index), index_in_chunk};
}

std::optional<EntityLocation> Archetype::deallocate_entity(const EntityLocation& location) {
    if (location.chunk_index >= chunks_.size()) {
        return std::nullopt;  // 无效的 chunk 索引
    }

    auto& chunk = *chunks_[location.chunk_index];
    if (location.index_in_chunk >= chunk.size()) {
        return std::nullopt;  // 无效的实体索引
    }

    auto moved_from = chunk.deallocate(location.index_in_chunk);

    if (moved_from.has_value()) {
        // 返回被移动实体的原位置
        return EntityLocation{location.chunk_index, *moved_from};
    }

    return std::nullopt;
}

void* Archetype::get_component(const EntityLocation& location, ComponentTypeId type_id) {
    if (location.chunk_index >= chunks_.size()) {
        return nullptr;
    }

    auto& chunk = *chunks_[location.chunk_index];
    return chunk.get_component_at(type_id, location.index_in_chunk);
}

const void* Archetype::get_component(const EntityLocation& location,
                                     ComponentTypeId type_id) const {
    return const_cast<Archetype*>(this)->get_component(location, type_id);
}

Chunk& Archetype::get_chunk(std::size_t index) {
    assert(index < chunks_.size() && "Invalid chunk index");
    return *chunks_[index];
}

const Chunk& Archetype::get_chunk(std::size_t index) const {
    assert(index < chunks_.size() && "Invalid chunk index");
    return *chunks_[index];
}

void Archetype::ensure_capacity() {
    if (find_available_chunk() < 0) {
        create_chunk();
    }
}

Chunk& Archetype::create_chunk() {
    // 使用内存分配器创建 Chunk
    auto chunk = std::make_unique<Chunk>(layout_, layout_.entities_per_chunk, allocator_);
    chunks_.push_back(std::move(chunk));
    return *chunks_.back();
}

std::ptrdiff_t Archetype::find_available_chunk() const {
    for (std::size_t i = 0; i < chunks_.size(); ++i) {
        if (!chunks_[i]->is_full()) {
            return static_cast<std::ptrdiff_t>(i);
        }
    }
    return -1;
}

}  // namespace Corona::Kernel::ECS
