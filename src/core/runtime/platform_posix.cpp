#include "platform.h"

#include "core/util/logging.h"
#include "fmt/format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>

namespace horizon::core {

namespace detail {

string dynamic_loader_error() {
    const char *message = ::dlerror();
    return message == nullptr ? "unknown dynamic-loader error" : string{message};
}

string demangle_symbol(const char *name) {
    if (name == nullptr) {
        return "???";
    }
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    string result = status == 0 && demangled != nullptr ? demangled : name;
    std::free(demangled);
    return result;
}

}// namespace detail

void *dynamic_module_load(const fs::path &path) noexcept {
    const string path_string = path.string();
    ::dlerror();
    void *module = ::dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        OC_ERROR_FORMAT("Failed to load dynamic module '{}', reason: {}.",
                        path_string, detail::dynamic_loader_error());
    }
    return module;
}

void dynamic_module_destroy(void *handle) noexcept {
    if (handle != nullptr) {
        ::dlclose(handle);
    }
}

void *dynamic_module_find_symbol(void *handle, string_view name_view) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const string name{name_view};
    ::dlerror();
    void *symbol = ::dlsym(handle, name.c_str());
    const char *error = ::dlerror();
    if (error != nullptr) {
        OC_INFO_FORMAT("Failed to load symbol '{}', reason: {}.", name, error);
        return nullptr;
    }
    return symbol;
}

string dynamic_module_name(string_view name) noexcept {
#if defined(__APPLE__)
    return "lib" + string{name} + ".dylib";
#else
    return "lib" + string{name} + ".so";
#endif
}

vector<TraceItem> traceback(int top) noexcept {
    void *frames[100]{};
    const int frame_count = ::backtrace(frames, 100);
    const int first_frame = std::clamp(top + 1, 0, frame_count);
    vector<TraceItem> trace;
    trace.reserve(static_cast<size_t>(frame_count - first_frame));

    for (int index = first_frame; index < frame_count; ++index) {
        Dl_info info{};
        TraceItem item{};
        item.address = reinterpret_cast<uint64_t>(frames[index]);
        if (::dladdr(frames[index], &info) != 0) {
            item.module = info.dli_fname == nullptr ? "???" : info.dli_fname;
            item.symbol = detail::demangle_symbol(info.dli_sname);
            item.offset = info.dli_saddr == nullptr
                              ? 0u
                              : static_cast<size_t>(reinterpret_cast<uintptr_t>(frames[index]) -
                                                    reinterpret_cast<uintptr_t>(info.dli_saddr));
        } else {
            item.module = "???";
            item.symbol = "???";
            item.offset = 0u;
        }
        trace.emplace_back(std::move(item));
    }
    return trace;
}

string traceback_string(int top) noexcept {
    string result;
    const vector<TraceItem> trace = traceback(top + 1);
    for (size_t index = 0; index < trace.size(); ++index) {
        const TraceItem &item = trace[index];
        result += fmt::format("\n    {:>2}: {} :: {} + {}",
                              index, item.module, item.symbol, item.offset);
    }
    return result;
}

}// namespace horizon::core
