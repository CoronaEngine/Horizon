#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Corona::PAL {

/**
 * @brief 文件系统接口（平台抽象层）
 *
 * IFileSystem 提供跨平台的文件和目录操作抽象。
 * 此接口是平台无关的，具体实现由各平台提供。
 *
 * 支持的操作：
 * - 文件读写：读取和写入文件内容
 * - 文件检查：检查文件或目录是否存在
 * - 目录操作：创建目录、列出目录内容
 *
 * 实现平台：
 * - Windows：基于 Win32 API
 * - Linux/Unix：基于 POSIX API
 * - 其他：根据需要扩展
 *
 * 使用示例：
 * @code
 * auto fs = create_file_system();  // 创建平台相关的实现
 *
 * // 写入文件
 * std::string data = "Hello, World!";
 * std::vector<std::byte> bytes(data.begin(), data.end());
 * fs->write_all_bytes("test.txt", bytes);
 *
 * // 读取文件
 * auto content = fs->read_all_bytes("test.txt");
 *
 * // 检查文件
 * if (fs->exists("test.txt")) {
 *     // 文件存在
 * }
 * @endcode
 */
class IFileSystem {
   public:
    virtual ~IFileSystem() = default;

    /**
     * @brief 读取文件的所有内容
     * @param path 文件路径（相对或绝对路径）
     * @return 文件内容的字节数组，失败返回空数组
     *
     * 一次性读取整个文件到内存，适用于小到中等大小的文件。
     * 对于大文件，应考虑流式读取（未来扩展）。
     */
    virtual std::vector<std::byte> read_all_bytes(std::string_view path) = 0;

    /**
     * @brief 写入数据到文件
     * @param path 文件路径
     * @param data 要写入的数据
     * @return 写入成功返回 true，失败返回 false
     *
     * 如果文件不存在会创建，如果文件存在会覆盖。
     * 如果父目录不存在，写入会失败。
     */
    virtual bool write_all_bytes(std::string_view path, std::span<const std::byte> data) = 0;

    /**
     * @brief 检查文件或目录是否存在
     * @param path 文件或目录路径
     * @return 存在返回 true，不存在返回 false
     */
    virtual bool exists(std::string_view path) = 0;

    // ========================================
    // 目录操作
    // ========================================

    /**
     * @brief 创建目录
     * @param path 目录路径
     * @return 创建成功返回 true，失败返回 false
     *
     * 如果父目录不存在，会递归创建（类似 mkdir -p）。
     * 如果目录已存在，返回 true。
     */
    virtual bool create_directory(std::string_view path) = 0;

    /**
     * @brief 列出目录中的所有文件和子目录
     * @param path 目录路径
     * @return 文件和目录名列表（不包含 . 和 ..），失败返回空数组
     *
     * 返回的是文件/目录名，不是完整路径。
     */
    virtual std::vector<std::string> list_directory(std::string_view path) = 0;
};

}  // namespace Corona::PAL
