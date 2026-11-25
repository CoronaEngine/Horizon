//
// Created by Zero on 06/06/2022.
//

#include "context.h"
#include "core/util/switch_profile.h"
#include "core/runtime/dynamic_module.h"
#include "rhi/device.h"
#include "core/runtime/platform.h"

#include <mutex>

#if defined(_WIN32)
#include "core/runtime/oc_windows.h"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace ocarina {

static constexpr string_view backend_prefix = "ocarina-backend-";

namespace {
std::mutex context_mutex;

fs::path executable_directory() {
    std::error_code error;
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    OC_ERROR_IF(size == 0 || size == path.size(), "GetModuleFileNameW failed to return the executable path");
    path.resize(size);
    const auto directory = fs::canonical(fs::path{path}.parent_path(), error);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    OC_ERROR_IF(_NSGetExecutablePath(path.data(), &size) != 0, "Failed to get the executable path");
    const auto directory = fs::canonical(path.c_str(), error).parent_path();
#elif defined(__linux__)
    const auto directory = fs::read_symlink("/proc/self/exe", error).parent_path();
#else
#error Unsupported platform for executable directory lookup
#endif
    OC_ERROR_IF(error.value() != 0, "Failed to locate the executable directory: ", error.message());
    return directory;
}
}// namespace

namespace detail {
bool create_directory_if_necessary(const fs::path &path) {
    if (fs::exists(path)) {
        return false;
    }
    try {
        fs::create_directory(path);
        OC_INFO("create folder ", path.string());
    } catch (fs::filesystem_error &e) {
        OC_WARNING("Failed to create folder ", path, ", reason: ", e.what());
    }
    return true;
}

[[nodiscard]] std::string backend_full_name(const string &name) {
    return string(backend_prefix) + name;
}
}// namespace detail

RHIContext::RHIContext() : impl_(ocarina::make_unique<Impl>()) {
    impl_->runtime_directory = executable_directory();
    DynamicModule::add_search_path(runtime_directory());
    set_cache_directory(".cache");
}

void RHIContext::set_cache_directory(const fs::path &directory) {
    impl_->cache_directory = runtime_directory() / directory;
    detail::create_directory_if_necessary(cache_directory());
}

RHIContext *RHIContext::s_context = nullptr;

RHIContext &RHIContext::instance() noexcept {
    std::lock_guard lock{context_mutex};
    if (s_context == nullptr) {
        s_context = new RHIContext();
    }
    return *s_context;
}

void RHIContext::destroy_instance() {
    std::lock_guard lock{context_mutex};
    if (s_context) {
        delete s_context;
        s_context = nullptr;
    }
}

RHIContext::~RHIContext() noexcept {
    OC_INFO("context was destructed !");
}

const fs::path &RHIContext::runtime_directory() const noexcept {
    return impl_->runtime_directory;
}

const fs::path &RHIContext::cache_directory() const noexcept {
    return impl_->cache_directory;
}

bool RHIContext::create_directory_if_necessary(const fs::path &path) {
    return detail::create_directory_if_necessary(path);
}

string RHIContext::read_file(const fs::path &fn) {
    switch_profile::Scope profile{"RHIContext::read_file", "file_io"};
    std::ifstream fst;
    fst.open(fn.c_str());
    std::stringstream buffer;
    buffer << fst.rdbuf();
    return buffer.str();
}

void RHIContext::write_file(const fs::path &fn, const std::string &text) {
    std::ofstream fs;
    fs.open(fn.c_str());
    fs << text;
    fs.close();
}

void RHIContext::write_global_cache(const string &fn, const string &text) const noexcept {
    switch_profile::Scope profile{"RHIContext::write_global_cache", "cache_io"};
    write_file(cache_directory() / fn, text);
}

string RHIContext::read_global_cache(const string &fn) const noexcept {
    switch_profile::Scope profile{"RHIContext::read_global_cache", "cache_io"};
    return read_file(cache_directory() / fn);
}

void RHIContext::clear_cache() const noexcept {
    if (fs::exists(impl_->cache_directory)) {
        fs::remove_all(impl_->cache_directory);
    }
}

bool RHIContext::is_exist_cache(const string &fn) const noexcept {
    switch_profile::Scope profile{"RHIContext::is_exist_cache", "cache_io"};
    if (!impl_->use_cache) {
        return false;
    }
    auto path = cache_directory() / fn;
    return fs::exists(path);
}

const DynamicModule *RHIContext::obtain_module(const string &module_name) noexcept {
    auto iter = impl_->modules.find(module_name);
    DynamicModule *ret = nullptr;
    if (iter == impl_->modules.cend()) {
        DynamicModule d(module_name);
        impl_->modules.insert(std::make_pair(string(module_name), std::move(d)));
        ret = &impl_->modules.at(module_name);
    } else {
        ret = &iter->second;
    }
    return ret;
}

const DynamicModule *RHIContext::obtain_module_at(const fs::path &absolute_path) noexcept {
    if (!absolute_path.is_absolute()) {
        return nullptr;
    }
    const auto utf8 = absolute_path.u8string();
    string key{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
    if (auto iter = impl_->modules.find(key); iter != impl_->modules.end()) {
        return &iter->second;
    }
    auto module = DynamicModule::from_path(absolute_path);
    if (!module.handle()) {
        return nullptr;
    }
    return &impl_->modules.emplace(std::move(key), std::move(module)).first->second;
}

void RHIContext::unload_module(void *handle) noexcept {
    dynamic_module_destroy(handle);
}

bool RHIContext::unload_module(const std::string &module_name) noexcept {
    auto iter = impl_->modules.find(module_name);
    if (iter == impl_->modules.cend()) {
        return false;
    }
    unload_module(iter->second.handle());
    impl_->modules.erase(iter);
    return true;
}

Device RHIContext::create_device() noexcept {
    constexpr auto backend_name = "vulkan-compute";
    std::string full_backend_name = detail::backend_full_name(backend_name);
    auto d = obtain_module(dynamic_module_name(full_backend_name));
    auto create_device = reinterpret_cast<Device::Creator *>(d->function_ptr("create_device"));
    if (!create_device) {
        OC_ERROR("Failed to create device for backend ", backend_name);
    }
    auto destroy_func = reinterpret_cast<Device::Deleter *>(d->function_ptr("destroy"));
    return Device{Device::Handle{create_device(this), destroy_func}};
}

}// namespace ocarina
