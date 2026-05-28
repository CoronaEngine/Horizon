#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace Corona::Horizon
{
    struct ResourceAccess;

    struct IResourceControlBlock
    {
        virtual ~IResourceControlBlock() = default;

        [[nodiscard]] virtual std::uintptr_t id() const noexcept = 0;
        [[nodiscard]] virtual bool valid() const noexcept = 0;
    };

    class ResourceHandleBase
    {
    public:
        ResourceHandleBase() noexcept = default;

        ResourceHandleBase(const ResourceHandleBase& other) noexcept
        {
            resource_.store(other.resource_.load(std::memory_order_acquire), std::memory_order_release);
        }

        ResourceHandleBase(ResourceHandleBase&& other) noexcept
        {
            resource_.store(other.resource_.exchange({}, std::memory_order_acq_rel), std::memory_order_release);
        }

        ResourceHandleBase& operator=(const ResourceHandleBase& other) noexcept
        {
            if (this != &other)
                resource_.store(other.resource_.load(std::memory_order_acquire), std::memory_order_release);

            return *this;
        }

        ResourceHandleBase& operator=(ResourceHandleBase&& other) noexcept
        {
            if (this != &other)
                resource_.store(other.resource_.exchange({}, std::memory_order_acq_rel), std::memory_order_release);

            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            auto resource = resource_.load(std::memory_order_acquire);
            return resource && resource->valid();
        }

    protected:
        [[nodiscard]] std::uintptr_t resource_id() const noexcept
        {
            auto resource = resource_.load(std::memory_order_acquire);
            return resource ? resource->id() : 0;
        }

    private:
        friend struct ResourceAccess;

        void set_resource(std::shared_ptr<IResourceControlBlock> resource) noexcept
        {
            resource_.store(std::move(resource), std::memory_order_release);
        }

        [[nodiscard]] std::shared_ptr<IResourceControlBlock> resource_token() const noexcept
        {
            return resource_.load(std::memory_order_acquire);
        }

        std::atomic<std::shared_ptr<IResourceControlBlock>> resource_{};
    };

    struct ResourceAccess
    {
        static void set(ResourceHandleBase& owner, std::shared_ptr<IResourceControlBlock> resource) noexcept
        {
            owner.set_resource(std::move(resource));
        }

        [[nodiscard]] static std::shared_ptr<IResourceControlBlock> token(const ResourceHandleBase& owner) noexcept
        {
            return owner.resource_token();
        }

        [[nodiscard]] static std::shared_ptr<const void> keep_alive(const ResourceHandleBase& owner) noexcept
        {
            return std::static_pointer_cast<const void>(owner.resource_token());
        }
    };
}
