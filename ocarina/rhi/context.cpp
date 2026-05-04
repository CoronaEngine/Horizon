//
// Created by Zero on 06/06/2022.
//

#include "context.h"
#include "core/runtime/dynamic_module.h"
#include "rhi/device.h"
#include "core/runtime/platform.h"

namespace ocarina {

#ifdef _MSC_VER
static constexpr string_view backend_prefix = "ocarina-backend-";
#else
static constexpr string_view backend_prefix = "libocarina-backend-";
#endif

namespace detail {
[[nodiscard]] fs::path create_runtime_directory(fs::path path) noexcept {
    path = fs::canonical(path);
    if (fs::is_directory(path)) {
        return path;
    }
    return fs::canonical(path.parent_path());
}

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

RHIContext &RHIContext::init(const fs::path &path, std::string_view cache_dir) {
    impl_ = std::move(ocarina::make_unique<Impl>());
    impl_->runtime_directory = detail::create_runtime_directory(path);
    impl_->cache_directory = runtime_directory() / cache_dir;
    DynamicModule::add_search_path(runtime_directory());
    detail::create_directory_if_necessary(cache_directory());
    return *this;
}

RHIContext *RHIContext::s_context = nullptr;

RHIContext &RHIContext::instance() noexcept {
    if (s_context == nullptr) {
        s_context = new RHIContext();
        s_context->init(fs::current_path());
    }
    return *s_context;
}

void RHIContext::destroy_instance() {
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
    write_file(cache_directory() / fn, text);
}

string RHIContext::read_global_cache(const string &fn) const noexcept {
    return read_file(cache_directory() / fn);
}

void RHIContext::clear_cache() const noexcept {
    if (fs::exists(impl_->cache_directory)) {
        fs::remove_all(impl_->cache_directory);
    }
}

bool RHIContext::is_exist_cache(const string &fn) const noexcept {
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

Device RHIContext::create_device(const string &backend_name, const ocarina::InstanceCreation &instance_creation) noexcept {
    std::string full_backend_name = detail::backend_full_name(backend_name);
    auto d = obtain_module(dynamic_module_name(full_backend_name));
    using Constructor = Device::Impl *(RHIContext *, const InstanceCreation &instance_creation);
    auto create_device = reinterpret_cast<Constructor *>(d->function_ptr("create_device"));
    auto destroy_func = reinterpret_cast<Device::Deleter *>(d->function_ptr("destroy"));
    return Device{Device::Handle{create_device(this, instance_creation), destroy_func}};
}

Device RHIContext::create_device(const string &backend_name) noexcept {
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