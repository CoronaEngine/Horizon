#pragma once

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include "corona/kernel/utils/storage.h"
#include "horizon.h"

#include <vk_mem_alloc.h>
#include <volk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Corona::Horizon
{
    class DeviceManager;
    class ResourceManager;

    template <typename Resource, typename Releaser>
    class ResourceStore
    {
    public:
        struct Slot
        {
            Resource value {};
            std::uint64_t generation { 0 };
            bool alive { false };
        };

        class Read
        {
        public:
            Read() = default;

            Read(typename Corona::Kernel::Utils::Storage<Slot>::ReadHandle handle, std::uint64_t generation)
                : handle_(std::move(handle)), generation_(generation)
            {
            }

            [[nodiscard]] bool valid() const noexcept
            {
                return handle_ && handle_->alive && handle_->generation == generation_;
            }

            [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

            [[nodiscard]] const Resource* get() const noexcept { return valid() ? &handle_->value : nullptr; }
            [[nodiscard]] const Resource* operator->() const noexcept { return get(); }
            [[nodiscard]] const Resource& operator*() const noexcept { return *get(); }

        private:
            typename Corona::Kernel::Utils::Storage<Slot>::ReadHandle handle_ {};
            std::uint64_t generation_ { 0 };
        };

        class Write
        {
        public:
            Write() = default;

            Write(typename Corona::Kernel::Utils::Storage<Slot>::WriteHandle handle, std::uint64_t generation)
                : handle_(std::move(handle)), generation_(generation)
            {
            }

            [[nodiscard]] bool valid() const noexcept
            {
                return handle_ && handle_->alive && handle_->generation == generation_;
            }

            [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

            [[nodiscard]] Resource* get() const noexcept { return valid() ? &handle_->value : nullptr; }
            [[nodiscard]] Resource* operator->() const noexcept { return get(); }
            [[nodiscard]] Resource& operator*() const noexcept { return *get(); }

        private:
            typename Corona::Kernel::Utils::Storage<Slot>::WriteHandle handle_ {};
            std::uint64_t generation_ { 0 };
        };

        class Handle
        {
        public:
            Handle() = default;
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            Handle(Handle&& other) noexcept
            {
                swap(other);
            }

            Handle& operator=(Handle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    swap(other);
                }
                return *this;
            }

            ~Handle()
            {
                reset();
            }

            [[nodiscard]] bool valid() const noexcept { return store_ && id_ != 0 && generation_ != 0; }
            [[nodiscard]] std::uintptr_t id() const noexcept { return id_; }
            [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
            [[nodiscard]] ResourceStore* store() const noexcept { return store_; }

            void reset() noexcept
            {
                ResourceStore* store = store_;
                const std::uintptr_t id = id_;
                const std::uint64_t generation = generation_;

                store_ = nullptr;
                id_ = 0;
                generation_ = 0;

                if (store && id != 0)
                {
                    store->release(id, generation);
                }
            }

        private:
            friend class ResourceStore;

            Handle(ResourceStore* store, std::uintptr_t id, std::uint64_t generation) noexcept
                : store_(store), id_(id), generation_(generation)
            {
            }

            void swap(Handle& other) noexcept
            {
                std::swap(store_, other.store_);
                std::swap(id_, other.id_);
                std::swap(generation_, other.generation_);
            }

            ResourceStore* store_ { nullptr };
            std::uintptr_t id_ { 0 };
            std::uint64_t generation_ { 0 };
        };

        template <typename Factory>
        [[nodiscard]] Handle create(Factory&& factory)
        {
            const std::uintptr_t id = storage_.allocate();
            if (id == 0)
            {
                throw std::runtime_error("ResourceStore allocation returned an empty id.");
            }

            try
            {
                auto entry = storage_.acquire_write(id);
                entry->value = std::forward<Factory>(factory)();
                entry->generation = next_generation_.fetch_add(1, std::memory_order_relaxed);
                if (entry->generation == 0)
                {
                    entry->generation = next_generation_.fetch_add(1, std::memory_order_relaxed);
                }
                entry->alive = true;
                return Handle(this, id, entry->generation);
            }
            catch (...)
            {
                storage_.deallocate(id);
                throw;
            }
        }

        [[nodiscard]] Read read(const Handle& handle) const
        {
            if (!handle.valid() || handle.store() != this)
            {
                return {};
            }
            return Read(storage_.try_acquire_read(handle.id()), handle.generation());
        }

        [[nodiscard]] Write write(const Handle& handle)
        {
            if (!handle.valid() || handle.store() != this)
            {
                return {};
            }
            return Write(storage_.try_acquire_write(handle.id()), handle.generation());
        }

    private:
        void release(std::uintptr_t id, std::uint64_t generation) noexcept
        {
            std::optional<Resource> dead;

            try
            {
                {
                    auto entry = storage_.try_acquire_write(id);
                    if (!entry || !entry->alive || entry->generation != generation)
                    {
                        return;
                    }

                    dead.emplace(std::move(entry->value));
                    entry->value = Resource {};
                    entry->alive = false;
                    entry->generation = 0;
                }

                if (dead)
                {
                    Releaser {}(*dead);
                }

                storage_.deallocate(id);
            }
            catch (...)
            {
            }
        }

        mutable Corona::Kernel::Utils::Storage<Slot> storage_ {};
        std::atomic<std::uint64_t> next_generation_ { 1 };
    };

    template <typename Store>
    class Token final : public IResourceRef
    {
    public:
        explicit Token(typename Store::Handle handle)
            : handle_(std::move(handle))
        {
        }

        [[nodiscard]] std::uintptr_t id() const noexcept override { return handle_.id(); }
        [[nodiscard]] bool valid() const noexcept override { return handle_.valid(); }

        [[nodiscard]] const typename Store::Handle& handle() const noexcept { return handle_; }

    private:
        typename Store::Handle handle_ {};
    };

    template <typename Store>
    [[nodiscard]] std::shared_ptr<IResourceRef> make_token(typename Store::Handle handle)
    {
        return std::make_shared<Token<Store>>(std::move(handle));
    }

    template <typename Store>
    [[nodiscard]] const typename Store::Handle* typed_handle(const std::shared_ptr<IResourceRef>& token)
    {
        Token<Store>* typed = dynamic_cast<Token<Store>*>(token.get());
        return typed ? &typed->handle() : nullptr;
    }

    template <typename Store>
    [[nodiscard]] typename Store::Read read(const std::shared_ptr<IResourceRef>& token)
    {
        const typename Store::Handle* handle = typed_handle<Store>(token);
        return handle ? handle->store()->read(*handle) : typename Store::Read {};
    }

    template <typename Store>
    [[nodiscard]] typename Store::Write write(const std::shared_ptr<IResourceRef>& token)
    {
        const typename Store::Handle* handle = typed_handle<Store>(token);
        return handle ? handle->store()->write(*handle) : typename Store::Write {};
    }

    struct BufferWrap
    {
        HardwareBufferDesc desc {};

        std::uint64_t allocation_size { 0 };
        std::uint64_t ref_count { 1 };

        VkBuffer buffer_handle { VK_NULL_HANDLE };
        VkBufferUsageFlags buffer_usage { 0 };
        VmaAllocation buffer_alloc { VK_NULL_HANDLE };
        VmaAllocationInfo buffer_alloc_info {};

        bool host_imported_manual_bind { false };
        bool imported { false };

        std::int32_t bindless_index { -1 };

        DeviceManager* device_manager { nullptr };
        ResourceManager* resource_manager { nullptr };
#if defined(_WIN32) || defined(_WIN64)
        void* exported_win32_handle { nullptr };
#endif

        [[nodiscard]] bool valid() const noexcept
        {
            return buffer_handle != VK_NULL_HANDLE;
        }

        [[nodiscard]] std::uint64_t logical_size() const
        {
            return desc.byte_size();
        }

        [[nodiscard]] std::uint64_t mapped_size() const noexcept
        {
            if (allocation_size != 0)
            {
                return allocation_size;
            }
            return static_cast<std::uint64_t>(buffer_alloc_info.size);
        }

        [[nodiscard]] void* mapped_data() const noexcept
        {
            return buffer_alloc_info.pMappedData;
        }

        void clear_handles() noexcept
        {
            buffer_handle = VK_NULL_HANDLE;
            buffer_alloc = VK_NULL_HANDLE;
            buffer_alloc_info = {};
            allocation_size = 0;
            buffer_usage = 0;
            host_imported_manual_bind = false;
            imported = false;
            bindless_index = -1;
            device_manager = nullptr;
            resource_manager = nullptr;
#if defined(_WIN32) || defined(_WIN64)
            exported_win32_handle = nullptr;
#endif
        }
    };

    void destroy_buffer(BufferWrap& buffer) noexcept;

    struct ImageWrap
    {
        HardwareImageDesc desc {};
        ImageSubresourceRange range { ImageSubresourceRange::whole() };

        std::uint64_t allocation_size { 0 };

        VkImage image_handle { VK_NULL_HANDLE };
        VkImageView image_view { VK_NULL_HANDLE };
        VkImageUsageFlags image_usage { 0 };
        VkImageAspectFlags aspect_mask { VK_IMAGE_ASPECT_COLOR_BIT };
        VkFormat image_format { VK_FORMAT_UNDEFINED };
        VkImageLayout image_layout { VK_IMAGE_LAYOUT_UNDEFINED };
        VmaAllocation image_alloc { VK_NULL_HANDLE };
        VmaAllocationInfo image_alloc_info {};

        bool imported { false };
        bool owns_native_image { true };

        std::int32_t bindless_index { -1 };
        VkClearValue clear_value {};

        DeviceManager* device_manager { nullptr };
        ResourceManager* resource_manager { nullptr };
#if defined(_WIN32) || defined(_WIN64)
        void* exported_win32_handle { nullptr };
#endif

        [[nodiscard]] bool valid() const noexcept
        {
            return image_handle != VK_NULL_HANDLE;
        }

        [[nodiscard]] void* mapped_data() const noexcept
        {
            return image_alloc_info.pMappedData;
        }

        [[nodiscard]] std::uint64_t mapped_size() const noexcept
        {
            if (allocation_size != 0)
            {
                return allocation_size;
            }
            return static_cast<std::uint64_t>(image_alloc_info.size);
        }

        void clear_handles() noexcept
        {
            image_handle = VK_NULL_HANDLE;
            image_view = VK_NULL_HANDLE;
            image_usage = 0;
            aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_format = VK_FORMAT_UNDEFINED;
            image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_alloc = VK_NULL_HANDLE;
            image_alloc_info = {};
            allocation_size = 0;
            imported = false;
            owns_native_image = true;
            bindless_index = -1;
            clear_value = {};
            device_manager = nullptr;
            resource_manager = nullptr;
#if defined(_WIN32) || defined(_WIN64)
            exported_win32_handle = nullptr;
#endif
        }
    };

    void destroy_image(ImageWrap& image) noexcept;

    struct ComputePipelineWrap
    {
        std::shared_ptr<void> impl {};
    };

    struct RasterizerPipelineWrap
    {
        std::shared_ptr<void> impl {};
    };

    struct DisplayerWrap
    {
        void* display_surface { nullptr };
        std::shared_ptr<void> display_manager {};
    };

    struct ExecutorWrap
    {
        void* impl { nullptr };
    };

    struct PushConstantWrap
    {
        std::shared_ptr<std::uint8_t[]> owner {};
        std::uint8_t* data { nullptr };
        std::uint64_t size { 0 };
    };

    struct BufferReleaser
    {
        void operator()(BufferWrap& buffer) const noexcept
        {
            destroy_buffer(buffer);
        }
    };

    struct ImageReleaser
    {
        void operator()(ImageWrap& image) const noexcept
        {
            destroy_image(image);
        }
    };

    struct NoopReleaser
    {
        template <typename T>
        void operator()(T&) const noexcept
        {
        }
    };

    struct ResourcePool
    {
        ResourceStore<BufferWrap, BufferReleaser> buffers;
        ResourceStore<ImageWrap, ImageReleaser> images;
        ResourceStore<ComputePipelineWrap, NoopReleaser> compute_pipelines;
        ResourceStore<RasterizerPipelineWrap, NoopReleaser> rasterizer_pipelines;
        ResourceStore<DisplayerWrap, NoopReleaser> displayers;
        ResourceStore<ExecutorWrap, NoopReleaser> executors;
        ResourceStore<PushConstantWrap, NoopReleaser> push_constants;
    };

    ResourcePool& resource_pool();
}
