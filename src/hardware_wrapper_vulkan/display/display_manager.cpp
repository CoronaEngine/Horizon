#include "display_manager.h"

#include <utility>

namespace Corona::Horizon
{
    DisplayManager::DisplayManager(DisplayerRef displayer) noexcept
        : displayer_(displayer)
    {
    }

    SwapchainAcquire DisplayManager::acquire_next_image()
    {
        SwapchainAcquire acquire;

        if (swapchain_ == VK_NULL_HANDLE)
        {
            acquire.status = fake_status_;
            acquire.message = fake_message_;
            return acquire;
        }

        acquire.status = PresentStatus::Skipped;
        acquire.message = "Swapchain acquisition is not wired to platform surfaces yet.";
        return acquire;
    }

    PresentResult DisplayManager::present(const PresentDesc& desc, const SubmissionToken&)
    {
        PresentResult result;
        result.status = fake_status_;
        result.displayer = desc.displayer;
        result.image = desc.image;

        if (swapchain_ == VK_NULL_HANDLE)
        {
            result.message = fake_message_;
            return result;
        }

        result.status = PresentStatus::Skipped;
        result.message = "Swapchain presentation is not wired to platform surfaces yet.";
        return result;
    }

    void DisplayManager::set_fake_present_status_for_tests(PresentStatus status, std::string message)
    {
        fake_status_ = status;
        fake_message_ = std::move(message);
    }

    std::shared_ptr<DisplayManager> make_fake_display_manager(DisplayerRef displayer)
    {
        return std::make_shared<DisplayManager>(displayer);
    }
}
