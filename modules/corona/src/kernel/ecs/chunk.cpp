#include "corona/kernel/ecs/chunk.h"

#include <cstdlib>
#include <cstring>

#include "corona/kernel/ecs/chunk_allocator.h"
#include "corona/pal/cfw_platform.h"

namespace Corona::Kernel::ECS {

namespace {

/// 分配对齐内存
[[nodiscard]] void* aligned_alloc_impl(std::size_t size, std::size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

#if defined(CFW_PLATFORM_WINDOWS)
    return _aligned_malloc(size, alignment);
#else
    // 确保 size 是 alignment 的倍数（std::aligned_alloc 要求）
    std::size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, aligned_size);
#endif
}

/// 释放对齐内存
void aligned_free_impl(void* ptr) {
    if (!ptr) {
        return;
    }

#if defined(CFW_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

}  // namespace

Chunk::Chunk(const ArchetypeLayout& layout, std::size_t capacity)
    : count_(0), capacity_(capacity), layout_(&layout), allocator_(nullptr), owns_memory_(true) {
    if (capacity_ > 0 && layout_->chunk_data_size > 0) {
        // 分配对齐内存（使用 64 字节对齐以优化缓存）
        constexpr std::size_t kChunkAlignment = 64;
        data_ = static_cast<std::byte*>(aligned_alloc_impl(layout_->chunk_data_size, kChunkAlignment));
        init_memory();
    }
}

Chunk::Chunk(const ArchetypeLayout& layout, std::size_t capacity, ChunkAllocator* allocator)
    : count_(0), capacity_(capacity), layout_(&layout), allocator_(allocator), owns_memory_(false) {
    if (capacity_ > 0 && layout_->chunk_data_size > 0 && allocator_) {
        // 从分配器获取内存
        data_ = static_cast<std::byte*>(allocator_->allocate());
        init_memory();
    }
}

void Chunk::init_memory() {
    if (data_) {
        // 零初始化
        std::memset(data_, 0, layout_->chunk_data_size);
    }
}

Chunk::~Chunk() {
    // 析构所有已分配的实体组件
    for (std::size_t i = 0; i < count_; ++i) {
        destruct_components_at(i);
    }

    // 释放内存
    if (data_) {
        if (owns_memory_) {
            aligned_free_impl(data_);
        } else if (allocator_) {
            allocator_->deallocate(data_);
        }
    }
    data_ = nullptr;
}

Chunk::Chunk(Chunk&& other) noexcept
    : data_(other.data_),
      count_(other.count_),
      capacity_(other.capacity_),
      layout_(other.layout_),
      allocator_(other.allocator_),
      owns_memory_(other.owns_memory_) {
    other.data_ = nullptr;
    other.count_ = 0;
    other.capacity_ = 0;
    other.layout_ = nullptr;
    other.allocator_ = nullptr;
    other.owns_memory_ = true;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // 析构当前实体
        for (std::size_t i = 0; i < count_; ++i) {
            destruct_components_at(i);
        }

        // 释放当前内存
        if (data_) {
            if (owns_memory_) {
                aligned_free_impl(data_);
            } else if (allocator_) {
                allocator_->deallocate(data_);
            }
        }

        // 移动数据
        data_ = other.data_;
        count_ = other.count_;
        capacity_ = other.capacity_;
        layout_ = other.layout_;
        allocator_ = other.allocator_;
        owns_memory_ = other.owns_memory_;

        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
        other.layout_ = nullptr;
        other.allocator_ = nullptr;
        other.owns_memory_ = true;
    }
    return *this;
}

void* Chunk::get_component_array(ComponentTypeId type_id) {
    if (!data_ || !layout_) {
        return nullptr;
    }

    const auto* comp_layout = layout_->find_component(type_id);
    if (!comp_layout) {
        return nullptr;
    }

    return data_ + comp_layout->array_offset;
}

const void* Chunk::get_component_array(ComponentTypeId type_id) const {
    return const_cast<Chunk*>(this)->get_component_array(type_id);
}

