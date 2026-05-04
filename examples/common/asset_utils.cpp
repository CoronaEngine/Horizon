#include "asset_utils.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

bool looks_like_project_root(const std::filesystem::path &path)
{
    return std::filesystem::exists(path / "CMakeLists.txt")
        && std::filesystem::exists(path / "examples")
        && std::filesystem::exists(path / "Src")
        && std::filesystem::exists(path / "include");
}

std::filesystem::path project_root_path()
{
#ifdef HELICON_ROOT_PATH
    return std::filesystem::path(HELICON_ROOT_PATH);
#else
    std::filesystem::path cursor = std::filesystem::current_path();
    while (true)
    {
        if (looks_like_project_root(cursor))
        {
            return cursor;
        }

        const std::filesystem::path parent = cursor.parent_path();
        if (parent == cursor || parent.empty())
        {
            break;
        }
        cursor = parent;
    }

    throw std::runtime_error("Failed to resolve Horizon project root path.");
#endif
}

std::filesystem::path examples_root_path()
{
    return project_root_path() / "examples";
}

std::filesystem::path resolve_examples_asset(std::string_view relative_path)
{
    return examples_root_path() / std::filesystem::path(relative_path);
}

std::string read_text_file(const std::filesystem::path &file_path)
{
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + file_path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
