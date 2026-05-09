#pragma once
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Corona::Kernel {

/**
 * @brief 虚拟文件系统接口
 *
 * IVirtualFileSystem 提供了平台无关的文件访问抽象。
 * 通过挂载点（mount point）将虚拟路径映射到物理路径，实现灵活的资源管理。
 *
 * 特性：
 * - 虚拟路径映射：将逻辑路径映射到实际文件系统路径
 * - 平台无关：隐藏不同操作系统的文件系统差异
 * - 模块化资源：可以将不同来源的资源挂载到统一的虚拟目录树
 *
 * 使用示例：
 * @code
 * auto vfs = KernelContext::instance().vfs();
 * vfs->mount("/assets", "C:/Game/Assets");      // Windows
 * vfs->mount("/config", "/etc/game/config");    // Linux
 *
 * auto data = vfs->read_file("/assets/texture.png");
 * vfs->write_file("/config/settings.json", data);
 * @endcode
 */
class IVirtualFileSystem {
   public:
    virtual ~IVirtualFileSystem() = default;

    // ========================================
    // 挂载点管理
    // ========================================

    /**
     * @brief 将物理路径挂载到虚拟路径
     * @param virtual_path 虚拟路径，如 "/assets" 或 "/config"
     * @param physical_path 实际文件系统路径，如 "C:/Game/Assets"
     * @return 挂载成功返回 true，失败返回 false
     *
     * 挂载后，访问虚拟路径会自动映射到物理路径
     */
    virtual bool mount(std::string_view virtual_path, std::string_view physical_path) = 0;

    /**
     * @brief 取消挂载虚拟路径
     * @param virtual_path 要取消挂载的虚拟路径
     */
    virtual void unmount(std::string_view virtual_path) = 0;

    /**
     * @brief 将虚拟路径解析为物理路径
     * @param virtual_path 虚拟路径
     * @return 对应的物理路径，如果路径未挂载则返回空字符串
     */
    virtual std::string resolve(std::string_view virtual_path) const = 0;

    // ========================================
    // 文件操作
    // ========================================

    /**
     * @brief 读取文件内容
     * @param virtual_path 虚拟文件路径
     * @return 文件内容的字节数组，读取失败返回空数组
     */
    virtual std::vector<std::byte> read_file(std::string_view virtual_path) = 0;

    /**
     * @brief 写入文件内容
     * @param virtual_path 虚拟文件路径
     * @param data 要写入的数据
     * @return 写入成功返回 true，失败返回 false
     */
    virtual bool write_file(std::string_view virtual_path, std::span<const std::byte> data) = 0;

    /**
     * @brief 检查文件或目录是否存在
     * @param virtual_path 虚拟路径
     * @return 存在返回 true，不存在返回 false
     */
    virtual bool exists(std::string_view virtual_path) = 0;

    // ========================================
    // 目录操作
    // ========================================

    /**
     * @brief 列出目录下的所有文件和子目录
     * @param virtual_path 虚拟目录路径
     * @return 文件和目录名列表，失败返回空数组
     */
    virtual std::vector<std::string> list_directory(std::string_view virtual_path) = 0;

    /**
     * @brief 创建目录
     * @param virtual_path 虚拟目录路径
     * @return 创建成功返回 true，失败返回 false
     *
     * 如果父目录不存在，会递归创建
     */
    virtual bool create_directory(std::string_view virtual_path) = 0;
};

/**
 * @brief 创建虚拟文件系统实例
 * @return 虚拟文件系统的唯一指针
 */
std::unique_ptr<IVirtualFileSystem> create_vfs();

}  // namespace Corona::Kernel
