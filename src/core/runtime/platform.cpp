//
// Created by Zero on 09/06/2022.
//

#include "platform.h"
#if defined(_WIN32)
#include "oc_windows.h"
#include <dbghelp.h>
#else
#include <algorithm>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif
#include "fmt/format.h"
#include "core/util/logging.h"
#include "core/util/util.h"

namespace ocarina {

namespace detail {
#if defined(_WIN32)
[[nodiscard]] ocarina::string win32_last_error_message() {
    char *buffer = nullptr;
    auto err_code = GetLastError();
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0, nullptr);
    ocarina::string err_msg{fmt::format("{} (code = 0x{:x}).", buffer ? buffer : "Win32 error", err_code)};
    LocalFree(buffer);
    return err_msg;
}
#else
[[nodiscard]] string dynamic_loader_error() {
    const char *message = ::dlerror();
    return message == nullptr ? "unknown dynamic-loader error" : string{message};
}
#endif
}// namespace detail

namespace {

void *load_dynamic_module(const fs::path &path, bool fatal) noexcept {
    const auto utf8 = path.u8string();
    const string path_string{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
    TIMER_TAG(dynamic_module_load, "load module: " + path_string)
#if defined(_WIN32)
    auto module = LoadLibraryW(path.c_str());
#else
    ::dlerror();
    auto *module = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (module == nullptr) [[unlikely]] {
#if defined(_WIN32)
        const auto reason = detail::win32_last_error_message();
#else
        const auto reason = detail::dynamic_loader_error();
#endif
        if (fatal) {
            OC_ERROR_FORMAT("Failed to load dynamic module '{}', reason: {}.", path_string, reason);
        }
        OC_WARNING_FORMAT("Failed to load dynamic module '{}', reason: {}.", path_string, reason);
    }
    return module;
}

}// namespace

void *dynamic_module_load(const fs::path &path) noexcept {
    return load_dynamic_module(path, true);
}

void *dynamic_module_try_load(const fs::path &path) noexcept {
    return load_dynamic_module(path, false);
}

void dynamic_module_destroy(void *handle) noexcept {
    if (handle != nullptr) {
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
        ::dlclose(handle);
#endif
    }
}

void *dynamic_module_find_symbol(void *handle, ocarina::string_view name_view) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    static thread_local ocarina::string name;
    name = name_view;
    OC_DEBUG_FORMAT("Loading dynamic symbol: {}.", name);
#if defined(_WIN32)
    auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str());
    if (symbol == nullptr) [[unlikely]] {
        OC_INFO_FORMAT("Failed to load symbol '{}', reason: {}.",
                       name, detail::win32_last_error_message());
    }
    return reinterpret_cast<void *>(symbol);
#else
    ::dlerror();
    void *symbol = ::dlsym(handle, name.c_str());
    if (const char *error = ::dlerror(); error != nullptr) [[unlikely]] {
        OC_INFO_FORMAT("Failed to load symbol '{}', reason: {}.", name, error);
        return nullptr;
    }
    return symbol;
#endif
}

ocarina::string dynamic_module_name(ocarina::string_view name) noexcept {
#if defined(_WIN32)
    return ocarina::string(name) + ".dll";
#else
    // Backends are CMake MODULE libraries, whose macOS suffix is .so.
    return string{OC_SHARED_MODULE_PREFIX} + string{name} + OC_SHARED_MODULE_SUFFIX;
#endif
}

string demangle(const char *name) noexcept {
#if defined(_WIN32)
    char buffer[256u];
    auto length = UnDecorateSymbolName(name, buffer, 256, 0);
    return {buffer, length};
#else
    if (name == nullptr) {
        return "???";
    }
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    string result = status == 0 && demangled != nullptr ? demangled : name;
    std::free(demangled);
    return result;
#endif
}

vector<TraceItem> traceback(int top) noexcept {
#if defined(_WIN32)
    void *stack[100];
    auto process = GetCurrentProcess();
    SymInitialize(process, nullptr, true);
    auto frame_count = CaptureStackBackTrace(0, 100, stack, nullptr);

    struct Symbol : SYMBOL_INFO {
        char name_storage[1023];
    } symbol{};
    symbol.MaxNameLen = 1024;
    symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
    vector<TraceItem> trace;
    trace.reserve(frame_count - 1u);
    for (auto i = 1u + top; i < frame_count; i++) {
        auto address = reinterpret_cast<uint64_t>(stack[i]);
        auto displacement = 0ull;
        if (SymFromAddr(process, address, &displacement, &symbol)) {
            TraceItem item{};
            if (SymGetModuleInfo64(process, symbol.ModBase, &module)) {
                item.module = module.ModuleName;
            } else {
                item.module = "???";
            }
            item.symbol = symbol.Name;
            item.address = address;
            item.offset = displacement;
            trace.emplace_back(std::move(item));
        } else {
//            OC_WARNING_FORMAT(
//                "Failed to get stacktrace at 0x{:012}: {}",
//                address, detail::win32_last_error_message());
        }
    }
    return trace;
#else
    void *stack[100]{};
    const int frame_count = ::backtrace(stack, 100);
    const int skip = std::max(top, 0);
    const int first_frame = skip < frame_count ? skip + 1 : frame_count;
    vector<TraceItem> trace;
    trace.reserve(static_cast<size_t>(frame_count - first_frame));
    for (int index = first_frame; index < frame_count; ++index) {
        Dl_info info{};
        TraceItem item{};
        item.address = reinterpret_cast<uint64_t>(stack[index]);
        if (::dladdr(stack[index], &info) != 0) {
            item.module = info.dli_fname == nullptr ? "???" : info.dli_fname;
            item.symbol = demangle(info.dli_sname);
            item.offset = info.dli_saddr == nullptr
                              ? 0u
                              : static_cast<size_t>(reinterpret_cast<uintptr_t>(stack[index]) -
                                                    reinterpret_cast<uintptr_t>(info.dli_saddr));
        } else {
            item.module = "???";
            item.symbol = "???";
        }
        trace.emplace_back(std::move(item));
    }
    return trace;
#endif
}

string traceback_string(int top) noexcept {
    string ret;
    vector<TraceItem> trace = traceback(top);
    for (int i = 1; i < trace.size(); ++i) {
        auto &&t = trace[i];
        using namespace std::string_view_literals;
        ret += fmt::format(
            FMT_STRING("\n    {:>2}: {} :: {} + {}"sv),
            i, t.module, t.symbol, t.offset);
    }
    return ret;
}

}// namespace ocarina
