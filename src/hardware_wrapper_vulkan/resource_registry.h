#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Corona::Horizon::Vulkan
{
    struct ResourceId
    {
        static constexpr uint32_t invalid_index = std::numeric_limits<uint32_t>::max();

        uint32_t index = invalid_index;
        uint32_t generation = 0;

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return index != invalid_index && generation != 0;
        }

        [[nodiscard]] constexpr std::uintptr_t encode() const noexcept
        {
            if (!valid()) return 0;
            const uint64_t encoded = (uint64_t{generation} << 32u) | uint64_t{index};
            return static_cast<std::uintptr_t>(encoded);
        }

        [[nodiscard]] static constexpr ResourceId decode(std::uintptr_t value) noexcept
        {
            if (value == 0) return {};
            const uint64_t encoded = static_cast<uint64_t>(value);
            return ResourceId{
                .index = static_cast<uint32_t>(encoded & 0xffff'ffffull),
                .generation = static_cast<uint32_t>(encoded >> 32u),
            };
        }

        [[nodiscard]] friend constexpr bool operator==(ResourceId lhs, ResourceId rhs) noexcept
        {
            return lhs.index == rhs.index && lhs.generation == rhs.generation;
        }

        [[nodiscard]] friend constexpr bool operator!=(ResourceId lhs, ResourceId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    class BadResourceId final : public std::runtime_error
    {
    public:
        explicit BadResourceId(const char* message) : std::runtime_error(message) {}
    };

    static_assert(sizeof(std::uintptr_t) >= sizeof(uint64_t),
                  "ResourceRegistry encoded ids require a 64-bit uintptr_t.");

    template <typename T>
    class ResourceRegistry
    {
    public:
        using value_type = T;
        using EncodedId = std::uintptr_t;
        using ObjectId = EncodedId;
        using Deleter = std::function<void(T&)>;

        enum class ReleaseResult
        {
            invalid,
            released,
            destroyed,
        };

        class ReadHandle
        {
        public:
            ReadHandle() = default;
            ReadHandle(ReadHandle&&) noexcept = default;
            ReadHandle& operator=(ReadHandle&&) noexcept = default;
            ReadHandle(const ReadHandle&) = delete;
            ReadHandle& operator=(const ReadHandle&) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return ptr_ != nullptr && lock_.owns_lock();
            }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return valid();
            }

            [[nodiscard]] EncodedId id() const noexcept { return id_; }
            [[nodiscard]] uint32_t index() const noexcept { return ResourceId::decode(id_).index; }
            [[nodiscard]] const T* get() const noexcept { return ptr_; }
            [[nodiscard]] const T* operator->() const noexcept { return ptr_; }
            [[nodiscard]] const T& operator*() const noexcept { return *ptr_; }

        private:
            friend class ResourceRegistry;

            ReadHandle(EncodedId id, const T* ptr, std::shared_lock<std::shared_mutex>&& lock) noexcept
                : id_(id), ptr_(ptr), lock_(std::move(lock))
            {
            }

            EncodedId id_ = 0;
            const T* ptr_ = nullptr;
            std::shared_lock<std::shared_mutex> lock_;
        };

        class WriteHandle
        {
        public:
            WriteHandle() = default;
            WriteHandle(WriteHandle&&) noexcept = default;
            WriteHandle& operator=(WriteHandle&&) noexcept = default;
            WriteHandle(const WriteHandle&) = delete;
            WriteHandle& operator=(const WriteHandle&) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return ptr_ != nullptr && lock_.owns_lock();
            }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return valid();
            }

            [[nodiscard]] EncodedId id() const noexcept { return id_; }
            [[nodiscard]] uint32_t index() const noexcept { return ResourceId::decode(id_).index; }
            [[nodiscard]] T* get() const noexcept { return ptr_; }
            [[nodiscard]] T* operator->() const noexcept { return ptr_; }
            [[nodiscard]] T& operator*() const noexcept { return *ptr_; }

        private:
            friend class ResourceRegistry;

            WriteHandle(EncodedId id, T* ptr, std::unique_lock<std::shared_mutex>&& lock) noexcept
                : id_(id), ptr_(ptr), lock_(std::move(lock))
            {
            }

            EncodedId id_ = 0;
            T* ptr_ = nullptr;
            std::unique_lock<std::shared_mutex> lock_;
        };

        class Ref
        {
        public:
            Ref() = default;

            ~Ref()
            {
                reset();
            }

            Ref(const Ref& other) : registry_(other.registry_), id_(other.id_)
            {
                if (registry_ && id_ != 0 && !registry_->retain(id_))
                {
                    registry_ = nullptr;
                    id_ = 0;
                }
            }

            Ref& operator=(const Ref& other)
            {
                if (this == &other) return *this;
                Ref copy(other);
                swap(copy);
                return *this;
            }

            Ref(Ref&& other) noexcept : registry_(other.registry_), id_(other.id_)
            {
                other.registry_ = nullptr;
                other.id_ = 0;
            }

            Ref& operator=(Ref&& other) noexcept
            {
                if (this == &other) return *this;
                reset();
                registry_ = other.registry_;
                id_ = other.id_;
                other.registry_ = nullptr;
                other.id_ = 0;
                return *this;
            }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return id_ != 0 && registry_ != nullptr;
            }

            [[nodiscard]] EncodedId id() const noexcept { return id_; }

            [[nodiscard]] bool alive() const
            {
                return registry_ != nullptr && registry_->contains(id_);
            }

            [[nodiscard]] ReadHandle read() const
            {
                if (!registry_) throw BadResourceId("Attempted to read through an empty resource ref.");
                return registry_->acquire_read(id_);
            }

            [[nodiscard]] WriteHandle write() const
            {
                if (!registry_) throw BadResourceId("Attempted to write through an empty resource ref.");
                return registry_->acquire_write(id_);
            }

            void reset() noexcept
            {
                if (registry_ && id_ != 0)
                {
                    try
                    {
                        registry_->release(id_);
                    }
                    catch (...)
                    {
                    }
                }
                registry_ = nullptr;
                id_ = 0;
            }

            void swap(Ref& other) noexcept
            {
                std::swap(registry_, other.registry_);
                std::swap(id_, other.id_);
            }

        private:
            friend class ResourceRegistry;

            struct AdoptTag
            {
            };

            Ref(ResourceRegistry& registry, EncodedId id, AdoptTag) noexcept : registry_(&registry), id_(id) {}

            ResourceRegistry* registry_ = nullptr;
            EncodedId id_ = 0;
        };

        ResourceRegistry() = default;
        explicit ResourceRegistry(Deleter deleter) : deleter_(std::move(deleter)) {}
        ~ResourceRegistry() = default;

        ResourceRegistry(const ResourceRegistry&) = delete;
        ResourceRegistry& operator=(const ResourceRegistry&) = delete;
        ResourceRegistry(ResourceRegistry&&) = delete;
        ResourceRegistry& operator=(ResourceRegistry&&) = delete;

        void set_deleter(Deleter deleter)
        {
            std::lock_guard lock(deleter_mutex_);
            deleter_ = std::move(deleter);
        }

        [[nodiscard]] EncodedId allocate()
        {
            return create();
        }

        [[nodiscard]] EncodedId create()
        {
            return emplace();
        }

        [[nodiscard]] Ref create_ref()
        {
            return Ref(*this, create(), typename Ref::AdoptTag{});
        }

        [[nodiscard]] EncodedId create(T value)
        {
            return emplace(std::move(value));
        }

        [[nodiscard]] Ref create_ref(T value)
        {
            return Ref(*this, create(std::move(value)), typename Ref::AdoptTag{});
        }

        template <typename... Args>
        [[nodiscard]] EncodedId emplace(Args&&... args)
        {
            std::lock_guard mutation_lock(mutation_mutex_);
            auto [index, slot] = acquire_slot();
            const uint32_t generation = slot->generation;

            std::unique_lock slot_lock(slot->mutex);
            try
            {
                slot->value = std::make_unique<T>(std::forward<Args>(args)...);
                slot->ref_count = 1;
                slot->alive = true;
            }
            catch (...)
            {
                slot_lock.unlock();
                recycle_unused_slot(index, slot);
                throw;
            }

            live_count_.fetch_add(1, std::memory_order_relaxed);
            return ResourceId{index, generation}.encode();
        }

        template <typename... Args>
        [[nodiscard]] Ref emplace_ref(Args&&... args)
        {
            return Ref(*this, emplace(std::forward<Args>(args)...), typename Ref::AdoptTag{});
        }

        [[nodiscard]] bool contains(EncodedId id) const
        {
            const ResourceId decoded = ResourceId::decode(id);
            const Slot* slot = slot_at(decoded.index);
            if (!slot) return false;

            std::shared_lock slot_lock(slot->mutex);
            return slot_is_current_locked(*slot, decoded);
        }

        [[nodiscard]] bool retain(EncodedId id)
        {
            const ResourceId decoded = ResourceId::decode(id);
            Slot* slot = slot_at(decoded.index);
            if (!slot) return false;

            std::unique_lock slot_lock(slot->mutex);
            if (!slot_is_current_locked(*slot, decoded)) return false;
            if (slot->ref_count == std::numeric_limits<uint32_t>::max()) return false;

            ++slot->ref_count;
            return true;
        }

        [[nodiscard]] uint32_t use_count(EncodedId id) const
        {
            const ResourceId decoded = ResourceId::decode(id);
            const Slot* slot = slot_at(decoded.index);
            if (!slot) return 0;

            std::shared_lock slot_lock(slot->mutex);
            if (!slot_is_current_locked(*slot, decoded)) return 0;
            return slot->ref_count;
        }

        template <typename Deleter>
        ReleaseResult release_result(EncodedId id, Deleter&& deleter)
        {
            const ResourceId decoded = ResourceId::decode(id);
            Slot* slot = slot_at(decoded.index);
            if (!slot) return ReleaseResult::invalid;

            std::unique_ptr<T> value_to_destroy;
            {
                std::unique_lock slot_lock(slot->mutex);
                if (!slot_is_current_locked(*slot, decoded)) return ReleaseResult::invalid;
                if (slot->ref_count == 0) return ReleaseResult::invalid;

                --slot->ref_count;
                if (slot->ref_count != 0) return ReleaseResult::released;

                value_to_destroy = std::move(slot->value);
                slot->alive = false;
                slot->generation = next_generation(slot->generation);
            }

            try
            {
                std::invoke(std::forward<Deleter>(deleter), *value_to_destroy);
            }
            catch (...)
            {
                recycle_free_slot(decoded.index);
                live_count_.fetch_sub(1, std::memory_order_relaxed);
                throw;
            }

            recycle_free_slot(decoded.index);
            live_count_.fetch_sub(1, std::memory_order_relaxed);
            return ReleaseResult::destroyed;
        }

        template <typename Deleter>
        bool release(EncodedId id, Deleter&& deleter)
        {
            return release_result(id, std::forward<Deleter>(deleter)) != ReleaseResult::invalid;
        }

        bool release(EncodedId id)
        {
            return release(id, [this](T& value) {
                Deleter deleter;
                {
                    std::lock_guard lock(deleter_mutex_);
                    deleter = deleter_;
                }
                if (deleter)
                {
                    deleter(value);
                }
            });
        }

        void deallocate(EncodedId id)
        {
            if (!release(id))
            {
                throw BadResourceId("Attempted to deallocate an invalid resource id.");
            }
        }

        [[nodiscard]] ReadHandle read(EncodedId id) const
        {
            return acquire_read(id);
        }

        [[nodiscard]] WriteHandle write(EncodedId id)
        {
            return acquire_write(id);
        }

        [[nodiscard]] ReadHandle acquire_read(EncodedId id) const
        {
            const ResourceId decoded = ResourceId::decode(id);
            const Slot* slot = slot_at(decoded.index);
            if (!slot) throw BadResourceId("Attempted to read an invalid resource id.");

            std::shared_lock slot_lock(slot->mutex);
            if (!slot_is_current_locked(*slot, decoded))
            {
                throw BadResourceId("Attempted to read a released resource.");
            }

            return ReadHandle(id, slot->value.get(), std::move(slot_lock));
        }

        [[nodiscard]] WriteHandle acquire_write(EncodedId id)
        {
            const ResourceId decoded = ResourceId::decode(id);
            Slot* slot = slot_at(decoded.index);
            if (!slot) throw BadResourceId("Attempted to write an invalid resource id.");

            std::unique_lock slot_lock(slot->mutex);
            if (!slot_is_current_locked(*slot, decoded))
            {
                throw BadResourceId("Attempted to write a released resource.");
            }

            return WriteHandle(id, slot->value.get(), std::move(slot_lock));
        }

        [[nodiscard]] ReadHandle try_acquire_read(EncodedId id) const
        {
            const ResourceId decoded = ResourceId::decode(id);
            const Slot* slot = slot_at(decoded.index);
            if (!slot) return {};

            std::shared_lock slot_lock(slot->mutex, std::try_to_lock);
            if (!slot_lock.owns_lock() || !slot_is_current_locked(*slot, decoded)) return {};

            return ReadHandle(id, slot->value.get(), std::move(slot_lock));
        }

        [[nodiscard]] WriteHandle try_acquire_write(EncodedId id)
        {
            const ResourceId decoded = ResourceId::decode(id);
            Slot* slot = slot_at(decoded.index);
            if (!slot) return {};

            std::unique_lock slot_lock(slot->mutex, std::try_to_lock);
            if (!slot_lock.owns_lock() || !slot_is_current_locked(*slot, decoded)) return {};

            return WriteHandle(id, slot->value.get(), std::move(slot_lock));
        }

        template <typename Fn>
        decltype(auto) with_read(EncodedId id, Fn&& fn) const
        {
            auto handle = acquire_read(id);
            return std::invoke(std::forward<Fn>(fn), *handle);
        }

        template <typename Fn>
        decltype(auto) with_write(EncodedId id, Fn&& fn)
        {
            auto handle = acquire_write(id);
            return std::invoke(std::forward<Fn>(fn), *handle);
        }

        template <typename Fn>
        decltype(auto) with_two_write(EncodedId first, EncodedId second, Fn&& fn)
        {
            if (first == second)
            {
                auto handle = acquire_write(first);
                return std::invoke(std::forward<Fn>(fn), handle, handle);
            }

            ResourceId first_id = ResourceId::decode(first);
            ResourceId second_id = ResourceId::decode(second);
            if (!first_id.valid() || !second_id.valid())
            {
                throw BadResourceId("Attempted to lock invalid resource ids.");
            }
            if (first_id.index == second_id.index)
            {
                throw BadResourceId("Attempted to lock two generations of the same resource slot.");
            }

            const bool swapped = second_id.index < first_id.index;
            EncodedId lower = swapped ? second : first;
            EncodedId higher = swapped ? first : second;

            auto lower_handle = acquire_write(lower);
            auto higher_handle = acquire_write(higher);

            if (swapped)
            {
                return std::invoke(std::forward<Fn>(fn), higher_handle, lower_handle);
            }
            return std::invoke(std::forward<Fn>(fn), lower_handle, higher_handle);
        }

        [[nodiscard]] std::size_t count() const noexcept
        {
            return live_count_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return count() == 0;
        }

        [[nodiscard]] std::size_t capacity() const noexcept
        {
            std::shared_lock lock(list_mutex_);
            return slots_.size();
        }

        template <typename DestroyFn>
        void clear(DestroyFn&& destroy)
        {
            std::lock_guard mutation_lock(mutation_mutex_);
            std::vector<uint32_t> freed_indices;
            std::vector<std::unique_ptr<T>> values_to_destroy;

            {
                std::unique_lock list_lock(list_mutex_);
                freed_indices.reserve(slots_.size());
                values_to_destroy.reserve(slots_.size());

                for (uint32_t index = 0; index < slots_.size(); ++index)
                {
                    Slot& slot = *slots_[index];
                    std::unique_lock slot_lock(slot.mutex);
                    if (!slot.alive) continue;

                    values_to_destroy.push_back(std::move(slot.value));
                    freed_indices.push_back(index);
                    slot.alive = false;
                    slot.ref_count = 0;
                    slot.generation = next_generation(slot.generation);
                }

                live_count_.store(0, std::memory_order_relaxed);
            }

            try
            {
                for (auto& value : values_to_destroy)
                {
                    std::invoke(std::forward<DestroyFn>(destroy), *value);
                }
            }
            catch (...)
            {
                recycle_free_slots(freed_indices);
                throw;
            }

            recycle_free_slots(freed_indices);
        }

        void clear()
        {
            Deleter deleter;
            {
                std::lock_guard lock(deleter_mutex_);
                deleter = deleter_;
            }

            clear([deleter = std::move(deleter)](T& value) mutable {
                if (deleter)
                {
                    deleter(value);
                }
            });
        }

        [[nodiscard]] uint32_t descriptor_index(EncodedId id) const
        {
            ResourceId decoded = ResourceId::decode(id);
            if (!contains(id))
            {
                throw BadResourceId("Attempted to fetch descriptor index for an invalid resource id.");
            }
            return decoded.index;
        }

        [[nodiscard]] int64_t seq_id(EncodedId id) const
        {
            return static_cast<int64_t>(descriptor_index(id));
        }

        [[nodiscard]] int64_t seq_id(const ReadHandle& handle) const
        {
            return handle ? static_cast<int64_t>(handle.index()) : -1;
        }

        [[nodiscard]] int64_t seq_id(const WriteHandle& handle) const
        {
            return handle ? static_cast<int64_t>(handle.index()) : -1;
        }

    private:
        struct Slot
        {
            mutable std::shared_mutex mutex;
            std::unique_ptr<T> value;
            uint32_t generation = 1;
            uint32_t ref_count = 0;
            bool alive = false;
        };

        [[nodiscard]] static uint32_t next_generation(uint32_t value) noexcept
        {
            ++value;
            return value == 0 ? 1 : value;
        }

        [[nodiscard]] static bool slot_is_current_locked(const Slot& slot, ResourceId id) noexcept
        {
            return id.valid() && slot.alive && slot.generation == id.generation && slot.value != nullptr;
        }

        [[nodiscard]] Slot* slot_at(uint32_t index)
        {
            if (index == ResourceId::invalid_index) return nullptr;

            std::shared_lock lock(list_mutex_);
            if (index >= slots_.size()) return nullptr;
            return slots_[index].get();
        }

        [[nodiscard]] const Slot* slot_at(uint32_t index) const
        {
            if (index == ResourceId::invalid_index) return nullptr;

            std::shared_lock lock(list_mutex_);
            if (index >= slots_.size()) return nullptr;
            return slots_[index].get();
        }

        [[nodiscard]] std::pair<uint32_t, Slot*> acquire_slot()
        {
            std::lock_guard lock(list_mutex_);
            if (!free_indices_.empty())
            {
                uint32_t index = free_indices_.back();
                free_indices_.pop_back();
                return {index, slots_[index].get()};
            }

            if (slots_.size() >= ResourceId::invalid_index)
            {
                throw std::length_error("ResourceRegistry exhausted its 32-bit slot space.");
            }

            slots_.push_back(std::make_unique<Slot>());
            return {static_cast<uint32_t>(slots_.size() - 1u), slots_.back().get()};
        }

        void recycle_unused_slot(uint32_t index, Slot* slot)
        {
            {
                std::unique_lock slot_lock(slot->mutex);
                slot->value.reset();
                slot->alive = false;
                slot->ref_count = 0;
                slot->generation = next_generation(slot->generation);
            }
            recycle_free_slot(index);
        }

        void recycle_free_slot(uint32_t index)
        {
            std::lock_guard lock(list_mutex_);
            free_indices_.push_back(index);
        }

        void recycle_free_slots(const std::vector<uint32_t>& indices)
        {
            std::lock_guard lock(list_mutex_);
            free_indices_.insert(free_indices_.end(), indices.begin(), indices.end());
        }

        mutable std::shared_mutex list_mutex_;
        std::vector<std::unique_ptr<Slot>> slots_;
        std::vector<uint32_t> free_indices_;
        std::atomic<std::size_t> live_count_{0};
        std::mutex mutation_mutex_;
        std::mutex deleter_mutex_;
        Deleter deleter_;
    };

    template <typename T>
    using StorageCompat = ResourceRegistry<T>;
}
