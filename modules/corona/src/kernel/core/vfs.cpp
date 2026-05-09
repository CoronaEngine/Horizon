#include <map>
#include <memory>
#include <mutex>

#include "corona/kernel/core/i_vfs.h"
#include "corona/pal/i_file_system.h"

namespace Corona::PAL {
std::unique_ptr<IFileSystem> create_file_system();
}

namespace Corona::Kernel {

class VirtualFileSystem : public IVirtualFileSystem {
   public:
    VirtualFileSystem() : file_system_(PAL::create_file_system()) {}

    bool mount(std::string_view virtual_path, std::string_view physical_path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string vpath(virtual_path);
        std::string ppath(physical_path);

        // Normalize paths (ensure they end with /)
        if (!vpath.empty() && vpath.back() != '/') {
            vpath += '/';
        }
        if (!ppath.empty() && ppath.back() != '/' && ppath.back() != '\\') {
            ppath += '/';
        }

        mount_points_[vpath] = ppath;
        return true;
    }

    void unmount(std::string_view virtual_path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string vpath(virtual_path);
        if (!vpath.empty() && vpath.back() != '/') {
            vpath += '/';
        }
        mount_points_.erase(vpath);
    }

    std::string resolve(std::string_view virtual_path) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return resolve_impl(virtual_path);
    }

    std::vector<std::byte> read_file(std::string_view virtual_path) override {
        std::string physical_path = resolve(virtual_path);
        return file_system_->read_all_bytes(physical_path);
    }

    bool write_file(std::string_view virtual_path, std::span<const std::byte> data) override {
        std::string physical_path = resolve(virtual_path);
        return file_system_->write_all_bytes(physical_path, data);
    }

    bool exists(std::string_view virtual_path) override {
        std::string physical_path = resolve(virtual_path);
        return file_system_->exists(physical_path);
    }

    std::vector<std::string> list_directory(std::string_view virtual_path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        // For directory operations, ensure path ends with /
        std::string vpath(virtual_path);
        if (!vpath.empty() && vpath.back() != '/') {
            vpath += '/';
        }
        std::string physical_path = resolve_impl(vpath);
        if (physical_path.empty()) {
            return {};
        }
        // Remove trailing / for directory listing
        if (!physical_path.empty() && physical_path.back() == '/') {
            physical_path.pop_back();
        }
        return file_system_->list_directory(physical_path);
    }

    bool create_directory(std::string_view virtual_path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        // For directory operations, ensure path ends with /
        std::string vpath(virtual_path);
        if (!vpath.empty() && vpath.back() != '/') {
            vpath += '/';
        }
        std::string physical_path = resolve_impl(vpath);
        if (physical_path.empty()) {
            return false;
        }
        // Remove trailing / for directory creation
        if (!physical_path.empty() && physical_path.back() == '/') {
            physical_path.pop_back();
        }
        return file_system_->create_directory(physical_path);
    }

   private:
    // Internal resolve without locking (caller must hold lock)
    std::string resolve_impl(std::string_view virtual_path) const {
        std::string vpath(virtual_path);

        // Find the longest matching mount point
        std::string best_match_physical;
        size_t best_match_length = 0;

        for (const auto& [mount_vpath, mount_ppath] : mount_points_) {
            if (vpath.find(mount_vpath) == 0 && mount_vpath.length() > best_match_length) {
                best_match_length = mount_vpath.length();
                best_match_physical = mount_ppath;
            }
        }

        if (best_match_length > 0) {
            // Replace virtual path prefix with physical path
            return best_match_physical + vpath.substr(best_match_length);
        }

        // No mount point found, return as-is
        return std::string(virtual_path);
    }

    std::unique_ptr<PAL::IFileSystem> file_system_;
    std::map<std::string, std::string> mount_points_;
    mutable std::mutex mutex_;
};

// Factory function
std::unique_ptr<IVirtualFileSystem> create_vfs() {
    return std::make_unique<VirtualFileSystem>();
}

}  // namespace Corona::Kernel
