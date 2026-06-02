#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace Corona::Horizon
{
    struct ResourceBridge;

    struct IResourceRef
    {
        virtual ~IResourceRef() = default;

        [[nodiscard]] virtual std::uintptr_t id() const noexcept = 0;
        [[nodiscard]] virtual bool valid() const noexcept = 0;
    };

    class ResourceHandle
    {
    public:
        ResourceHandle() noexcept = default;

        ResourceHandle(const ResourceHandle& other) noexcept
            : resource_(other.resource_)
        {
        }

        ResourceHandle(ResourceHandle&& other) noexcept
            : resource_(std::move(other.resource_))
        {
        }

        ResourceHandle& operator=(const ResourceHandle& other) noexcept
        {
            if (this != &other)
                resource_ = other.resource_;

            return *this;
        }

        ResourceHandle& operator=(ResourceHandle&& other) noexcept
        {
            if (this != &other)
                resource_ = std::move(other.resource_);

            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            auto resource = resource_;
            return resource && resource->valid();
        }

    protected:
        [[nodiscard]] std::uintptr_t resource_id() const noexcept
        {
            auto resource = resource_;
            return resource ? resource->id() : 0;
        }

    private:
        friend struct ResourceBridge;

        void set_resource(std::shared_ptr<IResourceRef> resource) noexcept
        {
            resource_ = std::move(resource);
        }

        [[nodiscard]] std::shared_ptr<IResourceRef> resource_ref() const noexcept
        {
            return resource_;
        }

        std::shared_ptr<IResourceRef> resource_{};
    };

    struct ResourceBridge
    {
        static void set(ResourceHandle& owner, std::shared_ptr<IResourceRef> resource) noexcept
        {
            owner.set_resource(std::move(resource));
        }

        [[nodiscard]] static std::shared_ptr<IResourceRef> token(const ResourceHandle& owner) noexcept
        {
            return owner.resource_ref();
        }

        [[nodiscard]] static std::shared_ptr<const IResourceRef> keep_alive(const ResourceHandle& owner) noexcept
        {
            return std::static_pointer_cast<const IResourceRef>(owner.resource_ref());
        }
    };
}
