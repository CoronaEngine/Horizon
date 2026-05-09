#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "corona/pal/i_file_system.h"

namespace fs = std::filesystem;

namespace Corona::PAL {

class StdFileSystem : public IFileSystem {
   public:
    std::vector<std::byte> read_all_bytes(std::string_view path) override {
        std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Failed to open file for reading: " << path << std::endl;
            return {};
        }

        auto size = file.tellg();
        if (size <= 0) {
            return {};
        }

        std::vector<std::byte> buffer(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        if (!file) {
            std::cerr << "Failed to read file: " << path << std::endl;
            return {};
        }

        return buffer;
    }

    bool write_all_bytes(std::string_view path, std::span<const std::byte> data) override {
        std::ofstream file(std::string(path), std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "Failed to open file for writing: " << path << std::endl;
            return false;
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size());

        if (!file) {
            std::cerr << "Failed to write file: " << path << std::endl;
            return false;
        }

        return true;
    }

    bool exists(std::string_view path) override {
        std::error_code ec;
        return fs::exists(fs::path(path), ec);
    }

    bool create_directory(std::string_view path) override {
        try {
            std::error_code ec;
            fs::create_directories(fs::path(path), ec);
            return !ec;
        } catch (...) {
            return false;
        }
    }

    std::vector<std::string> list_directory(std::string_view path) override {
        std::vector<std::string> entries;
        try {
            for (const auto& entry : fs::directory_iterator(fs::path(path))) {
                entries.push_back(entry.path().filename().string());
            }
        } catch (...) {
            // Return empty vector on error
        }
        return entries;
    }
};

// Factory function implementation
std::unique_ptr<IFileSystem> create_file_system() {
    return std::make_unique<StdFileSystem>();
}

}  // namespace Corona::PAL
