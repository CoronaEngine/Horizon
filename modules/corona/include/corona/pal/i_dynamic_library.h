#pragma once
#include <string_view>

namespace Corona::PAL {

/// @brief 函数指针类型（用于动态库符号）
using FunctionPtr = void (*)();

/**
 * @brief 动态库接口（平台抽象层）
 *
 * IDynamicLibrary 提供跨平台的动态库（DLL/SO）加载和符号解析。
 *
 * 支持的操作：
 * - 加载动态库：从文件路径加载 DLL/SO
 * - 卸载动态库：释放库资源
 * - 符号解析：根据名称获取函数指针
 *
 * 实现平台：
 * - Windows：使用 LoadLibrary/GetProcAddress
 * - Linux/Unix：使用 dlopen/dlsym
 *
 * 使用示例：
 * @code
 * auto lib = create_dynamic_library();  // 创建平台相关的实现
 *
 * // 加载动态库
 * if (lib->load("myplugin.dll")) {  // Windows: .dll, Linux: .so
 *     // 获取函数指针
 *     auto func = reinterpret_cast<int(*)()>(lib->get_symbol("my_function"));
 *     if (func) {
 *         int result = func();
 *     }
 *
 *     // 卸载
 *     lib->unload();
 * }
 * @endcode
 *
 * 注意事项：
 * - 符号名称在 C++ 中可能被修饰，建议使用 extern "C"
 * - 卸载前确保没有代码正在使用库中的函数
 * - Windows 和 Linux 的文件扩展名不同（.dll vs .so）
 */
class IDynamicLibrary {
   public:
    virtual ~IDynamicLibrary() = default;

    /**
     * @brief 加载动态库
     * @param path 动态库文件路径（相对或绝对路径）
     * @return 加载成功返回 true，失败返回 false
     *
     * 平台差异：
     * - Windows：支持 .dll 文件
     * - Linux：支持 .so 文件
     * - macOS：支持 .dylib 文件
     */
    virtual bool load(std::string_view path) = 0;

    /**
     * @brief 卸载动态库
     *
     * 释放动态库占用的资源。
     * 卸载后，之前获取的函数指针将失效，不应再使用。
     */
    virtual void unload() = 0;

    /**
     * @brief 获取动态库中的符号（函数或变量）
     * @param name 符号名称
     * @return 符号的函数指针，未找到返回 nullptr
     *
     * 符号名称注意事项：
     * - C 函数：直接使用函数名
     * - C++ 函数：建议使用 extern "C" 避免名称修饰
     * - 名称修饰规则因编译器和平台而异
     *
     * 示例：
     * @code
     * // 插件中定义：
     * extern "C" {
     *     void* create_plugin() { return new MyPlugin(); }
     * }
     *
     * // 主程序中使用：
     * auto create_func = reinterpret_cast<void*(*)()>(
     *     lib->get_symbol("create_plugin"));
     * @endcode
     */
    virtual FunctionPtr get_symbol(std::string_view name) = 0;
};

}  // namespace Corona::PAL