void* Chunk::get_component_at(ComponentTypeId type_id, std::size_t index) {
    if (index >= count_) {
        return nullptr;
    }

    if (!data_ || !layout_) {
        return nullptr;
    }

    const auto* comp_layout = layout_->find_component(type_id);
    if (!comp_layout) {
        return nullptr;
    }

    // SoA 布局：组件数组起始位置 + 索引 * 组件大小
    return data_ + comp_layout->array_offset + index * comp_layout->size;
}

const void* Chunk::get_component_at(ComponentTypeId type_id, std::size_t index) const {
    return const_cast<Chunk*>(this)->get_component_at(type_id, index);
}

std::size_t Chunk::allocate() {
    assert(!is_full() && "Chunk is full, cannot allocate");
    assert(layout_ != nullptr && "Layout is null");

    std::size_t index = count_;
    ++count_;

    // 构造所有组件
    construct_components_at(index);

    return index;
}

std::optional<std::size_t> Chunk::deallocate(std::size_t index) {
    if (index >= count_ || layout_ == nullptr) {
        return std::nullopt;  // 无效的索引或布局
    }

    std::optional<std::size_t> moved_from;

    if (index < count_ - 1) {
        // 不是最后一个元素，执行 swap-and-pop
        // 1. 先析构要删除位置的组件
        destruct_components_at(index);

        // 2. 将最后一个元素移动构造到被删除的位置
        //    注意：目标位置已被析构，是未初始化内存，必须用 move_construct 而非 move_assign
        move_construct_components(index, count_ - 1);

        // 3. 析构源位置（移动后的残留对象）
        destruct_components_at(count_ - 1);

        moved_from = count_ - 1;
    } else {
        // 是最后一个元素，直接析构
        destruct_components_at(index);
    }

    --count_;
    return moved_from;
}

void Chunk::construct_components_at(std::size_t index) {
    if (!layout_) {
        return;
    }

    for (const auto& comp : layout_->components) {
        if (comp.type_info && comp.type_info->construct) {
            void* ptr = data_ + comp.array_offset + index * comp.size;
            comp.type_info->construct(ptr);
        }
    }
}

void Chunk::destruct_components_at(std::size_t index) {
    if (!layout_) {
        return;
    }

    for (const auto& comp : layout_->components) {
        if (comp.type_info && comp.type_info->destruct && !comp.type_info->is_trivially_destructible) {
            void* ptr = data_ + comp.array_offset + index * comp.size;
            comp.type_info->destruct(ptr);
        }
    }
}

void Chunk::move_construct_components(std::size_t dst, std::size_t src) {
    if (!layout_ || dst == src) {
        return;
    }

    for (const auto& comp : layout_->components) {
        void* dst_ptr = data_ + comp.array_offset + dst * comp.size;
        void* src_ptr = data_ + comp.array_offset + src * comp.size;

        if (comp.type_info && comp.type_info->is_trivially_copyable) {
            // Trivially copyable 类型直接 memcpy
            std::memcpy(dst_ptr, src_ptr, comp.size);
        } else if (comp.type_info && comp.type_info->move_construct) {
            // 非 trivial 类型使用移动构造（dst 是未初始化内存）
            comp.type_info->move_construct(dst_ptr, src_ptr);
        }
    }
}

void Chunk::move_assign_components(std::size_t dst, std::size_t src) {
    if (!layout_ || dst == src) {
        return;
    }

    for (const auto& comp : layout_->components) {
        void* dst_ptr = data_ + comp.array_offset + dst * comp.size;
        void* src_ptr = data_ + comp.array_offset + src * comp.size;

        if (comp.type_info && comp.type_info->is_trivially_copyable) {
            // Trivially copyable 类型直接 memcpy
            std::memcpy(dst_ptr, src_ptr, comp.size);
        } else if (comp.type_info && comp.type_info->move_assign) {
            // 非 trivial 类型使用移动赋值（dst 是已初始化对象）
            comp.type_info->move_assign(dst_ptr, src_ptr);
        }
    }
}

}  // namespace Corona::Kernel::ECS
