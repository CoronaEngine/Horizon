// VFS Example - 展示虚拟文件系统
// 演示如何使用VFS进行文件操作、路径映射和资源管理

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "corona/kernel/core/i_vfs.h"
#include "corona/kernel/core/kernel_context.h"

using namespace Corona::Kernel;

// ========================================
// 辅助函数
// ========================================

std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<std::byte> string_to_bytes(const std::string& str) {
    std::vector<std::byte> bytes;
    bytes.reserve(str.size());
    for (char c : str) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    return bytes;
}

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - VFS Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    auto* vfs = kernel.vfs();

    // 获取当前工作目录
    auto current_dir = std::filesystem::current_path();
    auto vfs_data_dir = current_dir / "04_vfs_data";

    std::cout << "Current directory: " << current_dir.string() << std::endl;
    std::cout << "VFS data directory: " << vfs_data_dir.string() << std::endl;
    std::cout << std::endl;

    // ========================================
    // 示例 1: 挂载虚拟路径
    // ========================================
    std::cout << "[Example 1] Mount Virtual Paths" << std::endl;

    // 挂载不同的资源目录到示例程序目录下
    vfs->mount("/config/", (vfs_data_dir / "config").string() + "/");
    vfs->mount("/data/", (vfs_data_dir / "data").string() + "/");
    vfs->mount("/assets/textures/", (vfs_data_dir / "assets/textures").string() + "/");
    vfs->mount("/assets/models/", (vfs_data_dir / "assets/models").string() + "/");
    vfs->mount("/saves/", (vfs_data_dir / "saves").string() + "/");

    std::cout << "  [OK] Mounted 5 virtual paths" << std::endl;
    std::cout << "    /config/          -> " << (vfs_data_dir / "config").string() << "/" << std::endl;
    std::cout << "    /data/            -> " << (vfs_data_dir / "data").string() << "/" << std::endl;
    std::cout << "    /assets/textures/ -> " << (vfs_data_dir / "assets/textures").string() << "/" << std::endl;
    std::cout << "    /assets/models/   -> " << (vfs_data_dir / "assets/models").string() << "/" << std::endl;
    std::cout << "    /saves/           -> " << (vfs_data_dir / "saves").string() << "/" << std::endl;
    std::cout << std::endl;

    // ========================================
    // 示例 2: 通过VFS写入文件
    // ========================================
    std::cout << "[Example 2] Write Files Through VFS" << std::endl;

    // 写入配置文件
    std::string config_content = R"({
  "game_name": "CoronaGame",
  "version": "1.0.0",
  "fullscreen": true,
  "resolution": {
    "width": 1920,
    "height": 1080
  }
})";

    if (vfs->write_file("/config/game_settings.json", string_to_bytes(config_content))) {
        CFW_LOG_INFO("[OK] Created config file: /config/game_settings.json");
    }

    // 写入玩家数据
    std::string player_data = R"({
  "player_id": 12345,
  "name": "Player1",
  "level": 10,
  "experience": 2500,
  "gold": 1000
})";

    if (vfs->write_file("/data/player.json", string_to_bytes(player_data))) {
        CFW_LOG_INFO("[OK] Created data file: /data/player.json");
    }

    // 写入存档
    std::string save_data = "SAVE_DATA_v1|2025-01-15|Level:10|HP:100/100|MP:50/50";
    if (vfs->write_file("/saves/save_001.dat", string_to_bytes(save_data))) {
        CFW_LOG_INFO("[OK] Created save file: /saves/save_001.dat");
    }

    std::cout << std::endl;

    // ========================================
    // 示例 3: 通过VFS读取文件
    // ========================================
    std::cout << "[Example 3] Read Files Through VFS" << std::endl;

    // 读取配置文件
    auto config_bytes = vfs->read_file("/config/game_settings.json");
    if (!config_bytes.empty()) {
        std::string config = bytes_to_string(config_bytes);
        std::cout << "  Config file content:" << std::endl;
        std::cout << config << std::endl;
    }

    // 读取玩家数据
    auto player_bytes = vfs->read_file("/data/player.json");
    if (!player_bytes.empty()) {
        CFW_LOG_INFO("[OK] Loaded player data ({} bytes)", player_bytes.size());
    }

    std::cout << std::endl;

    // ========================================
    // 示例 4: 文件存在性检查
    // ========================================
    std::cout << "[Example 4] File Existence Check" << std::endl;

    std::vector<std::string> files_to_check = {
        "/config/game_settings.json",
        "/data/player.json",
        "/saves/save_001.dat",
        "/saves/save_002.dat"  // 不存在
    };

    for (const auto& file : files_to_check) {
        bool exists = vfs->exists(file);
        std::cout << "  " << file << ": "
                  << (exists ? "[Exists]" : "[Not found]") << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 示例 5: 路径解析
    // ========================================
    std::cout << "[Example 5] Path Resolution" << std::endl;

    std::vector<std::string> virtual_paths = {
        "/config/game_settings.json",
        "/data/player.json",
        "/assets/textures/player.png",
        "/saves/save_001.dat"};

    for (const auto& vpath : virtual_paths) {
        std::string resolved = vfs->resolve(vpath);
        std::cout << "  " << vpath << std::endl;
        std::cout << "    -> " << resolved << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 示例 6: 目录操作
    // ========================================
    std::cout << "[Example 6] Directory Operations" << std::endl;

    // 创建目录
    if (vfs->create_directory("/config/backup/")) {
        CFW_LOG_INFO("[OK] Created directory: /config/backup/");
    }

    if (vfs->create_directory("/data/cache/temp/")) {
        CFW_LOG_INFO("[OK] Created nested directory: /data/cache/temp/");
    }

    // 列出目录内容
    std::cout << "\n  Contents of /config/:" << std::endl;
    auto config_files = vfs->list_directory("/config/");
    for (const auto& file : config_files) {
        std::cout << "    - " << file << std::endl;
    }

    std::cout << "\n  Contents of /data/:" << std::endl;
    auto data_files = vfs->list_directory("/data/");
    for (const auto& file : data_files) {
        std::cout << "    - " << file << std::endl;
    }

    std::cout << std::endl;

    // ========================================
    // 示例 7: 删除文件
    // ========================================
    std::cout << "[Example 7] Delete Files" << std::endl;

    // 创建临时文件
    std::string temp_content = "This is a temporary file";
    vfs->write_file("/data/temp.txt", string_to_bytes(temp_content));
    std::cout << "  Created temporary file: /data/temp.txt" << std::endl;

    // 注意: VFS没有delete_file方法,这里只是演示文件存在性检查
    // 实际删除需要通过底层文件系统

    // 验证文件存在
    if (vfs->exists("/data/temp.txt")) {
        std::cout << "  [OK] Temporary file exists" << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 示例 8: 实际应用 - 配置管理器
    // ========================================
    std::cout << "[Example 8] Real-world Example - Configuration Manager" << std::endl;

    // 默认配置
    std::string default_config = R"({
  "audio": {
    "master_volume": 1.0,
    "music_volume": 0.8,
    "sfx_volume": 0.9
  },
  "graphics": {
    "vsync": true,
    "anti_aliasing": "FXAA",
    "shadow_quality": "high"
  },
  "controls": {
    "mouse_sensitivity": 1.5,
    "invert_y_axis": false
  }
})";

    // 保存默认配置
    vfs->write_file("/config/default_settings.json", string_to_bytes(default_config));
    CFW_LOG_INFO("[OK] Saved default configuration");

    // 用户自定义配置(覆盖部分默认值)
    std::string user_config = R"({
  "audio": {
    "master_volume": 0.7
  },
  "graphics": {
    "shadow_quality": "medium"
  }
})";

    vfs->write_file("/config/user_settings.json", string_to_bytes(user_config));
    CFW_LOG_INFO("[OK] Saved user configuration");

    // 读取配置
    auto default_cfg = vfs->read_file("/config/default_settings.json");
    auto user_cfg = vfs->read_file("/config/user_settings.json");

    if (!default_cfg.empty() && !user_cfg.empty()) {
        std::cout << "  [OK] Configuration loaded successfully" << std::endl;
        std::cout << "    Default config: " << default_cfg.size() << " bytes" << std::endl;
        std::cout << "    User config: " << user_cfg.size() << " bytes" << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 示例 9: 卸载挂载点
    // ========================================
    std::cout << "[Example 9] Unmount Paths" << std::endl;

    vfs->unmount("/assets/models/");
    CFW_LOG_INFO("[OK] Unmounted /assets/models/");

    // 尝试访问已卸载的路径
    if (!vfs->exists("/assets/models/character.obj")) {
        std::cout << "  [OK] Cannot access unmounted path (expected)" << std::endl;
    }
    std::cout << std::endl;

    // ========================================
    // 性能测试
    // ========================================
    std::cout << "[Performance Test] File Operations..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // 批量写入
    for (int i = 0; i < 100; ++i) {
        std::string filename = "/data/test_" + std::to_string(i) + ".txt";
        std::string content = "Test file content " + std::to_string(i);
        vfs->write_file(filename, string_to_bytes(content));
    }

    // 批量读取
    for (int i = 0; i < 100; ++i) {
        std::string filename = "/data/test_" + std::to_string(i) + ".txt";
        vfs->read_file(filename);
    }

    // 注意: VFS当前不支持删除操作,实际应用中需要通过PAL的文件系统删除

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Performed 200 file operations (100 write + 100 read)" << std::endl;
    std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "  Average: " << (duration.count() / 200.0) << " ms per operation" << std::endl;

    // 清理
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
