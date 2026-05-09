#pragma once

/**
 * @file cfw_platform.h
 * @brief Corona Framework 平台、编译器和架构特性宏定义
 *
 * 提供跨平台开发所需的统一宏定义，包括：
 * - 操作系统检测
 * - 编译器检测和版本
 * - CPU 架构检测
 * - 编译器特性（内联、属性、优化提示等）
 * - 调试/发布模式检测
 */

// ============================================================================
// 操作系统检测
// ============================================================================

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
#define CFW_PLATFORM_WINDOWS 1
#ifdef _WIN64
#define CFW_PLATFORM_WIN64 1
#else
#define CFW_PLATFORM_WIN32 1
#endif
#elif defined(__linux__)
#define CFW_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
#define CFW_PLATFORM_APPLE 1
#if TARGET_OS_MAC
#define CFW_PLATFORM_MACOS 1
#elif TARGET_OS_IPHONE
#define CFW_PLATFORM_IOS 1
#endif
#elif defined(__unix__) || defined(__unix)
#define CFW_PLATFORM_UNIX 1
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define CFW_PLATFORM_BSD 1
#else
#define CFW_PLATFORM_UNKNOWN 1
#endif

// 操作系统家族
#if defined(CFW_PLATFORM_LINUX) || defined(CFW_PLATFORM_MACOS) || defined(CFW_PLATFORM_BSD) || defined(CFW_PLATFORM_UNIX)
#define CFW_PLATFORM_POSIX 1
#endif

// ============================================================================
// 编译器检测
// ============================================================================

// 注意：检测顺序很重要！
// Clang 在 Windows 上会定义 _MSC_VER 以保持兼容性，所以必须先检查 __clang__
#if defined(__clang__)
#define CFW_COMPILER_CLANG 1
#define CFW_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#define CFW_COMPILER_NAME "Clang"
// Clang 在 Windows 上模拟 MSVC
#if defined(_MSC_VER)
#define CFW_COMPILER_CLANG_CL 1  // Clang-cl (MSVC 兼容模式)
#endif
#elif defined(_MSC_VER)
#define CFW_COMPILER_MSVC 1
#define CFW_COMPILER_VERSION _MSC_VER
#define CFW_COMPILER_NAME "MSVC"
#elif defined(__GNUC__) || defined(__GNUG__)
#define CFW_COMPILER_GCC 1
#define CFW_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#define CFW_COMPILER_NAME "GCC"
#elif defined(__INTEL_COMPILER) || defined(__ICC)
#define CFW_COMPILER_INTEL 1
#define CFW_COMPILER_VERSION __INTEL_COMPILER
#define CFW_COMPILER_NAME "Intel"
#else
#define CFW_COMPILER_UNKNOWN 1
#define CFW_COMPILER_VERSION 0
#define CFW_COMPILER_NAME "Unknown"
#endif

// ============================================================================
// CPU 架构检测
// ============================================================================

// 64位架构
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#define CFW_ARCH_X86_64 1
#define CFW_ARCH_64BIT 1
#define CFW_ARCH_NAME "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CFW_ARCH_ARM64 1
#define CFW_ARCH_64BIT 1
#define CFW_ARCH_NAME "ARM64"
#elif defined(__riscv) && __riscv_xlen == 64
#define CFW_ARCH_RISCV64 1
#define CFW_ARCH_64BIT 1
#define CFW_ARCH_NAME "RISC-V 64"
// 32位架构
#elif defined(__i386__) || defined(_M_IX86)
#define CFW_ARCH_X86_32 1
#define CFW_ARCH_32BIT 1
#define CFW_ARCH_NAME "x86_32"
#elif defined(__arm__) || defined(_M_ARM)
#define CFW_ARCH_ARM32 1
#define CFW_ARCH_32BIT 1
#define CFW_ARCH_NAME "ARM32"
#elif defined(__riscv) && __riscv_xlen == 32
#define CFW_ARCH_RISCV32 1
#define CFW_ARCH_32BIT 1
#define CFW_ARCH_NAME "RISC-V 32"
#else
#define CFW_ARCH_UNKNOWN 1
#define CFW_ARCH_NAME "Unknown"
#endif

// 架构家族
#if defined(CFW_ARCH_X86_64) || defined(CFW_ARCH_X86_32)
#define CFW_ARCH_X86 1
#endif

#if defined(CFW_ARCH_ARM64) || defined(CFW_ARCH_ARM32)
#define CFW_ARCH_ARM 1
#endif

// 字节序检测
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define CFW_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CFW_BIG_ENDIAN 1
#elif defined(CFW_ARCH_X86) || defined(CFW_ARCH_ARM)
#define CFW_LITTLE_ENDIAN 1  // x86 和 ARM 通常是小端
#else
#define CFW_ENDIAN_UNKNOWN 1
#endif

// ============================================================================
// 调试/发布模式检测
// ============================================================================

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
#define CFW_DEBUG 1
#define CFW_BUILD_TYPE "Debug"
#else
#define CFW_RELEASE 1
#define CFW_BUILD_TYPE "Release"
#endif

// ============================================================================
// C++ 标准版本检测
// ============================================================================

