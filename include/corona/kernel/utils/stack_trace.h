#pragma once

#include <sstream>
#include <string>

#include "corona/pal/cfw_platform.h"

// clang-format off
#if defined(CFW_PLATFORM_WINDOWS)
#include <mutex>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif
// clang-format on

namespace Corona::Kernel::Utils {

/**
 * @brief 捕获当前线程的调用堆栈
 *
 * @param skip_frames 跳过的栈帧数量（通常跳过本函数自身）
 * @param max_frames 最大捕获的栈帧数量
 * @return 格式化的堆栈跟踪字符串
 *
 * @note Windows平台使用DbgHelp API，其他平台暂时返回简化信息
 * @note 此函数会有一定性能开销，建议仅在调试或错误处理时使用
 */
inline std::string capture_stack_trace(int skip_frames = 1, int max_frames = 20) {
#if defined(CFW_PLATFORM_WINDOWS)
    static std::mutex sym_mutex;
    std::lock_guard<std::mutex> lock(sym_mutex);

    std::ostringstream oss;

    // 初始化符号处理器
    HANDLE process = GetCurrentProcess();
    static bool sym_initialized = false;
    if (!sym_initialized) {
        SymInitialize(process, NULL, TRUE);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        sym_initialized = true;
    }

    // 捕获堆栈帧
    constexpr int MAX_STACK_FRAMES = 64;
    void* stack[MAX_STACK_FRAMES];
    WORD frames = CaptureStackBackTrace(skip_frames,
                                        (max_frames < MAX_STACK_FRAMES) ? max_frames : MAX_STACK_FRAMES,
                                        stack,
                                        NULL);

    // 准备符号信息结构
    constexpr size_t MAX_NAME_LEN = 255;
    SYMBOL_INFO* symbol = static_cast<SYMBOL_INFO*>(
        calloc(sizeof(SYMBOL_INFO) + MAX_NAME_LEN * sizeof(char), 1));
    if (!symbol) {
        return "Call Stack: <memory allocation failed>\n";
    }

    symbol->MaxNameLen = MAX_NAME_LEN;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    // 用于获取源文件和行号
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    oss << "Call Stack (" << frames << " frames):\n";

    for (int i = 0; i < frames; i++) {
        DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);
        DWORD64 displacement64 = 0;
        DWORD displacement32 = 0;

        // 获取函数符号
        bool has_symbol = SymFromAddr(process, address, &displacement64, symbol);

        // 获取源文件位置
        bool has_line = SymGetLineFromAddr64(process, address, &displacement32, &line);

        oss << "  #" << i << " ";

        if (has_symbol) {
            oss << symbol->Name;
            if (displacement64 > 0) {
                oss << " +0x" << std::hex << displacement64 << std::dec;
            }
        } else {
            oss << "<unknown function>";
        }

        oss << " [0x" << std::hex << address << std::dec << "]";

        if (has_line) {
            oss << "\n     at " << line.FileName << ":" << line.LineNumber;
        }

        oss << "\n";
    }

    free(symbol);
    return oss.str();

#elif defined(CFW_PLATFORM_LINUX) || defined(CFW_PLATFORM_UNIX)
    // Linux下可以使用backtrace和backtrace_symbols
    // 这里先返回简化信息，后续可以扩展
    return "Call Stack: <Linux stack trace not yet implemented>\n";

#else
    return "Call Stack: <platform not supported>\n";
#endif
}

/**
 * @brief 轻量级堆栈跟踪，仅捕获地址不解析符号
 *
 * @param skip_frames 跳过的栈帧数量
 * @param max_frames 最大捕获的栈帧数量
 * @return 格式化的堆栈地址字符串
 *
 * @note 比完整版快得多，适合频繁调用的场景
 */
inline std::string capture_stack_trace_light(int skip_frames = 1, int max_frames = 10) {
#if defined(CFW_PLATFORM_WINDOWS)
    std::ostringstream oss;

    constexpr int MAX_STACK_FRAMES = 32;
    void* stack[MAX_STACK_FRAMES];
    WORD frames = CaptureStackBackTrace(skip_frames,
                                        (max_frames < MAX_STACK_FRAMES) ? max_frames : MAX_STACK_FRAMES,
                                        stack,
                                        NULL);

    oss << "Call Stack (addresses only, " << frames << " frames):\n";
    for (int i = 0; i < frames; i++) {
        oss << "  #" << i << " [0x" << std::hex
            << reinterpret_cast<uintptr_t>(stack[i])
            << std::dec << "]\n";
    }

    return oss.str();
#else
    return "Call Stack: <platform not supported>\n";
#endif
}

}  // namespace Corona::Kernel::Utils
