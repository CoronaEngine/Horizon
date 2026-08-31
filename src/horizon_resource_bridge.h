#pragma once

#include "horizon.h"

namespace Corona::Horizon
{

    struct ResourceBridge
    {
        static void set(ResourceHandle& owner, std::shared_ptr<IResourceRef> resource) noexcept
        {
            owner.resource_ = std::move(resource);
        }

        [[nodiscard]] static std::shared_ptr<IResourceRef> token(const ResourceHandle& owner) noexcept
        {
            return owner.resource_;
        }

        [[nodiscard]] static std::shared_ptr<const IResourceRef> keep_alive(const ResourceHandle& owner) noexcept
        {
            return owner.resource_;
        }
    };

}