#if __cplusplus >= 202302L
#define CFW_CPP23 1
#define CFW_CPP_VERSION 23
#elif __cplusplus >= 202002L
#define CFW_CPP20 1
#define CFW_CPP_VERSION 20
#elif __cplusplus >= 201703L
#define CFW_CPP17 1
#define CFW_CPP_VERSION 17
#elif __cplusplus >= 201402L
#define CFW_CPP14 1
#define CFW_CPP_VERSION 14
#else
#define CFW_CPP11 1
#define CFW_CPP_VERSION 11
#endif

// ============================================================================
// 编译器特性宏
// ============================================================================

// 强制内联
#if defined(CFW_COMPILER_MSVC)
#define CFW_FORCE_INLINE __forceinline
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_FORCE_INLINE inline __attribute__((always_inline))
#else
#define CFW_FORCE_INLINE inline
#endif

// 禁止内联
#if defined(CFW_COMPILER_MSVC)
#define CFW_NO_INLINE __declspec(noinline)
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_NO_INLINE __attribute__((noinline))
#else
#define CFW_NO_INLINE
#endif

// 分支预测提示
#if defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_LIKELY(x) __builtin_expect(!!(x), 1)
#define CFW_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CFW_LIKELY(x) (x)
#define CFW_UNLIKELY(x) (x)
#endif

// DLL 导入/导出
#if defined(CFW_PLATFORM_WINDOWS)
#define CFW_API_EXPORT __declspec(dllexport)
#define CFW_API_IMPORT __declspec(dllimport)
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_API_EXPORT __attribute__((visibility("default")))
#define CFW_API_IMPORT __attribute__((visibility("default")))
#else
#define CFW_API_EXPORT
#define CFW_API_IMPORT
#endif

// API 宏（根据是否正在编译库来选择导出或导入）
#ifdef CFW_BUILD_SHARED
#ifdef CFW_EXPORTS
#define CFW_API CFW_API_EXPORT
#else
#define CFW_API CFW_API_IMPORT
#endif
#else
#define CFW_API
#endif

// 对齐
#if defined(CFW_COMPILER_MSVC)
#define CFW_ALIGN(n) __declspec(align(n))
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_ALIGN(n) __attribute__((aligned(n)))
#else
#define CFW_ALIGN(n)
#endif

// 缓存行对齐
#if defined(__cpp_lib_hardware_interference_size)
#include <new>
constexpr size_t CFW_CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
static constexpr size_t CFW_CACHE_LINE_SIZE = 64;  // 保守值
#endif

#define CFW_CACHE_ALIGNED CFW_ALIGN(CFW_CACHE_LINE_SIZE)

// 废弃标记
#if defined(CFW_COMPILER_MSVC)
#define CFW_DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define CFW_DEPRECATED(msg)
#endif

// 未使用变量
#if defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_UNUSED __attribute__((unused))
#else
#define CFW_UNUSED
#endif

// 热路径/冷路径
#if defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_HOT __attribute__((hot))
#define CFW_COLD __attribute__((cold))
#else
#define CFW_HOT
#define CFW_COLD
#endif

// Restrict 指针
#if defined(CFW_COMPILER_MSVC)
#define CFW_RESTRICT __restrict
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_RESTRICT __restrict__
#else
#define CFW_RESTRICT
#endif

// 纯函数（无副作用）
#if defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_PURE __attribute__((pure))
#define CFW_CONST __attribute__((const))
#else
#define CFW_PURE
#define CFW_CONST
#endif

// 不返回函数
#if defined(CFW_COMPILER_MSVC)
#define CFW_NORETURN __declspec(noreturn)
#elif defined(CFW_CPP11)
#define CFW_NORETURN [[noreturn]]
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_NORETURN __attribute__((noreturn))
#else
#define CFW_NORETURN
#endif

// 可能未使用
#if defined(CFW_CPP17)
#define CFW_MAYBE_UNUSED [[maybe_unused]]
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_MAYBE_UNUSED __attribute__((unused))
#else
#define CFW_MAYBE_UNUSED
#endif

// 点穿 (fallthrough)
#if defined(CFW_CPP17)
#define CFW_FALLTHROUGH [[fallthrough]]
#elif defined(CFW_COMPILER_GCC) && CFW_COMPILER_VERSION >= 70000
#define CFW_FALLTHROUGH __attribute__((fallthrough))
#elif defined(CFW_COMPILER_CLANG) && __has_attribute(fallthrough)
#define CFW_FALLTHROUGH __attribute__((fallthrough))
#else
#define CFW_FALLTHROUGH
#endif

// 点穿 (nodiscard)
#if defined(CFW_CPP17)
#define CFW_NODISCARD [[nodiscard]]
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_NODISCARD __attribute__((warn_unused_result))
#else
#define CFW_NODISCARD
#endif

// ============================================================================
// 断言和诊断
// ============================================================================

// 静态断言（向后兼容）
#if defined(CFW_CPP11)
#define CFW_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define CFW_STATIC_ASSERT(cond, msg) typedef char static_assertion_##__LINE__[(cond) ? 1 : -1]
#endif

