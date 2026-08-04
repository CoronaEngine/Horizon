#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <volk.h>

#include "hardware_wrapper_vulkan/hardware/execution.h"

namespace Corona::Horizon
{
    class DeviceManager;
    class ResourceManager;

    struct SwapchainAcquire
    {
        bool acquired { false };
        uint32_t image_index { 0 };
        uint32_t frame_index { 0 };
        SubmitWait wait {};
        PresentStatus status { PresentStatus::None };
        std::string message;
    };

    struct PreparedPresent
    {
        bool ready_for_submit { false };
        Queue* present_queue { nullptr };
        std::vector<SubmitWait> waits;
        SubmitSignal signal {};
        PresentResult immediate_result {};
    };

    [[nodiscard]] constexpr VkPipelineStageFlags2 display_acquire_wait_stages() noexcept
    {
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }

    class DisplayManager
    {
    public:
        DisplayManager() = default;
        explicit DisplayManager(DisplayerRef displayer) noexcept;
        DisplayManager(DisplayerRef displayer, void* native_window) noexcept;
        ~DisplayManager();

        DisplayManager(const DisplayManager&) = delete;
        DisplayManager& operator=(const DisplayManager&) = delete;
        DisplayManager(DisplayManager&&) = delete;
        DisplayManager& operator=(DisplayManager&&) = delete;

        [[nodiscard]] DisplayerRef displayer() const noexcept { return displayer_; }
        [[nodiscard]] bool has_swapchain() const noexcept;
        [[nodiscard]] Queue* present_queue() const noexcept;

        [[nodiscard]] SwapchainAcquire acquire_next_image();
        [[nodiscard]] PreparedPresent prepare_present(PresentDesc& desc);
        [[nodiscard]] PresentResult present(const PresentDesc& desc, const SubmissionToken& producer);
        void cancel_prepared_present() noexcept;

        // prepare_present 未能给出可提交帧（窗口最小化、acquire 超时、无交换链等）
        // 时由提交侧调用。此时渲染命令仍然提交给了 GPU，帧环必须照样推进并设卡，
        // 否则下一帧 CPU 会覆写仍在飞的 UBO 槽。
        void note_skipped_frame(const SubmissionToken& producer);

        void set_fake_present_status_for_tests(PresentStatus status, std::string message = {});

    private:
        struct PendingFrame
        {
            uint32_t image_index { 0 };
            uint32_t frame_index { 0 };
            VkSemaphore render_finished { VK_NULL_HANDLE };
        };

        void ensure_swapchain();
        void create_surface();
        void choose_present_queue();
        void create_swapchain();
        void create_sync_objects();
        [[nodiscard]] bool native_window_available() const noexcept;
        [[nodiscard]] SwapchainAcquire acquire_next_image_unlocked();
        // present 的锁内段。gate 出参带回"新帧槽上一次占用者"的 token，由调用者
        // 在锁外等待。
        [[nodiscard]] PresentResult present_locked(const PresentDesc& desc,
                                                  const SubmissionToken& producer,
                                                  std::optional<SubmissionToken>& gate);
        // 记录本帧的生产者 token，推进帧环，返回新槽上一次占用者的 token
        // （调用者必须在放开 mutex_ 之后再等它）。
        [[nodiscard]] std::optional<SubmissionToken> rotate_frame_unlocked(const SubmissionToken& producer);
        // 在锁外等待 token 完成，并回收其命令缓冲。
        void await_frame_slot(const std::optional<SubmissionToken>& token) const;
        void destroy_swapchain() noexcept;
        void destroy_surface() noexcept;
        void shutdown() noexcept;

        DisplayerRef displayer_ {};
        mutable std::mutex mutex_;
        void* native_window_ {};
        VkInstance instance_ { VK_NULL_HANDLE };
        VkDevice device_ { VK_NULL_HANDLE };
        DeviceManager* device_manager_ { nullptr };
        ResourceManager* resource_manager_ { nullptr };
        Queue* present_queue_ { nullptr };
        VkSurfaceKHR surface_ { VK_NULL_HANDLE };
        VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
        VkFormat swapchain_format_ { VK_FORMAT_UNDEFINED };
        VkExtent2D swapchain_extent_ {};
        uint32_t swapchain_min_image_count_ { 0 };
        std::vector<HardwareImage> swapchain_images_;
        std::vector<VkSemaphore> image_available_;
        std::vector<VkSemaphore> render_finished_;
        // 按帧槽索引（frame_ring_slot()），生命周期独立于交换链：无交换链时渲染
        // 命令仍在提交，帧槽仍需设卡。
        std::vector<std::optional<SubmissionToken>> submitted_frames_;
        // 按交换链图像索引。
        std::vector<std::optional<SubmissionToken>> present_tokens_;
        bool needs_recreate_ { false };
        std::optional<PendingFrame> pending_frame_;
        PresentStatus fake_status_ { PresentStatus::Skipped };
        std::string fake_message_ { "No swapchain has been created for this DisplayManager." };
    };

    [[nodiscard]] std::shared_ptr<DisplayManager> make_fake_display_manager(DisplayerRef displayer);
    void register_display_manager(std::shared_ptr<DisplayManager> manager);
    [[nodiscard]] std::shared_ptr<DisplayManager> display_manager_for(DisplayerRef displayer);
}
