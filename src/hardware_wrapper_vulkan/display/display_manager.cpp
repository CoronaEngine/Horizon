#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "display_manager.h"

#include "hardware_wrapper/diagnostics.h"
#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace Corona::Horizon
{
    namespace
    {
        using ImageStore = ResourceStore<ImageWrap, ImageReleaser>;

        std::mutex& display_registry_mutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<std::weak_ptr<DisplayManager>>& display_registry()
        {
            static std::vector<std::weak_ptr<DisplayManager>> registry;
            return registry;
        }

        void throw_if_failed(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   std::string(operation) + " failed. VkResult=" + std::to_string(static_cast<int>(result)));
                throw std::runtime_error(std::string(operation) + " failed. VkResult=" + std::to_string(static_cast<int>(result)));
            }
        }

        void name_vulkan_object(VkDevice device, VkObjectType object_type, uint64_t object_handle, const std::string& name) noexcept
        {
            if (vkSetDebugUtilsObjectNameEXT == nullptr || device == VK_NULL_HANDLE || object_handle == 0 || name.empty())
            {
                return;
            }

            VkDebugUtilsObjectNameInfoEXT name_info {};
            name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.objectType = object_type;
            name_info.objectHandle = object_handle;
            name_info.pObjectName = name.c_str();
            (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }

        [[nodiscard]] VkSurfaceFormatKHR choose_surface_format(std::span<const VkSurfaceFormatKHR> formats)
        {
            if (formats.empty())
            {
                throw std::runtime_error("No Vulkan surface formats are available for the display surface.");
            }

            if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
            {
                return { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
            }

            const VkFormat preferred_formats[] = {
                VK_FORMAT_B8G8R8A8_SRGB,
                VK_FORMAT_R8G8B8A8_SRGB,
                VK_FORMAT_B8G8R8A8_UNORM,
                VK_FORMAT_R8G8B8A8_UNORM,
            };

            for (VkFormat preferred : preferred_formats)
            {
                auto found = std::ranges::find_if(formats, [preferred](const VkSurfaceFormatKHR& format) {
                    return format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
                if (found != formats.end())
                {
                    return *found;
                }
            }

            return formats.front();
        }

        [[nodiscard]] std::string present_mode_name(VkPresentModeKHR mode)
        {
            switch (mode)
            {
            case VK_PRESENT_MODE_IMMEDIATE_KHR:
                return "immediate";
            case VK_PRESENT_MODE_MAILBOX_KHR:
                return "mailbox";
            case VK_PRESENT_MODE_FIFO_KHR:
                return "fifo";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                return "fifo_relaxed";
            default:
                return "unknown(" + std::to_string(static_cast<int>(mode)) + ")";
            }
        }

        [[nodiscard]] bool present_mode_supported(std::span<const VkPresentModeKHR> modes, VkPresentModeKHR mode) noexcept
        {
            return std::ranges::find(modes, mode) != modes.end();
        }

        [[nodiscard]] std::string requested_present_mode()
        {
            const char* raw = std::getenv("HORIZON_VULKAN_PRESENT_MODE");
            if (raw == nullptr)
            {
                return {};
            }

            std::string value(raw);
            std::ranges::transform(value, value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        [[nodiscard]] std::optional<VkPresentModeKHR> parse_present_mode_request(std::string_view value)
        {
            if (value.empty() || value == "default" || value == "fifo")
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }
            if (value == "mailbox")
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
            if (value == "immediate")
            {
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            if (value == "fifo_relaxed" || value == "fifo-relaxed")
            {
                return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            }

            return std::nullopt;
        }

        [[nodiscard]] VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
        {
            uint32_t mode_count = 0;
            throw_if_failed(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr),
                            "vkGetPhysicalDeviceSurfacePresentModesKHR");

            std::vector<VkPresentModeKHR> modes(mode_count);
            if (mode_count != 0)
            {
                throw_if_failed(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, modes.data()),
                                "vkGetPhysicalDeviceSurfacePresentModesKHR");
            }

            const std::string requested = requested_present_mode();
            if (!requested.empty())
            {
                const std::optional<VkPresentModeKHR> parsed = parse_present_mode_request(requested);
                if (parsed && present_mode_supported(modes, *parsed))
                {
                    Diagnostics::write(Diagnostics::Level::Info,
                                       "HORIZON VALIDATION",
                                       "Using requested Vulkan present mode: " + present_mode_name(*parsed) + ".");
                    return *parsed;
                }

                Diagnostics::write(Diagnostics::Level::Warning,
                                   "HORIZON VALIDATION",
                                   "Ignoring unsupported HORIZON_VULKAN_PRESENT_MODE='" + requested + "'; using fifo.");
            }

            Diagnostics::write(Diagnostics::Level::Info,
                               "HORIZON VALIDATION",
                               "Using default Vulkan present mode: fifo.");
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        [[nodiscard]] VkExtent2D native_window_extent(void* native_window)
        {
            VkExtent2D extent { 800, 600 };

#if defined(_WIN32) || defined(_WIN64)
            RECT rect {};
            if (native_window != nullptr && GetClientRect(static_cast<HWND>(native_window), &rect))
            {
                extent.width = static_cast<uint32_t>(std::max<LONG>(1, rect.right - rect.left));
                extent.height = static_cast<uint32_t>(std::max<LONG>(1, rect.bottom - rect.top));
            }
#else
            (void)native_window;
#endif

            return extent;
        }

        [[nodiscard]] bool native_window_drawable(void* native_window) noexcept
        {
#if defined(_WIN32) || defined(_WIN64)
            HWND window = static_cast<HWND>(native_window);
            if (window == nullptr || IsWindow(window) == FALSE || IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE)
            {
                return false;
            }

            RECT rect {};
            if (GetClientRect(window, &rect) == FALSE)
            {
                return false;
            }

            return rect.right > rect.left && rect.bottom > rect.top;
#else
            return native_window != nullptr;
#endif
        }

        [[nodiscard]] VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, void* native_window)
        {
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            {
                return capabilities.currentExtent;
            }

            VkExtent2D desired = native_window_extent(native_window);
            desired.width = std::clamp(desired.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            desired.height = std::clamp(desired.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            return desired;
        }

        [[nodiscard]] VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) noexcept
        {
            const VkCompositeAlphaFlagBitsKHR candidates[] = {
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            };

            for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
            {
                if ((supported & candidate) != 0)
                {
                    return candidate;
                }
            }

            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }

        [[nodiscard]] PresentStatus present_status(VkResult result) noexcept
        {
            switch (result)
            {
            case VK_SUCCESS:
                return PresentStatus::Presented;
            case VK_SUBOPTIMAL_KHR:
                return PresentStatus::Suboptimal;
            case VK_ERROR_OUT_OF_DATE_KHR:
                return PresentStatus::OutOfDate;
            default:
                return PresentStatus::Skipped;
            }
        }

        [[nodiscard]] std::string present_message(PresentStatus status)
        {
            switch (status)
            {
            case PresentStatus::Presented:
                return "Presented.";
            case PresentStatus::Suboptimal:
                return "Presented with a suboptimal swapchain.";
            case PresentStatus::OutOfDate:
                return "Swapchain is out of date.";
            case PresentStatus::Skipped:
                return "Present was skipped.";
            case PresentStatus::None:
            default:
                return {};
            }
        }

        [[nodiscard]] uint64_t completed_value_for_token(VkDevice device, const SubmissionToken& token)
        {
            if (!token.has_sync() || token.value == 0)
            {
                return token.value;
            }

            const VkSemaphore timeline = token.sync->timeline();
            if (device == VK_NULL_HANDLE || timeline == VK_NULL_HANDLE)
            {
                return token.value;
            }

            uint64_t value = 0;
            const VkResult result = vkGetSemaphoreCounterValue(device, timeline, &value);
            if (result != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkGetSemaphoreCounterValue(display frame token) failed. VkResult=" +
                                       std::to_string(static_cast<int>(result)));
                throw std::runtime_error("vkGetSemaphoreCounterValue(display frame token) failed. VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }

            return value;
        }

        [[nodiscard]] constexpr VkPipelineStageFlags2 display_present_signal_stages() noexcept
        {
            return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }

        [[nodiscard]] constexpr uint64_t swapchain_acquire_timeout_ns() noexcept
        {
            return 1'000'000;
        }

    }

    DisplayManager::DisplayManager(DisplayerRef displayer) noexcept
        : displayer_(displayer)
    {
    }

    DisplayManager::DisplayManager(DisplayerRef displayer, void* native_window) noexcept
        : displayer_(displayer),
          native_window_(native_window)
    {
    }

    DisplayManager::~DisplayManager()
    {
        std::lock_guard lock(mutex_);
        shutdown();
    }

    bool DisplayManager::has_swapchain() const noexcept
    {
        std::lock_guard lock(mutex_);
        return swapchain_ != VK_NULL_HANDLE;
    }

    Queue* DisplayManager::present_queue() const noexcept
    {
        std::lock_guard lock(mutex_);
        return present_queue_;
    }

    void DisplayManager::ensure_swapchain()
    {
        if (swapchain_ != VK_NULL_HANDLE)
        {
            return;
        }

        if (native_window_ == nullptr)
        {
            throw std::runtime_error("HardwareDisplayer requires a native Win32 GLFW window handle before presenting.");
        }

        create_surface();
        choose_present_queue();
        create_swapchain();
        create_sync_objects();
    }

    bool DisplayManager::native_window_available() const noexcept
    {
        if (native_window_ == nullptr)
        {
            return false;
        }

#if defined(_WIN32) || defined(_WIN64)
        return IsWindow(static_cast<HWND>(native_window_)) != FALSE;
#else
        return true;
#endif
    }

    void DisplayManager::create_surface()
    {
        if (surface_ != VK_NULL_HANDLE)
        {
            return;
        }

        instance_ = vulkan_instance();
        if (instance_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot create a Vulkan display surface without a Vulkan instance.");
        }

#if defined(_WIN32) || defined(_WIN64)
        if (vkCreateWin32SurfaceKHR == nullptr)
        {
            throw std::runtime_error("vkCreateWin32SurfaceKHR is not available. VK_KHR_win32_surface may be missing.");
        }

        VkWin32SurfaceCreateInfoKHR create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.hinstance = GetModuleHandleW(nullptr);
        create_info.hwnd = static_cast<HWND>(native_window_);

        throw_if_failed(vkCreateWin32SurfaceKHR(instance_, &create_info, nullptr, &surface_), "vkCreateWin32SurfaceKHR");
#else
        throw std::runtime_error("HardwareDisplayer currently supports only Win32 native windows.");
#endif
    }

    void DisplayManager::choose_present_queue()
    {
        HardwareContext::DeviceContext& context = main_device_context();
        device_manager_ = &context.device_manager;
        resource_manager_ = &context.resource_manager;
        instance_ = device_manager_->instance();
        device_ = device_manager_->logical_device();

        if (device_ == VK_NULL_HANDLE || device_manager_->physical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot present before the main Vulkan device is initialized.");
        }

        const auto supports_present = [this](Queue& queue) {
            VkBool32 present_supported = VK_FALSE;
            throw_if_failed(vkGetPhysicalDeviceSurfaceSupportKHR(device_manager_->physical_device(),
                                                                 queue.id().family_index,
                                                                 surface_,
                                                                 &present_supported),
                            "vkGetPhysicalDeviceSurfaceSupportKHR");
            return present_supported == VK_TRUE;
        };

        Queue* graphics_queue = nullptr;
        for (Queue* queue : device_manager_->queues_for(QueueCapability::Graphics))
        {
            if (graphics_queue == nullptr)
            {
                graphics_queue = queue;
            }
            if (queue != nullptr && supports_present(*queue))
            {
                present_queue_ = queue;
                return;
            }
        }

        if (graphics_queue == nullptr)
        {
            throw std::runtime_error("Main Vulkan device has no graphics queue for first-stage presentation.");
        }

        throw std::runtime_error("Main Vulkan graphics queue family does not support presentation for this Win32 surface.");
    }

    void DisplayManager::create_swapchain()
    {
        if (surface_ == VK_NULL_HANDLE || device_manager_ == nullptr || resource_manager_ == nullptr || present_queue_ == nullptr)
        {
            throw std::runtime_error("DisplayManager cannot create a swapchain before surface and present queue selection.");
        }

        VkSurfaceCapabilitiesKHR capabilities {};
        throw_if_failed(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_manager_->physical_device(), surface_, &capabilities),
                        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        swapchain_extent_ = choose_extent(capabilities, native_window_);
        if (swapchain_extent_.width == 0 || swapchain_extent_.height == 0)
        {
            throw std::runtime_error("Cannot create a swapchain for a zero-sized window.");
        }

        uint32_t format_count = 0;
        throw_if_failed(vkGetPhysicalDeviceSurfaceFormatsKHR(device_manager_->physical_device(), surface_, &format_count, nullptr),
                        "vkGetPhysicalDeviceSurfaceFormatsKHR");
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        if (format_count != 0)
        {
            throw_if_failed(vkGetPhysicalDeviceSurfaceFormatsKHR(device_manager_->physical_device(), surface_, &format_count, formats.data()),
                            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        }

        const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
        swapchain_format_ = surface_format.format;

        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        {
            throw std::runtime_error("Swapchain images do not support VK_IMAGE_USAGE_TRANSFER_DST_BIT.");
        }

        VkImageUsageFlags image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)
        {
            image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount)
        {
            image_count = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = swapchain_extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = image_usage;
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
        create_info.presentMode = choose_present_mode(device_manager_->physical_device(), surface_);
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        throw_if_failed(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
        name_vulkan_object(device_,
                           VK_OBJECT_TYPE_SWAPCHAIN_KHR,
                           reinterpret_cast<uint64_t>(swapchain_),
                           "horizon.swapchain");

        uint32_t native_image_count = 0;
        throw_if_failed(vkGetSwapchainImagesKHR(device_, swapchain_, &native_image_count, nullptr), "vkGetSwapchainImagesKHR");
        std::vector<VkImage> native_images(native_image_count);
        throw_if_failed(vkGetSwapchainImagesKHR(device_, swapchain_, &native_image_count, native_images.data()), "vkGetSwapchainImagesKHR");

        swapchain_images_.clear();
        swapchain_images_.reserve(native_images.size());
        for (size_t index = 0; index < native_images.size(); ++index)
        {
            HardwareImage image;
            auto handle = resource_pool().images.create([&] {
                return resource_manager_->wrap_swapchain_image(native_images[index],
                                                               swapchain_format_,
                                                               swapchain_extent_,
                                                               image_usage,
                                                               "horizon.swapchain." + std::to_string(index));
            });

            ResourceBridge::set(image, make_token<ImageStore>(std::move(handle)));
            swapchain_images_.push_back(std::move(image));
        }
    }

    void DisplayManager::create_sync_objects()
    {
        if (device_ == VK_NULL_HANDLE || swapchain_images_.empty())
        {
            throw std::runtime_error("DisplayManager cannot create present sync objects without swapchain images.");
        }

        image_available_.resize(swapchain_images_.size());
        render_finished_.resize(swapchain_images_.size());
        submitted_frames_.clear();
        submitted_frames_.resize(swapchain_images_.size());

        VkSemaphoreCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (size_t index = 0; index < swapchain_images_.size(); ++index)
        {
            throw_if_failed(vkCreateSemaphore(device_, &create_info, nullptr, &image_available_[index]), "vkCreateSemaphore(image_available)");
            name_vulkan_object(device_,
                               VK_OBJECT_TYPE_SEMAPHORE,
                               reinterpret_cast<uint64_t>(image_available_[index]),
                               "horizon.swapchain.image_available." + std::to_string(index));
            throw_if_failed(vkCreateSemaphore(device_, &create_info, nullptr, &render_finished_[index]), "vkCreateSemaphore(render_finished)");
            name_vulkan_object(device_,
                               VK_OBJECT_TYPE_SEMAPHORE,
                               reinterpret_cast<uint64_t>(render_finished_[index]),
                               "horizon.swapchain.render_finished." + std::to_string(index));
        }
    }

    bool DisplayManager::reclaim_frame(uint32_t frame_index)
    {
        if (present_queue_ == nullptr || frame_index >= submitted_frames_.size())
        {
            return true;
        }

        std::optional<SubmissionToken>& token = submitted_frames_[frame_index];
        if (!token)
        {
            return true;
        }

        if (!token->has_sync() || token->value == 0)
        {
            token.reset();
            return true;
        }

        const uint64_t completed = completed_value_for_token(device_, *token);
        if (completed < token->value)
        {
            return false;
        }

        Queue* submitted_queue = device_manager_ == nullptr ? nullptr : device_manager_->queue_by_id(token->queue);
        if (submitted_queue != nullptr)
        {
            submitted_queue->retire_completed();
        }
        token.reset();
        return true;
    }

    std::optional<uint32_t> DisplayManager::find_reusable_frame()
    {
        if (image_available_.empty())
        {
            return std::nullopt;
        }

        const uint32_t frame_count = static_cast<uint32_t>(image_available_.size());
        for (uint32_t offset = 0; offset < frame_count; ++offset)
        {
            const uint32_t frame = (frame_index_ + offset) % frame_count;
            if (reclaim_frame(frame))
            {
                frame_index_ = frame;
                return frame;
            }
        }

        return std::nullopt;
    }

    SwapchainAcquire DisplayManager::acquire_next_image()
    {
        std::lock_guard lock(mutex_);
        return acquire_next_image_unlocked();
    }

    SwapchainAcquire DisplayManager::acquire_next_image_unlocked()
    {
        SwapchainAcquire acquire;

        if (swapchain_ == VK_NULL_HANDLE)
        {
            acquire.status = fake_status_;
            acquire.message = fake_message_;
            return acquire;
        }

        if (image_available_.empty())
        {
            acquire.status = PresentStatus::Skipped;
            acquire.message = "Swapchain has no image-available semaphores.";
            return acquire;
        }

        std::optional<uint32_t> reusable_frame = find_reusable_frame();
        if (!reusable_frame)
        {
            acquire.status = PresentStatus::Skipped;
            acquire.message = "No reusable swapchain acquire semaphore is available yet.";
            return acquire;
        }

        const uint32_t frame = *reusable_frame;
        acquire.frame_index = frame;

        const VkResult result = vkAcquireNextImageKHR(device_,
                                                      swapchain_,
                                                      swapchain_acquire_timeout_ns(),
                                                      image_available_[frame],
                                                      VK_NULL_HANDLE,
                                                      &acquire.image_index);

        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        {
            acquire.acquired = true;
            acquire.status = present_status(result);
            acquire.message = present_message(acquire.status);
            return acquire;
        }

        acquire.status = present_status(result);
        acquire.message = present_message(acquire.status);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            destroy_swapchain();
        }
        else if (result == VK_NOT_READY || result == VK_TIMEOUT)
        {
            acquire.status = PresentStatus::Skipped;
            acquire.message = "No swapchain image is ready for nonblocking acquire.";
        }
        else if (result == VK_ERROR_SURFACE_LOST_KHR)
        {
            acquire.message = "Display surface was lost.";
            destroy_swapchain();
            destroy_surface();
        }
        else
        {
            acquire.message = "vkAcquireNextImageKHR failed. VkResult=" + std::to_string(static_cast<int>(result));
        }
        return acquire;
    }

    PreparedPresent DisplayManager::prepare_present(PresentDesc& desc)
    {
        std::lock_guard lock(mutex_);

        PreparedPresent prepared;
        prepared.immediate_result.displayer = desc.displayer;
        prepared.immediate_result.image = desc.image;

        if (pending_frame_)
        {
            prepared.immediate_result.status = PresentStatus::Skipped;
            prepared.immediate_result.message = "DisplayManager already has an acquired swapchain image pending presentation.";
            return prepared;
        }

        if (!native_window_available())
        {
            shutdown();
            prepared.immediate_result.status = fake_status_;
            prepared.immediate_result.message = "Native display window is no longer available.";
            return prepared;
        }

        if (!native_window_drawable(native_window_))
        {
            prepared.immediate_result.status = PresentStatus::Skipped;
            prepared.immediate_result.message = "Native display window has no drawable client area.";
            return prepared;
        }

        ensure_swapchain();

        SwapchainAcquire acquire = acquire_next_image_unlocked();
        if (!acquire.acquired)
        {
            prepared.immediate_result.status = acquire.status == PresentStatus::None ? PresentStatus::Skipped : acquire.status;
            prepared.immediate_result.message = acquire.message;
            return prepared;
        }

        if (acquire.image_index >= swapchain_images_.size())
        {
            prepared.immediate_result.status = PresentStatus::Skipped;
            prepared.immediate_result.message = "vkAcquireNextImageKHR returned an out-of-range swapchain image index.";
            return prepared;
        }

        const uint32_t frame = acquire.frame_index;
        desc.swapchain_image = { static_cast<const ResourceHandle&>(swapchain_images_[acquire.image_index]) };

        prepared.ready_for_submit = true;
        prepared.present_queue = present_queue_;
        prepared.wait = {
            image_available_[frame],
            0,
            display_acquire_wait_stages(),
        };
        prepared.signal = {
            render_finished_[acquire.image_index],
            0,
            display_present_signal_stages(),
        };
        pending_frame_ = PendingFrame {
            .image_index = acquire.image_index,
            .frame_index = frame,
            .render_finished = render_finished_[acquire.image_index],
        };
        return prepared;
    }

    PresentResult DisplayManager::present(const PresentDesc& desc, const SubmissionToken& producer)
    {
        std::lock_guard lock(mutex_);

        PresentResult result;
        result.displayer = desc.displayer;
        result.image = desc.image;

        if (swapchain_ == VK_NULL_HANDLE)
        {
            result.status = fake_status_;
            result.message = fake_message_;
            return result;
        }

        if (!pending_frame_)
        {
            result.status = PresentStatus::Skipped;
            result.message = "DisplayManager has no acquired swapchain image for this present.";
            return result;
        }

        if (present_queue_ == nullptr)
        {
            pending_frame_.reset();
            result.status = PresentStatus::Skipped;
            result.message = "DisplayManager has no present queue.";
            return result;
        }

        PendingFrame pending = *pending_frame_;
        VkSemaphore wait_semaphore = pending.render_finished;
        if (pending.frame_index < submitted_frames_.size())
        {
            submitted_frames_[pending.frame_index] = producer;
        }

        VkPresentInfoKHR present_info {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = wait_semaphore == VK_NULL_HANDLE ? 0u : 1u;
        present_info.pWaitSemaphores = wait_semaphore == VK_NULL_HANDLE ? nullptr : &wait_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_;
        present_info.pImageIndices = &pending.image_index;

        const VkResult vk_result = present_queue_->present(present_info);
        result.status = present_status(vk_result);
        result.message = present_message(result.status);

        pending_frame_.reset();
        frame_index_ = image_available_.empty()
            ? 0
            : ((pending.frame_index + 1) % static_cast<uint32_t>(image_available_.size()));

        if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            destroy_swapchain();
            return result;
        }

        if (vk_result == VK_ERROR_SURFACE_LOST_KHR)
        {
            destroy_swapchain();
            destroy_surface();
            result.message = "Display surface was lost.";
            return result;
        }

        if (vk_result != VK_SUCCESS && vk_result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("vkQueuePresentKHR failed. VkResult=" + std::to_string(static_cast<int>(vk_result)));
        }

        return result;
    }

    void DisplayManager::cancel_prepared_present() noexcept
    {
        std::lock_guard lock(mutex_);

        if (!pending_frame_)
        {
            return;
        }

        pending_frame_.reset();
        destroy_swapchain();
    }

    void DisplayManager::set_fake_present_status_for_tests(PresentStatus status, std::string message)
    {
        std::lock_guard lock(mutex_);
        fake_status_ = status;
        fake_message_ = std::move(message);
    }

    void DisplayManager::destroy_swapchain() noexcept
    {
        pending_frame_.reset();

        if (present_queue_ != nullptr)
        {
            try
            {
                present_queue_->wait_idle();
                present_queue_->retire_completed();
            }
            catch (...)
            {
            }
        }
        else if (device_ != VK_NULL_HANDLE)
        {
            (void)vkDeviceWaitIdle(device_);
        }

        swapchain_images_.clear();

        for (VkSemaphore semaphore : image_available_)
        {
            if (device_ != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        image_available_.clear();

        for (VkSemaphore semaphore : render_finished_)
        {
            if (device_ != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        render_finished_.clear();
        submitted_frames_.clear();

        if (device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }

        swapchain_ = VK_NULL_HANDLE;
        swapchain_format_ = VK_FORMAT_UNDEFINED;
        swapchain_extent_ = {};
        frame_index_ = 0;
    }

    void DisplayManager::destroy_surface() noexcept
    {
        if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }

        surface_ = VK_NULL_HANDLE;
    }

    void DisplayManager::shutdown() noexcept
    {
        destroy_swapchain();
        destroy_surface();
        present_queue_ = nullptr;
        resource_manager_ = nullptr;
        device_manager_ = nullptr;
        device_ = VK_NULL_HANDLE;
        instance_ = VK_NULL_HANDLE;
        native_window_ = nullptr;
    }

    std::shared_ptr<DisplayManager> make_fake_display_manager(DisplayerRef displayer)
    {
        return std::make_shared<DisplayManager>(displayer);
    }

    void register_display_manager(std::shared_ptr<DisplayManager> manager)
    {
        if (!manager || manager->displayer().id == 0)
        {
            return;
        }

        std::lock_guard lock(display_registry_mutex());
        std::vector<std::weak_ptr<DisplayManager>>& registry = display_registry();

        registry.erase(std::remove_if(registry.begin(), registry.end(), [&](const std::weak_ptr<DisplayManager>& weak) {
                           std::shared_ptr<DisplayManager> existing = weak.lock();
                           return !existing || existing->displayer().id == manager->displayer().id;
                       }),
                       registry.end());

        registry.push_back(std::move(manager));
    }

    std::shared_ptr<DisplayManager> display_manager_for(DisplayerRef displayer)
    {
        if (displayer.id == 0)
        {
            return {};
        }

        std::lock_guard lock(display_registry_mutex());
        std::vector<std::weak_ptr<DisplayManager>>& registry = display_registry();

        std::shared_ptr<DisplayManager> result;
        registry.erase(std::remove_if(registry.begin(), registry.end(), [&](const std::weak_ptr<DisplayManager>& weak) {
                           std::shared_ptr<DisplayManager> manager = weak.lock();
                           if (!manager)
                           {
                               return true;
                           }
                           if (manager->displayer().id == displayer.id)
                           {
                               result = std::move(manager);
                           }
                           return false;
                       }),
                       registry.end());

        return result;
    }
}
