#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "corona/kernel/utils/storage.h"
#include "hardware_wrapper_vulkan/resource_handle.h"

namespace Corona::Horizon::Vulkan
{
    enum class ResourceSlotState : std::uint8_t
    {
        Empty,
        Alive,
        Retiring
    };

    template <typename Resource>
    struct GpuResourceSlot
    {
        GpuResourceSlot() = default;
        GpuResourceSlot(const GpuResourceSlot&) = delete;
        GpuResourceSlot& operator=(const GpuResourceSlot&) = delete;
        GpuResourceSlot(GpuResourceSlot&&) = delete;
        GpuResourceSlot& operator=(GpuResourceSlot&&) = delete;

        ResourceId id{};
        std::atomic<std::uint64_t> ref_count{0};
        ResourceSlotState state = ResourceSlotState::Empty;
        std::optional<Resource> resource;

        [[nodiscard]] bool alive(ResourceId expected) const noexcept
        {
            return state == ResourceSlotState::Alive &&
                   id == expected &&
                   resource.has_value() &&
                   ref_count.load(std::memory_order_acquire) > 0;
        }
    };

    template <typename Resource>
    class GpuResourcePool
    {
    public:
        using resource_type = Resource;
        using slot_type = GpuResourceSlot<Resource>;
        using storage_type = Corona::Kernel::Utils::Storage<slot_type>;
        using storage_id_type = typename storage_type::ObjectId;
        using handle_type = ResourceHandle<Resource>;
        using lease_type = ResourceLease<Resource>;

        class ReadAccess
        {
        public:
            ReadAccess() = default;

            explicit ReadAccess(typename storage_type::ReadHandle handle)
                : handle_(std::move(handle))
            {
            }

            ReadAccess(ReadAccess&&) noexcept = default;
            ReadAccess& operator=(ReadAccess&&) noexcept = default;
            ReadAccess(const ReadAccess&) = delete;
            ReadAccess& operator=(const ReadAccess&) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return handle_.valid() && handle_->resource.has_value();
            }

            explicit operator bool() const noexcept
            {
                return valid();
            }

            [[nodiscard]] const Resource* get() const noexcept
            {
                return valid() ? std::addressof(*handle_->resource) : nullptr;
            }

            [[nodiscard]] const Resource* operator->() const noexcept
            {
                return get();
            }

            [[nodiscard]] const Resource& operator*() const
            {
                auto* resource = get();
                if (resource == nullptr)
                    throw std::logic_error("Dereferencing an empty GPU resource read access.");
                return *resource;
            }

            [[nodiscard]] ResourceId id() const noexcept
            {
                return handle_.valid() ? handle_->id : ResourceId{};
            }

            [[nodiscard]] std::uint64_t ref_count() const noexcept
            {
                return handle_.valid() ? handle_->ref_count.load(std::memory_order_acquire) : 0;
            }

        private:
            typename storage_type::ReadHandle handle_;
        };

        class WriteAccess
        {
        public:
            WriteAccess() = default;

            explicit WriteAccess(typename storage_type::WriteHandle handle)
                : handle_(std::move(handle))
            {
            }

            WriteAccess(WriteAccess&&) noexcept = default;
            WriteAccess& operator=(WriteAccess&&) noexcept = default;
            WriteAccess(const WriteAccess&) = delete;
            WriteAccess& operator=(const WriteAccess&) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return handle_.valid() && handle_->resource.has_value();
            }

            explicit operator bool() const noexcept
            {
                return valid();
            }

            [[nodiscard]] Resource* get() const noexcept
            {
                return valid() ? std::addressof(*handle_->resource) : nullptr;
            }

            [[nodiscard]] Resource* operator->() const noexcept
            {
                return get();
            }

            [[nodiscard]] Resource& operator*() const
            {
                auto* resource = get();
                if (resource == nullptr)
                    throw std::logic_error("Dereferencing an empty GPU resource write access.");
                return *resource;
            }

            [[nodiscard]] ResourceId id() const noexcept
            {
                return handle_.valid() ? handle_->id : ResourceId{};
            }

            [[nodiscard]] std::uint64_t ref_count() const noexcept
            {
                return handle_.valid() ? handle_->ref_count.load(std::memory_order_acquire) : 0;
            }

