#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// 着色器路径解析
inline std::string resolveShaderPath()
{
    std::string runtimePath = std::filesystem::current_path().string();
    std::regex pattern(R"((.*)Horizon\b)");
    std::smatch matches;

    if (std::regex_search(runtimePath, matches, pattern) && matches.size() > 1)
    {
        std::string resultPath = matches[1].str() + "Horizon";
        std::replace(resultPath.begin(), resultPath.end(), '\\', '/');
        return resultPath + "/Examples";
    }

    throw std::runtime_error("Failed to resolve shader path.");
}

inline const std::string shaderPath = resolveShaderPath();
inline const std::string textureAssetPath = shaderPath + "/assets/textures";
inline const std::string defaultTexturePath = textureAssetPath + "/awesomeface.png";

// 文件读取工具
inline std::string readStringFile(const std::string_view file_path)
{
    std::ifstream file(file_path.data());
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + std::string(file_path));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
