#include "staged_module.h"

#include <memory>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vision::inline hotfix::detail {
namespace {

using ModuleEntry = ModuleInterface *(*)();

void close_module(void *handle) noexcept {
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

void *open_module(const std::filesystem::path &path) noexcept {
#if defined(_WIN32)
    return ::LoadLibraryW(path.c_str());
#else
    int flags = RTLD_NOW | RTLD_LOCAL;
#if defined(__APPLE__)
    flags |= RTLD_FIRST;
#endif
    return ::dlopen(path.c_str(), flags);
#endif
}

ModuleEntry find_entry(void *handle, [[maybe_unused]] const std::filesystem::path &path) {
#if defined(_WIN32)
    return reinterpret_cast<ModuleEntry>(::GetProcAddress(static_cast<HMODULE>(handle), "module_interface"));
#else
    void *entry = ::dlsym(handle, "module_interface");
    Dl_info image{};
    std::error_code error;
    // A lookup on a POSIX handle can also find a symbol in a dependency.
    if (!entry || ::dladdr(entry, &image) == 0 || !image.dli_fname ||
        !std::filesystem::equivalent(path, image.dli_fname, error)) {
        return nullptr;
    }
    return reinterpret_cast<ModuleEntry>(entry);
#endif
}

}// namespace

ModuleInterface *load_staged_module(const std::filesystem::path &path) noexcept {
    if (!path.is_absolute()) {
        return nullptr;
    }

    std::unique_ptr<void, decltype(&close_module)> module{open_module(path), close_module};
    if (!module) {
        return nullptr;
    }

    try {
        const auto entry = find_entry(module.get(), path);
        auto *module_interface = entry ? entry() : nullptr;
        if (!module_interface) {
            return nullptr;
        }
        // Constructors and objects can outlive a version or HotfixSystem.
        // Keep their code loaded for the rest of the process.
        module.release();
        return module_interface;
    } catch (...) {
        // The handle stays alive through destruction of any module-defined exception.
        return nullptr;
    }
}

}// namespace vision::inline hotfix::detail
