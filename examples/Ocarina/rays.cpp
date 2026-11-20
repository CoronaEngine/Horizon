#include "rays.h"
#include "rhi/context.h"
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace ray_example {
ocarina::Device open_device() {
    namespace fs = std::filesystem;
    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring filename(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
    if (!length || length >= filename.size())
        throw std::runtime_error("Cannot locate HorizonExamples");
    filename.resize(length);
    const auto executable = fs::path(filename);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string filename(size, '\0');
    if (_NSGetExecutablePath(filename.data(), &size))
        throw std::runtime_error("Cannot locate HorizonExamples");
    const auto executable = fs::canonical(filename.c_str());
#else
    const auto executable = fs::read_symlink("/proc/self/exe");
#endif
    auto& context = ocarina::RHIContext::instance(executable.parent_path());
    auto device = context.create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    return device;
}
} // namespace ray_example
