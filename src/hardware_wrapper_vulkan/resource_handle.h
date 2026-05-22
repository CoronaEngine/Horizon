#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Corona::Horizon::Vulkan
{
    template <typename Resource>
    class GpuResourcePool;

    template <typename Resource>
    class ResourceHandle;

    template <typename Resource>
    class ResourceLease;

    struct ResourceId
    {
        static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t index = invalid_index;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return index != invalid_index && generation != 0;
        }

        [[nodiscard]] constexpr std::uint64_t packed() const noexcept
        {
            return (std::uint64_t{generation} << 32u) | std::uint64_t{index};
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        friend constexpr bool operator==(ResourceId lhs, ResourceId rhs) noexcept
        {
            return lhs.index == rhs.index && lhs.generation == rhs.generation;
        }

        friend constexpr bool operator!=(ResourceId lhs, ResourceId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    struct ResourceIdHash
    {
        [[nodiscard]] std::size_t operator()(ResourceId id) const noexcept
        {
            return static_cast<std::size_t>(id.packed());
        }
    };

    template <typename Resource>
    class ResourceHandle
    {
    public:
        using resource_type = Resource;
        using pool_type = GpuResourcePool<Resource>;

        constexpr ResourceHandle() noexcept = default;
        constexpr ResourceHandle(std::nullptr_t) noexcept {}

        ResourceHandle(const ResourceHandle& other) noexcept
            : pool_(other.pool_), id_(other.id_)
        {
            if (!add_ref())
            {
                pool_ = nullptr;
                id_ = ResourceId{};
            }
        }

        ResourceHandle(ResourceHandle&& other) noexcept
            : pool_(std::exchange(other.pool_, nullptr)),
              id_(std::exchange(other.id_, ResourceId{}))
        {
        }

        ~ResourceHandle()
        {
            reset();
        }

        ResourceHandle& operator=(const ResourceHandle& other) noexcept
        {
            if (this == &other)
                return *this;

            ResourceHandle copy(other);
            swap(copy);
            return *this;
        }

        ResourceHandle& operator=(ResourceHandle&& other) noexcept
        {
            if (this == &other)
                return *this;

            reset();
            pool_ = std::exchange(other.pool_, nullptr);
            id_ = std::exchange(other.id_, ResourceId{});
            return *this;
        }

        void reset() noexcept
        {
            auto* old_pool = std::exchange(pool_, nullptr);
            const ResourceId old_id = std::exchange(id_, ResourceId{});

            if (old_pool != nullptr && old_id.valid())
                old_pool->release(old_id);
        }

        void swap(ResourceHandle& other) noexcept
        {
            using std::swap;
            swap(pool_, other.pool_);
            swap(id_, other.id_);
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return pool_ != nullptr && id_.valid();
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        [[nodiscard]] ResourceId id() const noexcept
        {
            return id_;
        }

        [[nodiscard]] pool_type* pool() const noexcept
        {
            return pool_;
        }

        [[nodiscard]] ResourceLease<Resource> lease() const noexcept;

    private:
        struct adopt_ref_t
        {
        };

        struct retain_ref_t
        {
        };

        friend class GpuResourcePool<Resource>;
        friend class ResourceLease<Resource>;

        ResourceHandle(pool_type* pool, ResourceId id, adopt_ref_t) noexcept
            : pool_(pool), id_(id)
        {
        }

        ResourceHandle(pool_type* pool, ResourceId id, retain_ref_t) noexcept
            : pool_(pool), id_(id)
        {
            if (!add_ref())
            {
                pool_ = nullptr;
                id_ = ResourceId{};
            }
        }

        [[nodiscard]] static ResourceHandle adopt(pool_type& pool, ResourceId id) noexcept
        {
            return ResourceHandle(&pool, id, adopt_ref_t{});
        }

        [[nodiscard]] static ResourceHandle retained(pool_type& pool, ResourceId id) noexcept
        {
            return ResourceHandle(&pool, id, retain_ref_t{});
        }

        [[nodiscard]] bool add_ref() noexcept
        {
            if (pool_ != nullptr && id_.valid())
                return pool_->retain(id_);

            return false;
        }

        pool_type* pool_ = nullptr;
        ResourceId id_{};
    };

    template <typename Resource>
    void swap(ResourceHandle<Resource>& lhs, ResourceHandle<Resource>& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    template <typename Resource>
    class ResourceLease
    {
    public:
        using resource_type = Resource;
        using handle_type = ResourceHandle<Resource>;
        using pool_type = typename handle_type::pool_type;

        constexpr ResourceLease() noexcept = default;
        constexpr ResourceLease(std::nullptr_t) noexcept {}

        explicit ResourceLease(const handle_type& handle) noexcept
            : handle_(handle)
        {
        }

        explicit ResourceLease(handle_type&& handle) noexcept
            : handle_(std::move(handle))
        {
        }

        ResourceLease(const ResourceLease&) noexcept = default;
        ResourceLease(ResourceLease&&) noexcept = default;
        ResourceLease& operator=(const ResourceLease&) noexcept = default;
        ResourceLease& operator=(ResourceLease&&) noexcept = default;
        ~ResourceLease() = default;

        void reset() noexcept
        {
            handle_.reset();
        }

        void swap(ResourceLease& other) noexcept
        {
            handle_.swap(other.handle_);
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return handle_.valid();
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        [[nodiscard]] ResourceId id() const noexcept
        {
            return handle_.id();
        }

        [[nodiscard]] const handle_type& handle() const noexcept
        {
            return handle_;
        }

        [[nodiscard]] pool_type* pool() const noexcept
        {
            return handle_.pool();
        }

        [[nodiscard]] decltype(auto) read() const
        {
            auto* owner = pool();
            if (owner == nullptr || !id().valid())
                throw std::logic_error("Cannot read an empty GPU resource lease.");

            return owner->acquire_read(id());
        }

        [[nodiscard]] decltype(auto) write() const
        {
            auto* owner = pool();
            if (owner == nullptr || !id().valid())
                throw std::logic_error("Cannot write an empty GPU resource lease.");

            return owner->acquire_write(id());
        }

    private:
        handle_type handle_;
    };

    template <typename Resource>
    void swap(ResourceLease<Resource>& lhs, ResourceLease<Resource>& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    template <typename Resource>
    ResourceLease<Resource> ResourceHandle<Resource>::lease() const noexcept
    {
        return ResourceLease<Resource>(*this);
    }

    template <typename Resource>
    struct ResourceTypeKey
    {
        [[nodiscard]] static const void* value() noexcept
        {
            static const int key = 0;
            return &key;
        }
    };

    class AnyResourceLease
    {
    public:
        AnyResourceLease() = default;
        AnyResourceLease(std::nullptr_t) noexcept {}

        template <typename Resource>
        AnyResourceLease(ResourceLease<Resource> lease)
            : storage_(std::make_unique<Model<Resource>>(std::move(lease)))
        {
        }

        AnyResourceLease(const AnyResourceLease& other)
            : storage_(other.storage_ ? other.storage_->clone() : nullptr)
        {
        }

        AnyResourceLease(AnyResourceLease&&) noexcept = default;
        AnyResourceLease& operator=(AnyResourceLease&&) noexcept = default;

        AnyResourceLease& operator=(const AnyResourceLease& other)
        {
            if (this == &other)
                return *this;

            storage_ = other.storage_ ? other.storage_->clone() : nullptr;
            return *this;
        }

        void reset() noexcept
        {
            storage_.reset();
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return storage_ != nullptr && storage_->valid();
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        [[nodiscard]] ResourceId id() const noexcept
        {
            return storage_ ? storage_->id() : ResourceId{};
        }

        [[nodiscard]] const void* type_key() const noexcept
        {
            return storage_ ? storage_->type_key() : nullptr;
        }

        template <typename Resource>
        [[nodiscard]] bool is() const noexcept
        {
            return type_key() == ResourceTypeKey<Resource>::value();
        }

        template <typename Resource>
        [[nodiscard]] ResourceLease<Resource>* as() noexcept
        {
            if (!is<Resource>())
                return nullptr;

            return &static_cast<Model<Resource>*>(storage_.get())->lease;
        }

        template <typename Resource>
        [[nodiscard]] const ResourceLease<Resource>* as() const noexcept
        {
            if (!is<Resource>())
                return nullptr;

            return &static_cast<const Model<Resource>*>(storage_.get())->lease;
        }

    private:
        struct Concept
        {
            virtual ~Concept() = default;
            [[nodiscard]] virtual std::unique_ptr<Concept> clone() const = 0;
            [[nodiscard]] virtual bool valid() const noexcept = 0;
            [[nodiscard]] virtual ResourceId id() const noexcept = 0;
            [[nodiscard]] virtual const void* type_key() const noexcept = 0;
        };

        template <typename Resource>
        struct Model final : Concept
        {
            explicit Model(ResourceLease<Resource> value)
                : lease(std::move(value))
            {
            }

            [[nodiscard]] std::unique_ptr<Concept> clone() const override
            {
                return std::make_unique<Model<Resource>>(lease);
            }

            [[nodiscard]] bool valid() const noexcept override
            {
                return lease.valid();
            }

            [[nodiscard]] ResourceId id() const noexcept override
            {
                return lease.id();
            }

            [[nodiscard]] const void* type_key() const noexcept override
            {
                return ResourceTypeKey<Resource>::value();
            }

            ResourceLease<Resource> lease;
        };

        std::unique_ptr<Concept> storage_;
    };
}