// 编译期警告
#if defined(CFW_COMPILER_MSVC)
#define CFW_PRAGMA(x) __pragma(x)
#define CFW_WARNING(msg) CFW_PRAGMA(message(__FILE__ "(" CFW_STRINGIZE(__LINE__) "): warning: " msg))
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_PRAGMA(x) _Pragma(#x)
#define CFW_WARNING(msg) CFW_PRAGMA(GCC warning msg)
#else
#define CFW_PRAGMA(x)
#define CFW_WARNING(msg)
#endif

// 字符串化
#define CFW_STRINGIZE_IMPL(x) #x
#define CFW_STRINGIZE(x) CFW_STRINGIZE_IMPL(x)

// 连接
#define CFW_CONCAT_IMPL(a, b) a##b
#define CFW_CONCAT(a, b) CFW_CONCAT_IMPL(a, b)

// ============================================================================
// 功能检测
// ============================================================================

// SIMD 支持
#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define CFW_HAS_SSE2 1
#endif

#if defined(__AVX__)
#define CFW_HAS_AVX 1
#endif

#if defined(__AVX2__)
#define CFW_HAS_AVX2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define CFW_HAS_NEON 1
#endif

// 原子操作
#if defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#define CFW_HAS_ATOMICS 1
#elif defined(_MSC_VER) && _MSC_VER >= 1800
#define CFW_HAS_ATOMICS 1
#elif defined(CFW_CPP11)
#define CFW_HAS_ATOMICS 1
#endif

// 线程局部存储
#if defined(CFW_CPP11)
#define CFW_THREAD_LOCAL thread_local
#elif defined(CFW_COMPILER_MSVC)
#define CFW_THREAD_LOCAL __declspec(thread)
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_THREAD_LOCAL __thread
#else
#define CFW_THREAD_LOCAL
#endif

// ============================================================================
// 调试辅助
// ============================================================================

// 断点
#if defined(CFW_COMPILER_MSVC)
#define CFW_DEBUGBREAK() __debugbreak()
#elif defined(CFW_PLATFORM_POSIX)
#include <signal.h>
#define CFW_DEBUGBREAK() raise(SIGTRAP)
#else
#define CFW_DEBUGBREAK() ((void)0)
#endif

// 函数签名
#if defined(CFW_COMPILER_MSVC)
#define CFW_FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_CLANG)
#define CFW_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#else
#define CFW_FUNCTION_SIGNATURE __func__
#endif

// ============================================================================
// 版本信息
// ============================================================================

#define CFW_VERSION_MAJOR 0
#define CFW_VERSION_MINOR 1
#define CFW_VERSION_PATCH 0

#define CFW_VERSION_STRING           \
    CFW_STRINGIZE(CFW_VERSION_MAJOR) \
    "." CFW_STRINGIZE(CFW_VERSION_MINOR) "." CFW_STRINGIZE(CFW_VERSION_PATCH)

// ============================================================================
// 便利宏
// ============================================================================

// 禁用拷贝和移动
#define CFW_NON_COPYABLE(ClassName)       \
    ClassName(const ClassName&) = delete; \
    ClassName& operator=(const ClassName&) = delete;

#define CFW_NON_MOVABLE(ClassName)   \
    ClassName(ClassName&&) = delete; \
    ClassName& operator=(ClassName&&) = delete;

#define CFW_NON_COPYABLE_NON_MOVABLE(ClassName) \
    CFW_NON_COPYABLE(ClassName)                 \
    CFW_NON_MOVABLE(ClassName)

// 单例模式
#define CFW_SINGLETON(ClassName)   \
   public:                         \
    static ClassName& instance() { \
        static ClassName instance; \
        return instance;           \
    }                              \
                                   \
   private:                        \
    CFW_NON_COPYABLE_NON_MOVABLE(ClassName)

// ============================================================================
// 命名空间别名（可选）
// ============================================================================

namespace Corona {
namespace PAL {
namespace Platform {

/**
 * @brief 平台信息结构
 */
struct Info {
    static constexpr const char* os_name =
#if defined(CFW_PLATFORM_WINDOWS)
        "Windows"
#elif defined(CFW_PLATFORM_LINUX)
        "Linux"
#elif defined(CFW_PLATFORM_MACOS)
        "macOS"
#elif defined(CFW_PLATFORM_IOS)
        "iOS"
#else
        "Unknown"
#endif
        ;

    static constexpr const char* compiler_name = CFW_COMPILER_NAME;
    static constexpr int compiler_version = CFW_COMPILER_VERSION;
    static constexpr const char* arch_name = CFW_ARCH_NAME;
    static constexpr int cpp_version = CFW_CPP_VERSION;
    static constexpr const char* build_type = CFW_BUILD_TYPE;

#if defined(CFW_ARCH_64BIT)
    static constexpr bool is_64bit = true;
#else
    static constexpr bool is_64bit = false;
#endif

#if defined(CFW_DEBUG)
    static constexpr bool is_debug = true;
#else
    static constexpr bool is_debug = false;
#endif
};

}  // namespace Platform
}  // namespace PAL
}  // namespace Corona
