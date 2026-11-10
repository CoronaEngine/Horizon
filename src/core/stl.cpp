//
// Created by Zero on 08/06/2022.
//

#include "stl.h"
#include <algorithm>
#include <cwchar>

namespace ocarina {

fs::path parent_path(const fs::path &p, int levels) {
    fs::path cur_path = p;
    for (int i = 0; i < levels; ++i) {
        cur_path = cur_path.parent_path();
    }
    return cur_path;
}

void clear_directory(const std::filesystem::path &dir_path) {
    try {
        if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
            for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove_all(entry.path());
            }
            std::cout << "Directory cleared: " << dir_path << std::endl;
        } else {
            std::cout << "Directory does not exist: " << dir_path << std::endl;
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error clearing directory: " << e.what() << std::endl;
    }
}

std::string wstring_to_string(const wchar_t *source) {
#ifdef _WIN32
    size_t len = 0;
    wcstombs_s(&len, nullptr, 0, source, 0);
    std::string str(len > 0 ? len - 1 : 0, '\0');
    if (len > 0) {
        wcstombs_s(&len, str.data(), str.size() + 1, source, str.size());
    }
    return str;
#else
    if (source == nullptr) {
        return {};
    }
    std::mbstate_t state{};
    const wchar_t *cursor = source;
    const size_t length = std::wcsrtombs(nullptr, &cursor, 0, &state);
    if (length == static_cast<size_t>(-1)) {
        return {};
    }
    std::string str(length, '\0');
    state = {};
    cursor = source;
    if (std::wcsrtombs(str.data(), &cursor, str.size(), &state) == static_cast<size_t>(-1)) {
        return {};
    }
    return str;
#endif
}

std::string get_file_name(const std::string &file_path) {
    auto it = std::find_if(file_path.rbegin(), file_path.rend(), [](const char c) {
        return c == '\\' || c == '/';
    });
    if (it == file_path.rend()) {
        return file_path;
    }

    return file_path.substr(it.base() - file_path.begin());
}

namespace detail {
void *allocator_allocate(size_t size, size_t alignment) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        std::abort();
    }
    // Keep zero-sized allocations usable and preserve the fail-fast contract.
    const size_t bytes = size == 0 ? 1 : size;
    void *p = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                  ? ::operator new(bytes, std::align_val_t(alignment), std::nothrow)
                  : ::operator new(bytes, std::nothrow);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}

void allocator_deallocate(void *p, size_t alignment) noexcept {
    if (p == nullptr) {
        return;
    }
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(p, std::align_val_t(alignment));
    } else {
        ::operator delete(p);
    }
}
}// namespace detail
}// namespace ocarina