        private:
            typename storage_type::WriteHandle handle_;
        };

        GpuResourcePool() = default;
        GpuResourcePool(const GpuResourcePool&) = delete;
        GpuResourcePool& operator=(const GpuResourcePool&) = delete;
        GpuResourcePool(GpuResourcePool&&) = delete;
        GpuResourcePool& operator=(GpuResourcePool&&) = delete;

        ~GpuResourcePool()
        {
            clear();
        }

        template <typename... Args>
        [[nodiscard]] handle_type create(Args&&... args)
        {
            std::unique_lock table_lock(table_mutex_);

            const bool use_free_index = !free_indices_.empty();
            const std::uint32_t index = use_free_index
                                            ? free_indices_.back()
                                            : allocate_table_entry_locked();

            storage_id_type storage_id = 0;

            try
            {
                storage_id = storage_.allocate();

                const std::uint32_t generation = next_generation(generations_[index]);
                const ResourceId id{index, generation};

                {
                    auto slot = storage_.acquire_write(storage_id);
                    slot->id = id;
                    slot->ref_count.store(1, std::memory_order_release);
                    slot->state = ResourceSlotState::Alive;
                    slot->resource.emplace(std::forward<Args>(args)...);
                }

                object_ids_[index] = storage_id;
                generations_[index] = generation;

                if (use_free_index)
                    free_indices_.pop_back();

                live_count_.fetch_add(1, std::memory_order_relaxed);
                return handle_type::adopt(*this, id);
            }
            catch (...)
            {
                if (storage_id != 0)
                {
                    try
                    {
                        storage_.deallocate(storage_id);
                    }
                    catch (...)
                    {
                    }
                }

                if (!use_free_index && index + 1 == object_ids_.size())
                {
                    object_ids_.pop_back();
                    generations_.pop_back();
                }

                throw;
            }
        }

        [[nodiscard]] bool retain(ResourceId id) noexcept
        {
            try
            {
                const storage_id_type storage_id = storage_id_for(id);
                if (storage_id == 0)
                    return false;

                auto slot = storage_.acquire_write(storage_id);
                if (!slot->alive(id))
                    return false;

                const std::uint64_t previous = slot->ref_count.load(std::memory_order_acquire);
                if (previous == 0 || previous == std::numeric_limits<std::uint64_t>::max())
                    return false;

                slot->ref_count.store(previous + 1, std::memory_order_release);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool release(ResourceId id) noexcept
        {
            try
            {
                storage_id_type storage_id = 0;
                bool destroy_slot = false;

                {
                    std::unique_lock table_lock(table_mutex_);
                    storage_id = storage_id_for_locked(id);
                    if (storage_id == 0)
                        return false;

                    {
                        auto slot = storage_.acquire_write(storage_id);
                        if (!slot->alive(id))
                            return false;

                        const std::uint64_t previous = slot->ref_count.load(std::memory_order_acquire);
                        if (previous == 0)
                            return false;

                        if (previous > 1)
                        {
                            slot->ref_count.store(previous - 1, std::memory_order_release);
                            return false;
                        }

                        slot->state = ResourceSlotState::Retiring;
                        slot->ref_count.store(0, std::memory_order_release);
                        slot->resource.reset();
                        slot->id = ResourceId{};
                        destroy_slot = true;
                    }

                    if (destroy_slot)
                    {
                        object_ids_[id.index] = 0;
                        free_indices_.push_back(id.index);
                        live_count_.fetch_sub(1, std::memory_order_relaxed);
                    }
                }

                if (destroy_slot)
                    storage_.deallocate(storage_id);

                return destroy_slot;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] ReadAccess acquire_read(ResourceId id)
        {
            const storage_id_type storage_id = storage_id_for(id);
            if (storage_id == 0)
                throw std::runtime_error("Invalid GPU resource id during read acquisition.");

            auto slot = storage_.acquire_read(storage_id);
            if (!slot->alive(id))
                throw std::runtime_error("Stale GPU resource id during read acquisition.");

            return ReadAccess(std::move(slot));
        }

        [[nodiscard]] WriteAccess acquire_write(ResourceId id)
        {
            const storage_id_type storage_id = storage_id_for(id);
            if (storage_id == 0)
                throw std::runtime_error("Invalid GPU resource id during write acquisition.");

            auto slot = storage_.acquire_write(storage_id);
            if (!slot->alive(id))
                throw std::runtime_error("Stale GPU resource id during write acquisition.");

            return WriteAccess(std::move(slot));
        }

        [[nodiscard]] ReadAccess try_acquire_read(ResourceId id) noexcept
        {
            try
            {
                const storage_id_type storage_id = storage_id_for(id);
                if (storage_id == 0)
                    return ReadAccess{};

                auto slot = storage_.try_acquire_read(storage_id);
                if (!slot || !slot->alive(id))
                    return ReadAccess{};

                return ReadAccess(std::move(slot));
            }
            catch (...)
            {
                return ReadAccess{};
            }
        }

        [[nodiscard]] WriteAccess try_acquire_write(ResourceId id) noexcept
        {
            try
            {
                const storage_id_type storage_id = storage_id_for(id);
                if (storage_id == 0)
                    return WriteAccess{};

                auto slot = storage_.try_acquire_write(storage_id);
                if (!slot || !slot->alive(id))
                    return WriteAccess{};

                return WriteAccess(std::move(slot));
            }
            catch (...)
            {
                return WriteAccess{};
            }
        }

        [[nodiscard]] bool contains(ResourceId id) const noexcept
        {
            std::lock_guard table_lock(table_mutex_);
            return storage_id_for_locked(id) != 0;
        }

        [[nodiscard]] std::uint64_t ref_count(ResourceId id) noexcept
        {
            try
            {
                const storage_id_type storage_id = storage_id_for(id);
                if (storage_id == 0)
                    return 0;

                auto slot = storage_.acquire_read(storage_id);
                return slot->alive(id) ? slot->ref_count.load(std::memory_order_acquire) : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::size_t live_count() const noexcept
        {
            return live_count_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] std::size_t capacity() const noexcept
        {
            std::lock_guard table_lock(table_mutex_);
            return object_ids_.size();
        }

        void clear() noexcept
        {
            std::vector<storage_id_type> storage_ids;

            {
                std::lock_guard table_lock(table_mutex_);
                storage_ids = object_ids_;
                object_ids_.assign(object_ids_.size(), 0);
                free_indices_.clear();
                free_indices_.reserve(object_ids_.size());

                for (std::uint32_t index = 0; index < object_ids_.size(); ++index)
                {
                    free_indices_.push_back(index);
                    generations_[index] = next_generation(generations_[index]);
                }

                live_count_.store(0, std::memory_order_relaxed);
            }

            for (storage_id_type storage_id : storage_ids)
            {
                if (storage_id == 0)
                    continue;

                try
                {
                    {
                        auto slot = storage_.acquire_write(storage_id);
                        slot->state = ResourceSlotState::Retiring;
                        slot->ref_count.store(0, std::memory_order_release);
                        slot->resource.reset();
                        slot->id = ResourceId{};
                        slot->state = ResourceSlotState::Empty;
                    }

                    storage_.deallocate(storage_id);
                }
                catch (...)
                {
                }
            }
        }

    private:
        [[nodiscard]] static std::uint32_t next_generation(std::uint32_t current) noexcept
        {
            std::uint32_t next = current + 1;
            if (next == 0)
                next = 1;
            return next;
        }

        [[nodiscard]] std::uint32_t allocate_table_entry_locked()
        {
            if (object_ids_.size() >= ResourceId::invalid_index)
                throw std::overflow_error("GPU resource table index overflow.");

            const auto index = static_cast<std::uint32_t>(object_ids_.size());
            object_ids_.push_back(0);
            generations_.push_back(0);
            return index;
        }

        [[nodiscard]] storage_id_type storage_id_for(ResourceId id) const noexcept
        {
            std::lock_guard table_lock(table_mutex_);
            return storage_id_for_locked(id);
        }

        [[nodiscard]] storage_id_type storage_id_for_locked(ResourceId id) const noexcept
        {
            if (!id.valid() || id.index >= object_ids_.size())
                return 0;

            if (generations_[id.index] != id.generation)
                return 0;

            return object_ids_[id.index];
        }

        mutable std::mutex table_mutex_;
        storage_type storage_;
        std::vector<storage_id_type> object_ids_;
        std::vector<std::uint32_t> generations_;
        std::vector<std::uint32_t> free_indices_;
        std::atomic<std::size_t> live_count_{0};
    };
}
