#include <memory>
#include <string>

#include "corona/pal/i_dynamic_library.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Corona::PAL {

class WinDynamicLibrary : public IDynamicLibrary {
   public:
    WinDynamicLibrary() : handle_(nullptr) {}

    ~WinDynamicLibrary() override {
        unload();
    }

    bool load(std::string_view path) override {
        // Convert UTF-8 to wide string safely using std::wstring
        int path_length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
        if (path_length == 0) {
            return false;
        }

        std::wstring wide_path(path_length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide_path.data(), path_length);

        handle_ = LoadLibraryW(wide_path.c_str());
        return handle_ != nullptr;
    }

    void unload() override {
        if (handle_) {
            FreeLibrary(handle_);
            handle_ = nullptr;
        }
    }

    FunctionPtr get_symbol(std::string_view name) override {
        if (!handle_) {
            return nullptr;
        }

        // name 必须是 null-terminated 的，所以我们创建一个临时字符串
        std::string name_str(name);
        return reinterpret_cast<FunctionPtr>(GetProcAddress(handle_, name_str.c_str()));
    }

   private:
    HMODULE handle_;
};

// Factory function implementation
std::unique_ptr<IDynamicLibrary> create_dynamic_library() {
    return std::make_unique<WinDynamicLibrary>();
}

}  // namespace Corona::PAL

#endif  // _WIN32
