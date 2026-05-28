#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <volk.h>

#include "hardware_wrapper_vulkan/hardware/execution.h"

namespace Corona::Horizon
{
    struct SwapchainAcquire
    {
        bool acquired { false };
        uint32_t image_index { 0 };
        SubmitWait wait {};
        PresentStatus status { PresentStatus::None };
        std::string message;
    };

    class DisplayManager
    {
    public:
        DisplayManager() = default;
        explicit DisplayManager(DisplayerRef displayer) noexcept;

        [[nodiscard]] DisplayerRef displayer() const noexcept { return displayer_; }
        [[nodiscard]] bool has_swapchain() const noexcept { return swapchain_ != VK_NULL_HANDLE; }

        [[nodiscard]] SwapchainAcquire acquire_next_image();
        [[nodiscard]] PresentResult present(const PresentDesc& desc, const SubmissionToken& producer);

        void set_fake_present_status_for_tests(PresentStatus status, std::string message = {});

    private:
        DisplayerRef displayer_ {};
        VkDevice device_ { VK_NULL_HANDLE };
        VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
        PresentStatus fake_status_ { PresentStatus::Skipped };
        std::string fake_message_ { "No swapchain has been created for this DisplayManager." };
    };

    [[nodiscard]] std::shared_ptr<DisplayManager> make_fake_display_manager(DisplayerRef displayer);
}
