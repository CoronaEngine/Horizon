#include "frame_hash.h"

#include <volk.h>

#include <cstdio>
#include <cstdlib>

namespace Corona::Horizon::FrameHash
{
    namespace
    {
        struct State
        {
            bool initialized = false;
            bool active = false;
            uint64_t target = 0;
            uint64_t frame = 0;
            VkDevice device = VK_NULL_HANDLE;
            VkPhysicalDevice physical_device = VK_NULL_HANDLE;
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            bool pending = false;
            uint64_t captured_frame = 0;
            uint32_t captures_done = 0;
            uint32_t captures_wanted = 4;

            struct Result
            {
                uint64_t frame = 0;
                uint64_t hash = 0;
                uint64_t nonzero = 0;
                uint64_t bytes = 0;
                uint32_t first_words[4] = { 0, 0, 0, 0 };
            };
            Result results[8] {};
        };

        State& state()
        {
            static State s;
            return s;
        }
        void ensure_config()
        {
            State& s = state();
            if (s.initialized)
            {
                return;
            }
            s.initialized = true;
            const char* value = std::getenv("HORIZON_FRAME_HASH");
            if (value == nullptr || value[0] == '\0' || value[0] == '0')
            {
                return;
            }
            s.active = true;
            const long long parsed = std::atoll(value);
            s.target = parsed > 0 ? static_cast<uint64_t>(parsed) : 1;
        }

        uint32_t bytes_per_pixel(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R8_UNORM:
                case VK_FORMAT_R8_SNORM:
                case VK_FORMAT_R8_UINT:
                case VK_FORMAT_R8_SINT:
                    return 1;
                case VK_FORMAT_R8G8_UNORM:
                case VK_FORMAT_R16_SFLOAT:
                case VK_FORMAT_R16_UNORM:
                case VK_FORMAT_D16_UNORM:
                    return 2;
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_SRGB:
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_B8G8R8A8_SRGB:
                case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                case VK_FORMAT_R16G16_SFLOAT:
                case VK_FORMAT_R32_SFLOAT:
                case VK_FORMAT_D32_SFLOAT:
                case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
                    return 4;
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                case VK_FORMAT_R16G16B16A16_UNORM:
                case VK_FORMAT_R32G32_SFLOAT:
                    return 8;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                    return 16;
                default:
                    return 4;
            }
        }

        uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_bits, VkMemoryPropertyFlags wanted)
        {
            VkPhysicalDeviceMemoryProperties properties {};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
            for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            {
                const bool type_ok = (type_bits & (1u << index)) != 0;
                const bool flags_ok = (properties.memoryTypes[index].propertyFlags & wanted) == wanted;
                if (type_ok && flags_ok)
                {
                    return index;
                }
            }
            return ~0u;
        }

        void destroy_staging()
        {
            State& s = state();
            if (s.buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(s.device, s.buffer, nullptr);
                s.buffer = VK_NULL_HANDLE;
            }
            if (s.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(s.device, s.memory, nullptr);
                s.memory = VK_NULL_HANDLE;
            }
            s.size = 0;
        }
    }
    bool enabled() noexcept
    {
        ensure_config();
        return state().active;
    }

    void set_device(VkDevice device, VkPhysicalDevice physical_device)
    {
        ensure_config();
        State& s = state();
        if (!s.active)
        {
            return;
        }
        if (s.device != VK_NULL_HANDLE && s.device != device)
        {
            destroy_staging();
        }
        s.device = device;
        s.physical_device = physical_device;
    }

    bool wants_capture() noexcept
    {
        ensure_config();
        State& s = state();
        return s.active && s.captures_done < s.captures_wanted && s.device != VK_NULL_HANDLE &&
               s.physical_device != VK_NULL_HANDLE && s.frame + 1 >= s.target;
    }

    void capture(VkCommandBuffer command_buffer,
                 VkImage image,
                 uint32_t width,
                 uint32_t height,
                 VkFormat format)
    {
        if (!wants_capture() || width == 0 || height == 0)
        {
            return;
        }

        State& s = state();
        VkDevice device = s.device;
        VkPhysicalDevice physical_device = s.physical_device;
        const VkDeviceSize needed =
            static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * bytes_per_pixel(format);

        if (s.buffer == VK_NULL_HANDLE || s.size < needed)
        {
            destroy_staging();

            VkBufferCreateInfo buffer_info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buffer_info.size = needed;
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &buffer_info, nullptr, &s.buffer) != VK_SUCCESS)
            {
                s.buffer = VK_NULL_HANDLE;
                return;
            }

            VkMemoryRequirements requirements {};
            vkGetBufferMemoryRequirements(device, s.buffer, &requirements);
            const uint32_t memory_type =
                find_memory_type(physical_device,
                                 requirements.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memory_type == ~0u)
            {
                destroy_staging();
                return;
            }

            VkMemoryAllocateInfo allocate_info { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocate_info.allocationSize = requirements.size;
            allocate_info.memoryTypeIndex = memory_type;
            if (vkAllocateMemory(device, &allocate_info, nullptr, &s.memory) != VK_SUCCESS)
            {
                s.memory = VK_NULL_HANDLE;
                destroy_staging();
                return;
            }
            if (vkBindBufferMemory(device, s.buffer, s.memory, 0) != VK_SUCCESS)
            {
                destroy_staging();
                return;
            }
            s.size = needed;
        }

        VkBufferImageCopy region {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };

        vkCmdCopyImageToBuffer(
            command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.buffer, 1, &region);

        VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);

        s.width = width;
        s.height = height;
        s.pending = true;
        s.captured_frame = s.frame + 1;
    }
    void end_frame()
    {
        ensure_config();
        State& s = state();
        if (!s.active)
        {
            return;
        }

        ++s.frame;

        if (!s.pending)
        {
            return;
        }
        s.pending = false;

        if (s.device == VK_NULL_HANDLE || s.memory == VK_NULL_HANDLE)
        {
            return;
        }

        vkDeviceWaitIdle(s.device);

        void* mapped = nullptr;
        if (vkMapMemory(s.device, s.memory, 0, s.size, 0, &mapped) != VK_SUCCESS || mapped == nullptr)
        {
            return;
        }

        uint64_t hash = 0xcbf29ce484222325ull;
        uint64_t nonzero = 0;
        const unsigned char* bytes = static_cast<const unsigned char*>(mapped);
        for (VkDeviceSize index = 0; index < s.size; ++index)
        {
            hash ^= static_cast<uint64_t>(bytes[index]);
            hash *= 0x100000001b3ull;
            nonzero += bytes[index] != 0 ? 1u : 0u;
        }

        if (s.captures_done < 8)
        {
            State::Result& r = s.results[s.captures_done];
            r.frame = s.captured_frame;
            r.hash = hash;
            r.nonzero = nonzero;
            r.bytes = static_cast<uint64_t>(s.size);
            const uint32_t* words = static_cast<const uint32_t*>(mapped);
            const uint64_t word_count = static_cast<uint64_t>(s.size) / 4u;
            for (uint32_t i = 0; i < 4u; ++i)
            {
                r.first_words[i] = i < word_count ? words[i] : 0u;
            }
        }
        ++s.captures_done;

        if (const char* path = std::getenv("HORIZON_FRAME_HASH_DUMP"))
        {
            if (path[0] != '\0')
            {
                if (std::FILE* file = std::fopen(path, "wb"))
                {
                    std::fwrite(mapped, 1, static_cast<size_t>(s.size), file);
                    std::fclose(file);
                }
            }
        }

        vkUnmapMemory(s.device, s.memory);
    }

    void report()
    {
        ensure_config();
        State& s = state();
        if (!s.active)
        {
            return;
        }
        if (s.captures_done == 0)
        {
            std::printf("[framehash] no capture (target frame=%llu, reached=%llu)\n",
                        static_cast<unsigned long long>(s.target),
                        static_cast<unsigned long long>(s.frame));
            std::fflush(stdout);
            return;
        }
        const uint32_t count = s.captures_done < 8 ? s.captures_done : 8u;
        for (uint32_t i = 0; i < count; ++i)
        {
            const State::Result& r = s.results[i];
            std::printf("[framehash] frame=%llu %ux%u hash=%016llx nonzero=%llu/%llu w=%08x,%08x,%08x,%08x\n",
                        static_cast<unsigned long long>(r.frame),
                        s.width,
                        s.height,
                        static_cast<unsigned long long>(r.hash),
                        static_cast<unsigned long long>(r.nonzero),
                        static_cast<unsigned long long>(r.bytes),
                        r.first_words[0],
                        r.first_words[1],
                        r.first_words[2],
                        r.first_words[3]);
        }
        std::fflush(stdout);
    }
}
