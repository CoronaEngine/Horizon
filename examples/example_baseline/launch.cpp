#include "example_baseline.h"

#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

void run_example_baseline()
{
    const std::filesystem::path executable = std::filesystem::u8path(HORIZON_TUTORIAL_EXECUTABLE);
    std::filesystem::current_path(executable.parent_path());
#ifdef _WIN32
    const auto status = _wspawnl(_P_WAIT, executable.c_str(), L"VulkanTutorialBaseline", nullptr);
    if (status == 0) return;
    if (status != -1) throw std::runtime_error("Vulkan Tutorial exited with code " + std::to_string(status));
#else
    execl(executable.c_str(), "VulkanTutorialBaseline", static_cast<char*>(nullptr));
#endif
    throw std::system_error(errno, std::generic_category(), "launching Vulkan Tutorial");
}
