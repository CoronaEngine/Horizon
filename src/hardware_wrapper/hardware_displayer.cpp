#include "hardware_wrapper_vulkan/display/display_manager.h"

#include <atomic>
#include <memory>

namespace Corona::Horizon
{
    namespace
    {
        std::atomic_uintptr_t next_displayer_id { 1 };
    }

    HardwareDisplayer::HardwareDisplayer() = default;

    HardwareDisplayer::HardwareDisplayer(void* native_window)
    {
        const std::uintptr_t id = next_displayer_id.fetch_add(1, std::memory_order_relaxed);
        displayer_ = { id == 0 ? next_displayer_id.fetch_add(1, std::memory_order_relaxed) : id };

        auto manager = std::make_shared<DisplayManager>(displayer_, native_window);
        register_display_manager(manager);
        manager_ = std::move(manager);
    }

    HardwareDisplayer::~HardwareDisplayer() = default;
}
