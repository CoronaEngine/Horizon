#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Corona::Kernel {

/**
 * @brief 插件信息结构体
 *
 * 描述插件的基本元数据信息
 */
struct PluginInfo {
    std::string name;                       ///< 插件名称
    std::string version;                    ///< 版本号（如 "1.0.0"）
    std::string description;                ///< 插件描述
    std::vector<std::string> dependencies;  ///< 依赖的其他插件名称列表
};

/**
 * @brief 插件接口
 *
 * 所有插件必须实现此接口。
 * 插件应该通过动态库导出 create_plugin 和 destroy_plugin 函数。
 *
 * 插件生命周期：
 * 1. 加载动态库
 * 2. 调用 initialize() 初始化插件
 * 3. 插件正常运行
 * 4. 调用 shutdown() 清理资源
 * 5. 卸载动态库
 */
class IPlugin {
   public:
    virtual ~IPlugin() = default;

    /**
     * @brief 初始化插件
     * @return 初始化成功返回 true，失败返回 false
     *
     * 在此方法中初始化插件所需的资源、注册服务等
     */
    virtual bool initialize() = 0;

    /**
     * @brief 关闭插件
     *
     * 在此方法中清理资源、注销服务等
     */
    virtual void shutdown() = 0;

    /**
     * @brief 获取插件信息
     * @return 插件的元数据信息
     */
    virtual PluginInfo get_info() const = 0;
};

/**
 * @brief 插件管理器接口
 *
 * IPluginManager 负责动态加载、管理和卸载插件。
 *
 * 特性：
 * - 动态加载：运行时加载动态库（DLL/SO）
 * - 生命周期管理：自动初始化和关闭插件
 * - 依赖管理：根据依赖关系排序初始化顺序
 * - 安全卸载：使用 shared_ptr 管理插件生命周期
 *
 * 使用示例：
 * @code
 * auto pm = KernelContext::instance().plugin_manager();
 * pm->load_plugin("plugins/rendering.dll");
 * pm->load_plugin("plugins/physics.dll");
 * pm->initialize_all();  // 按依赖顺序初始化
 *
 * auto plugin = pm->get_plugin("rendering");
 * // 使用插件...
 *
 * pm->shutdown_all();
 * @endcode
 */
class IPluginManager {
   public:
    virtual ~IPluginManager() = default;

    /**
     * @brief 从动态库加载插件
     * @param path 动态库文件路径（.dll 或 .so）
     * @return 加载成功返回 true，失败返回 false
     *
     * 加载后插件处于未初始化状态，需调用 initialize_all() 初始化
     */
    virtual bool load_plugin(std::string_view path) = 0;

    /**
     * @brief 根据名称卸载插件
     * @param name 插件名称
     *
     * 会先调用插件的 shutdown() 方法，然后卸载动态库
     */
    virtual void unload_plugin(std::string_view name) = 0;

    /**
     * @brief 根据名称获取插件
     * @param name 插件名称
     * @return 插件的共享指针，未找到返回 nullptr
     *
     * 使用 shared_ptr 确保插件在被引用时不会被意外卸载
     */
    virtual std::shared_ptr<IPlugin> get_plugin(std::string_view name) = 0;

    /**
     * @brief 获取所有已加载插件的名称列表
     * @return 插件名称列表
     */
    virtual std::vector<std::string> get_loaded_plugins() const = 0;

    /**
     * @brief 初始化所有已加载的插件
     * @return 所有插件初始化成功返回 true，任一失败返回 false
     *
     * 会根据插件的依赖关系自动排序初始化顺序
     */
    virtual bool initialize_all() = 0;

    /**
     * @brief 关闭所有插件
     *
     * 按初始化相反的顺序关闭插件
     */
    virtual void shutdown_all() = 0;
};

}  // namespace Corona::Kernel
