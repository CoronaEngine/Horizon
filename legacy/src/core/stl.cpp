//
// Created by Zero on 08/06/2022.
//

#include "stl.h"

#include <cwchar>

namespace horizon::core {

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
    if (source == nullptr) {
        return {};
    }

    std::mbstate_t state{};
    const wchar_t *cursor = source;
    const size_t length = std::wcsrtombs(nullptr, &cursor, 0u, &state);
    if (length == static_cast<size_t>(-1)) {
        return {};
    }

    std::string result(length, '\0');
    if (result.empty()) {
        return result;
    }

    state = {};
    cursor = source;
    const size_t converted = std::wcsrtombs(result.data(), &cursor, result.size(), &state);
    return converted == static_cast<size_t>(-1) ? std::string{} : result;
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
    return ::operator new(size, std::align_val_t(alignment));
}

void allocator_deallocate(void *p, size_t alignment) noexcept {
    ::operator delete(p, std::align_val_t(alignment));
}

void *allocator_reallocate(void *p, size_t size, size_t alignment) noexcept {
    allocator_deallocate(p, alignment);
    return allocator_allocate(size, alignment);
}
}// namespace detail
}// namespace horizon::core
