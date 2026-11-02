//
// Created by Zero on 09/06/2022.
//

#include "platform.h"
#include "oc_windows.h"
#include <dbghelp.h>
#include "fmt/format.h"
#include "core/util/logging.h"
#include "core/util/util.h"

namespace ocarina {

namespace detail {
[[nodiscard]] ocarina::string win32_last_error_message() {
    void *buffer = nullptr;
    auto err_code = GetLastError();
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&buffer,
        0, nullptr);
    ocarina::string err_msg{fmt::format("{} (code = 0x{:x}).", static_cast<char *>(buffer), err_code)};
    LocalFree(buffer);
    return err_msg;
}
}// namespace detail

void *dynamic_module_load(const fs::path &path) noexcept {
    auto path_string = path.string();
    TIMER_TAG(dynamic_module_load, "load module: "+path.string())
    auto module = LoadLibraryA(path_string.c_str());
    if (module == nullptr) [[unlikely]] {
        OC_ERROR_FORMAT(
            "Failed to load dynamic module '{}', reason: {}.",
            path_string, detail::win32_last_error_message());
    }
    return module;
}

void dynamic_module_destroy(void *handle) noexcept {
    if (handle != nullptr) {
        FreeLibrary(reinterpret_cast<HMODULE>(handle));
    }
}

void *dynamic_module_find_symbol(void *handle, ocarina::string_view name_view) noexcept {
    static thread_local ocarina::string name;
    name = name_view;
    OC_DEBUG_FORMAT("Loading dynamic symbol: {}.", name);
    auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str());
    if (symbol == nullptr) [[unlikely]] {
        OC_INFO_FORMAT("Failed to load symbol '{}', reason: {}.",
                       name, detail::win32_last_error_message());
    }
    return reinterpret_cast<void *>(symbol);
}

ocarina::string dynamic_module_name(ocarina::string_view name) noexcept {
    return ocarina::string(name) + ".dll";
}

string demangle(const char *name) noexcept {
    char buffer[256u];
    auto length = UnDecorateSymbolName(name, buffer, 256, 0);
    return {buffer, length};
}

vector<TraceItem> traceback(int top) noexcept {

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